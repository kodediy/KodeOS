# The station program's events

`test_dot_final.py --events` prints one line per event on stdout:
`@@TD {json}`, the JSON on one line, a `type` key naming the event. Every
other line on stdout is plain text for a person. `test_dot_ui.py` hands the
events to the page (`page.html`) as server-sent events without changing them,
adding only `rx`, the time it received each one. The page draws the run from
these events alone.

`t` is the program's wall clock in seconds. Step states are `PASS`, `FAIL`,
`TIMEOUT`, `NOVERDICT` and `NOT_RUN`. The verdict itself is `PASS`, `FAIL` or
`NO VERDICT`, prefixed `BENCH ` on a bench run (`--operator-sim`,
`--no-write` or `--no-place`). A bench verdict never reaches the CSV, and a
bench run never resets nor locks the Kode.

## The order of a run

`place` (usb, then waiting, searching, ok) -> `start` -> `phase place` ->
`step`/`result` for each row -> `phase camera` -> `operator` prompts and
answers within `phase buttons` and `phase touch` -> `phase rest` ->
`phase result` -> `photos` -> (`lock`, when the lock failed) -> `end` ->
`removed`. On a PASS the Kode is locked before `end`, so `end` is the run's
last word.

Every row of `start.steps` gets a `result`; rows the run never reached
arrive as `NOT_RUN`.

## `start`

```json
{"type":"start","t":1793260000.0,"serial":"KODE1TEST00001D",
 "versions":{"kodeos":"00.00.24","c5":"2.12.0","mac":"38:44:be:07:e6:54"},
 "steps":[{"id":"place","name":"Placing the Kode in the fixture","block":"place"}, "..."],
 "repeat_of":{"folder":"20261103-114646_KODE1TEST00001D","report":"/.../report.html","verdict":"FAIL"}}
```

`repeat_of` is there only when the same serial comes back within 30 minutes
of a run that did not pass. Both reports are kept and the latest one counts.

## `step` and `result`

```json
{"type":"step","id":"imu","state":"running","t":1793260010.2}
{"type":"result","id":"imu","state":"PASS","values":[{"name":"ms","value":620,"unit":"","range":""}],
 "image":null,"reason":"","step_s":0.62,"total_s":12.4}
```

`image` is the absolute path of the row's first picture, under the run's
folder. `reason` says why a row did not pass: the last error the Kode logged,
or else what was measured.

## `phase`

The point the operator has reached. `text` is meant to be shown large.

```json
{"type":"phase","name":"place","text":"Put the Kode in the nest","t":1793260000.1}
{"type":"phase","name":"camera","text":"DO NOT TOUCH: the camera is looking at the screen","limit_s":8,"t":1793260004.0}
{"type":"phase","name":"buttons","text":"Press the button that lights up","t":1793260012.0}
{"type":"phase","name":"touch","text":"Touch the circle that lights up","t":1793260040.0}
{"type":"phase","name":"rest","text":"Finishing the automatic tests","t":1793260050.0}
{"type":"phase","name":"result","text":"Result: ALL GOOD","result":"pass","t":1793260060.0}
```

`camera.limit_s` is how long the camera is expected to look, for the
"do not touch" countdown. `result.result` is `pass` or `fail`.

## `operator`

A prompt from the Kode, then its answer. `limit_s` is the time the Kode gives
each step and `t` the time it was asked, so a countdown is
`limit_s - (now - t)`.

```json
{"type":"operator","step":1,"of":6,"kind":"press","what":"pad-left","limit_s":20,"t":1793260013.2}
{"type":"operator","step":1,"kind":"answer","result":"pass","button":"pad-left","pressed":null,"ms":131,"target":null,"got":null}
{"type":"operator","step":1,"of":2,"kind":"target","what":"80,215","limit_s":20,"t":1793260040.3}
{"type":"operator","step":1,"kind":"answer","result":"miss","button":null,"pressed":null,"ms":null,"target":"80,215","got":"160,300"}
```

A `press` names the button by the Kode's name: `pad-left`, `pad-right`,
`pad-up`, `pad-down`, `top` (BLUE), `bottom` (RED). A `target` is `"x,y"` in
the pixels of the Kode's glass (410 x 502).

An answer's `result`:

- `pass`, `timeout`.
- `miss`: a touch off the target, forgiven; the same step is asked again.
- `repeat`, with `reason`: `other_button` (another button pressed) or `double`
  (the asked one and another together, forgiven once). A new prompt for the
  same step follows.
- `fail`, with `reason`: `double` (the second time in a step), `no_release`
  (the button still down 3 s after it was judged), `held` (a button already
  down when the step began) or `mistakes` (the third wrong press in a row ends
  the row, and the row fails).

A `timeout` answer has no `ms` and no `got`: the Kode gives none.

`pressed` names what went down, joined with `+`.

## `place`

```json
{"type":"place","state":"searching","text":"Looking for the marks","t":1793260001.0}
{"type":"place","state":"ok","text":"Kode placed: the test starts","t":1793260002.5}
```

`state` is one of `usb`, `waiting`, `searching`, `no_camera`, `misplaced`,
`ok`, and, when the placement ends the run, `no_usb`, `badly_seated`,
`j3_failed` or `panel_failed` (these two carry a `reason` too;
`badly_seated` carries the placement picture as `image`). The run's
clock starts at `ok`. The camera window of a good placement is kept in the
station's configuration (`place.window`).

## `camera`

A picture the camera took, saved in the run's folder.

```json
{"type":"camera","image":"/.../img_cam_location.jpg","pattern":"location","t":1793260003.0}
```

## `log`

```json
{"type":"log","text":"interrupted: the port is let go","level":"info","t":1793260030.0}
```

`level` is `info` or `error`.

## `end`

```json
{"type":"end","verdict":"FAIL","total_s":61.2,"blocks":{"camera":10.4},
 "report":"/.../report.html","repeatable":true,"screen_fails":["cam_pixels"],"lock":"not_sent"}
```

`verdict` is the one written into `report.html`. `lock` is `ok`, `failed` or
`not_sent` (no PASS, or a bench run); a failed lock makes the verdict `FAIL`. `repeatable` is true when
it is not a PASS; `screen_fails` lists the `cam_*` rows that failed, which the
page offers to repeat with the same Kode. A repeat carries `repeat_of` here
too.

## `lock`

Sent only when the Kode did not confirm `test_dot_lock`. The run is then a
FAIL with a `lock` row, `lock: failed` in the report and the CSV, and exit
status 3: the Kode must not leave the station.

```json
{"type":"lock","state":"failed","reason":"the Kode did not confirm the lock: >err code=4 msg=nvs","t":1793260070.0}
```

## `photos`

The run's photos read off the Kode after the verdict is told to it and
before the lock.

```json
{"type":"photos","n":12,"bytes":186000,"secs":4.1,"folder":"/.../20261103-114646_KODE1TEST00001D","retries":1,"missing":[]}
```

## `removed`

After `end` the program closes the port and stays alive until the Kode leaves
the USB, then says so and exits; after 15 minutes it gives up with
`"reason":"time"`. A later run of the same Kode takes over and this one goes
quiet. A stopped run (SIGTERM, the page's Stop) exits without waiting.

```json
{"type":"removed","t":1793260300.0,"reason":"usb"}
```

## The page's own events

`test_dot_ui.py` adds `{"type":"system","kind":...}` events of its own:

- `launched` (`run`, `simulator`): a run started; the page starts from clean.
- `port_busy`, `error` (`text`): the run could not start.
- `stopped` (`text`): the run was stopped from the page.
- `died` (`code`, `text`): the program ended without an `end` event.
- `finished` (`code`): the program ended, whichever way.
