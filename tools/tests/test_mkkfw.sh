#!/usr/bin/env bash
#
# mkkfw.py, checked against bytes rather than against itself.
#
# Every assertion unpacks the header with its own offsets, spelled out here,
# so that changing either side has to be a deliberate change to both. A test
# that called mkkfw's own struct format would agree with any bug it had.
#
#   usage: tools/tests/test_mkkfw.sh
#   exit:  0 all clear, 1 something is wrong
#
set -uo pipefail
cd "$(dirname "$0")/../.."

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0
check() {   # check <name> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '\033[32mok\033[0m    %s\n' "$1"
    else
        printf '\033[31mFAIL\033[0m  %s\n      expected: %s\n      got:      %s\n' \
               "$1" "$2" "$3"
        fail=1
    fi
}
field() {   # field <file> <python-struct-expression>
    python3 -c "
import struct,sys
d=open('$1','rb').read()
print($2)"
}

pyd() {     # pyd <file> <python statements, the last one prints>
    python3 -c "
import struct,sys,json,zlib,hashlib
d=open('$1','rb').read()
$2"
}

# A synthetic image: 4096 bytes that start with the ESP magic, like a real one.
python3 -c "
open('$tmp/in.bin','wb').write(bytes([0xe9]) + bytes(4095))"
sha_in=$(shasum -a 256 "$tmp/in.bin" | cut -d' ' -f1)

# ---- uncompressed ---------------------------------------------------------
python3 tools/mkkfw.py --target os --no-compress "$tmp/in.bin" "$tmp/out.kfw"

check "magic is KFW1"        "4b465731" "$(xxd -p -l4 "$tmp/out.kfw")"
check "header version"       "1"        "$(field "$tmp/out.kfw" "struct.unpack_from('<H',d,4)[0]")"
check "header length"        "72"       "$(field "$tmp/out.kfw" "struct.unpack_from('<H',d,6)[0]")"
check "target os"            "0"        "$(field "$tmp/out.kfw" "d[8]")"
check "compression none"     "0"        "$(field "$tmp/out.kfw" "d[9]")"
check "flags reserved zero"  "0"        "$(field "$tmp/out.kfw" "struct.unpack_from('<H',d,10)[0]")"
check "payload length"       "4096"     "$(field "$tmp/out.kfw" "struct.unpack_from('<I',d,12)[0]")"
check "image length"         "4096"     "$(field "$tmp/out.kfw" "struct.unpack_from('<I',d,16)[0]")"
check "sha256 of the image"  "$sha_in"  "$(field "$tmp/out.kfw" "d[20:52].hex()")"
# `wc -c` rather than `stat`, because the two stats disagree about `-f`: BSD
# reads it as the format string this wanted, GNU as "describe the filesystem"
# and prints a block report instead of a size. Harmless for as long as this
# only ever ran on the bench Mac; CI runs it on Linux.
check "total size"           "4168"     "$(wc -c < "$tmp/out.kfw" | tr -d ' ')"
check "name empty for os"    ""         "$(field "$tmp/out.kfw" "d[56:72].rstrip(bytes(1)).decode()")"
check "payload is the image" "$sha_in"  "$(field "$tmp/out.kfw" "__import__('hashlib').sha256(d[72:]).hexdigest()")"

# The CRC covers the 52 bytes before it, and nothing after - in the board's
# convention, not zlib's default: esp_rom_crc32_le(UINT32_MAX, ..) inverts the
# seed it is handed, so the raw CRC starts at 0 where zlib's starts at ~0.
check "header crc is the one the board computes" "ok" "$(field "$tmp/out.kfw" "
'ok' if __import__('zlib').crc32(d[0:52], 0xFFFFFFFF) & 0xffffffff == struct.unpack_from('<I',d,52)[0] else 'bad'")"

# ---- compressed -----------------------------------------------------------
python3 tools/mkkfw.py --target app --name hello "$tmp/in.bin" "$tmp/c.kfw"

check "target app"           "1"        "$(field "$tmp/c.kfw" "d[8]")"
check "compression deflate"  "1"        "$(field "$tmp/c.kfw" "d[9]")"
check "image length kept"    "4096"     "$(field "$tmp/c.kfw" "struct.unpack_from('<I',d,16)[0]")"
check "payload shrank"       "yes"      "$(field "$tmp/c.kfw" "
'yes' if struct.unpack_from('<I',d,12)[0] < 4096 else 'no'")"
check "name carried"         "hello"    "$(field "$tmp/c.kfw" "d[56:72].rstrip(bytes(1)).decode()")"
check "digest is of the plain image, not the payload" "$sha_in" \
      "$(field "$tmp/c.kfw" "d[20:52].hex()")"
check "payload inflates back to the image" "$sha_in" "$(field "$tmp/c.kfw" "
__import__('hashlib').sha256(
    __import__('zlib').decompress(d[72:72+struct.unpack_from('<I',d,12)[0]])).hexdigest()")"

# ---- app data block -------------------------------------------------------
# The block sits right after the 72-byte header: "KAD1", u16 json_len,
# u16 icon_len, u32 CRC (the header's flavour) over what follows, then the
# json, then the icon. Offsets are spelled out here, not taken from mkkfw.
mkdir "$tmp/ad" "$tmp/ad_json"
printf '{"kode": "1", "name": "Scope", "description": "Two-channel scope"}' \
    > "$tmp/ad/app.json"
cp "$tmp/ad/app.json" "$tmp/ad_json/app.json"
python3 -c "
from PIL import Image
Image.new('RGBA', (10, 20), (200, 30, 40, 255)).save('$tmp/ad/icon.png')"

python3 tools/mkkfw.py --target app --name scope --appdata "$tmp/ad" \
        "$tmp/in.bin" "$tmp/ad.kfw" >/dev/null 2>&1
check "appdata: exits 0"          "0"      "$?"
check "appdata: flags bit 0 set"  "1"      "$(field "$tmp/ad.kfw" "struct.unpack_from('<H',d,10)[0]")"
check "appdata: block magic"      "4b414431" "$(field "$tmp/ad.kfw" "d[72:76].hex()")"
check "appdata: icon_len"         "12288"  "$(field "$tmp/ad.kfw" "struct.unpack_from('<H',d,72+6)[0]")"
check "appdata: json_len is the json's own size" "ok" "$(pyd "$tmp/ad.kfw" "
n=struct.unpack_from('<H',d,72+4)[0]
print('ok' if 0 < n <= 1024 and d[84+n-1:84+n] == b'}' else 'bad')")"
check "appdata: block crc is the board's" "ok" "$(pyd "$tmp/ad.kfw" "
n=struct.unpack_from('<H',d,72+4)[0]; i=struct.unpack_from('<H',d,72+6)[0]
c=zlib.crc32(d[84:84+n+i], 0xFFFFFFFF) & 0xffffffff
print('ok' if c == struct.unpack_from('<I',d,72+8)[0] else 'bad')")"
check "appdata: json is compact, icon key rewritten" \
      '{"kode":"1","name":"Scope","description":"Two-channel scope","icon":"icon.k64"}' \
      "$(pyd "$tmp/ad.kfw" "
n=struct.unpack_from('<H',d,72+4)[0]
print(d[84:84+n].decode())")"
check "appdata: payload_len counts the payload only" "ok" "$(pyd "$tmp/ad.kfw" "
n=struct.unpack_from('<H',d,72+4)[0]; i=struct.unpack_from('<H',d,72+6)[0]
print('ok' if struct.unpack_from('<I',d,12)[0] == len(d) - 72 - 12 - n - i else 'bad')")"
check "appdata: payload after the block inflates to the image" "$sha_in" "$(pyd "$tmp/ad.kfw" "
n=struct.unpack_from('<H',d,72+4)[0]; i=struct.unpack_from('<H',d,72+6)[0]
print(hashlib.sha256(zlib.decompress(d[84+n+i:])).hexdigest())")"
check "appdata: header crc still covers bytes 0..51" "ok" "$(pyd "$tmp/ad.kfw" "
print('ok' if zlib.crc32(d[0:52], 0xFFFFFFFF) & 0xffffffff == struct.unpack_from('<I',d,52)[0] else 'bad')")"

python3 tools/mkkfw.py --target app --name scope --appdata "$tmp/ad_json" \
        "$tmp/in.bin" "$tmp/adj.kfw" >/dev/null 2>&1
check "appdata without an icon: exits 0" "0" "$?"
check "appdata without an icon: icon_len 0" "0" "$(field "$tmp/adj.kfw" "struct.unpack_from('<H',d,72+6)[0]")"
check "appdata without an icon: json as written" '{"kode":"1","name":"Scope","description":"Two-channel scope"}' "$(pyd "$tmp/adj.kfw" "
n=struct.unpack_from('<H',d,72+4)[0]
print(d[84:84+n].decode())")"

rm -f "$tmp/x.kfw"
python3 tools/mkkfw.py --target os --appdata "$tmp/ad" "$tmp/in.bin" "$tmp/x.kfw" >/dev/null 2>&1
check "appdata on a non-app target refused" "1" "$?"
check "appdata refusal writes no file" "absent" "$([ -e "$tmp/x.kfw" ] && echo present || echo absent)"

mkdir "$tmp/ad_long"
printf '{"kode": "1", "description": "%s"}' "$(printf 'x%.0s' $(seq 53))" > "$tmp/ad_long/app.json"
msg=$(python3 tools/mkkfw.py --target app --name scope --appdata "$tmp/ad_long" \
        "$tmp/in.bin" "$tmp/x.kfw" 2>&1 >/dev/null); rc=$?
check "appdata: 53-character description refused" "1" "$rc"
check "appdata: the refusal names the field" "yes" "$(printf '%s' "$msg" | grep -q description && echo yes || echo no)"
check "appdata: a refused block writes no file" "absent" "$([ -e "$tmp/x.kfw" ] && echo present || echo absent)"

mkdir "$tmp/ad_none"
python3 tools/mkkfw.py --target app --name scope --appdata "$tmp/ad_none" \
        "$tmp/in.bin" "$tmp/x.kfw" >/dev/null 2>&1
check "appdata: a folder without app.json refused" "1" "$?"

# ---- app data: the .k64 route and the refusals that write no file -----------
mkdir "$tmp/ad_k64"
printf '{"kode": "1", "name": "K"}' > "$tmp/ad_k64/app.json"
python3 -c "
open('$tmp/ad_k64/icon.k64','wb').write(bytes(range(256)) * 48)"
python3 tools/mkkfw.py --target app --appdata "$tmp/ad_k64" "$tmp/in.bin" "$tmp/k.kfw" >/dev/null 2>&1
check "appdata .k64: exits 0" "0" "$?"
check "appdata .k64: icon packed byte-identical" "ok" "$(pyd "$tmp/k.kfw" "
n=struct.unpack_from('<H',d,72+4)[0]; i=struct.unpack_from('<H',d,72+6)[0]
print('ok' if i == 12288 and d[84+n:84+n+i] == open('$tmp/ad_k64/icon.k64','rb').read() else 'bad')")"
check "appdata .k64: icon key says icon.k64" '{"kode":"1","name":"K","icon":"icon.k64"}' "$(pyd "$tmp/k.kfw" "
n=struct.unpack_from('<H',d,72+4)[0]
print(d[84:84+n].decode())")"

out=$(python3 tools/mkkfw.py --target app --appdata "$tmp/ad_k64" "$tmp/in.bin" "$tmp/k2.kfw")
check "appdata: the summary says what was packed" "yes" "$(printf '%s' "$out" | grep -q 'app data 41 bytes of app.json + 12288 bytes of icon' && echo yes || echo no)"
check "appdata: the ratio is the payload's, the block left out" "yes" "$(printf '%s' "$out" | grep -q '(0% of the original' && echo yes || echo no)"

refused() {   # refused <name> <folder> <word the message must name>
    rm -f "$tmp/x.kfw"
    msg=$(python3 tools/mkkfw.py --target app --appdata "$2" "$tmp/in.bin" "$tmp/x.kfw" 2>&1 >/dev/null); rc=$?
    check "$1: exits 1" "1" "$rc"
    check "$1: the message names $3" "yes" "$(printf '%s' "$msg" | grep -q "$3" && echo yes || echo no)"
    check "$1: no file written" "absent" "$([ -e "$tmp/x.kfw" ] && echo present || echo absent)"
}

mkdir "$tmp/ad_short"
printf '{"kode": "1"}' > "$tmp/ad_short/app.json"
head -c 100 /dev/zero > "$tmp/ad_short/icon.k64"
refused "appdata: a .k64 of the wrong size" "$tmp/ad_short" "icon"

mkdir "$tmp/ad_gone"
printf '{"kode": "1", "icon": "gone.png"}' > "$tmp/ad_gone/app.json"
refused "appdata: an icon key naming a missing file" "$tmp/ad_gone" "gone.png"

mkdir "$tmp/ad_key"
printf '{"kode": "1", "na\\u00efve": "x"}' > "$tmp/ad_key/app.json"
refused "appdata: a non-ASCII key" "$tmp/ad_key" "ASCII"

# ---- refusals -------------------------------------------------------------
python3 tools/mkkfw.py --target os "$tmp/nope.bin" "$tmp/x.kfw" >/dev/null 2>&1
check "missing input refused" "1" "$?"

python3 tools/mkkfw.py --target app --name thisnameiswaytoolong \
        "$tmp/in.bin" "$tmp/x.kfw" >/dev/null 2>&1
check "overlong name refused" "1" "$?"

python3 tools/mkkfw.py --target os --name nope "$tmp/in.bin" "$tmp/x.kfw" >/dev/null 2>&1
check "name on a non-app refused" "1" "$?"

printf 'not an esp image' > "$tmp/bad.bin"
python3 tools/mkkfw.py --target os "$tmp/bad.bin" "$tmp/x.kfw" >/dev/null 2>&1
check "input without the 0xE9 magic refused" "1" "$?"

python3 tools/mkkfw.py --target nonsense "$tmp/in.bin" "$tmp/x.kfw" >/dev/null 2>&1
check "unknown target refused" "2" "$?"

if [ $fail -eq 0 ]; then
    printf '\n\033[32mmkkfw writes what the board is going to read.\033[0m\n'
else
    printf '\n\033[31mmkkfw does not agree with the format.\033[0m\n'
fi
exit $fail
