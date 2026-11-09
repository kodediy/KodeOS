# Custom second-stage bootloader (the mechanism of the IDF's official
# bootloader_multiboot example). It shares the region format header with
# kdrv_apps, and the pin map with kbsp for the panel's reset line.
get_filename_component(KODEOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

idf_component_register(SRCS "bootloader_start.c"
                       PRIV_INCLUDE_DIRS "${KODEOS_ROOT}/sdk/components/kdrv/apps/include"
                                         "${KODEOS_ROOT}/sdk/components/kbsp/board/include"
                       REQUIRES bootloader bootloader_support spi_flash esp_hal_gpio esp_hal_usb)

# No -flto here: LTO cannot see the ENTRY() reference from the linker script,
# so call_start_cpu0 is dropped and --gc-sections then empties the image. The
# result still links, but it is a 304-byte binary with entry 0x0 that faults
# the moment the ROM jumps into it.
target_compile_options(${COMPONENT_LIB} PRIVATE "-Oz" "-fno-unroll-loops" "-fno-unwind-tables" "-fno-asynchronous-unwind-tables")

idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
