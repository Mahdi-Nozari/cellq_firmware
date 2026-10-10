# CellQ Firmware Debug Messages

This guide describes the USB serial debug messages currently produced by the CellQ firmware.

## Serial monitor setup

- Port: the Arduino Mega USB serial port (`Serial`)
- Baud rate: `250000`
- Line ending: any setting is acceptable because the firmware does not read debug commands
- Debug switch: `CELLQ_DEBUG` at the top of `include/BuildConfig.h`
- DAC acknowledgment policy: `CELLQ_REQUIRE_DAC_ACK` immediately below it
- Monitor sensor source: `CELLQ_VIRTUAL_PRESSURE_SENSORS` in `include/BuildConfig.h`
- Factory preset source: `CELLQ_LOAD_FACTORY_PRESET_FROM_SD` in `include/BuildConfig.h`

```cpp
#define CELLQ_DEBUG 1
#define CELLQ_REQUIRE_DAC_ACK 0
#define CELLQ_VIRTUAL_PRESSURE_SENSORS 0
#define CELLQ_LOAD_FACTORY_PRESET_FROM_SD 1
```

Use `1` to enable messages. Use `0` to compile out the debug calls and their text.

For `CELLQ_REQUIRE_DAC_ACK`, use `0` during display-only bench testing so the Square-wave procedure can run without physical DACs. Set it to `1` when the DACs are installed so a missing acknowledgment changes the procedure to ERROR. The firmware attempts the I2C writes in both modes.

Set `CELLQ_VIRTUAL_PRESSURE_SENSORS` to `1` for display-only testing. Set it to `0` when the six physical sensors are connected.

Set `CELLQ_LOAD_FACTORY_PRESET_FROM_SD` to `1` to use valid `/PRESETS/FCTRY1.CSV` and `/PRESETS/FCTRY2.CSV` mirrors. Set it to `0` to always use both compiled factory presets. A failed factory SD load also falls back automatically.

Debug output is best-effort. A message is skipped when there is not enough room in the serial transmit buffer, so debugging cannot block important firmware work.

## Message reference

| Message | Meaning |
|---|---|
| `D,BOOT,OK` | Firmware setup completed. |
| `D,E,<cmd>,<object>,<index>,<value>` | A raw event was received from the display. |
| `D,E,DROP` | The display event queue was full, so an event was discarded. |
| `D,LCD,RETRY,<command>,<index>` | The display did not acknowledge a command within 250 ms, or returned NAK. Firmware queued one retry. Command `1` is an object/image/switch write; `12` is a label write. |
| `D,LCD,DROP,<command>,<index>` | A display command was discarded after a failed retry, because newer navigation took priority, because the transmit queue filled, or because label text exceeded 20 characters. Repeated messages indicate a communication problem; report any missing label. |
| `D,P,<regulator>,<tenths>` | A manual pressure setting changed. |
| `D,DAC,<regulator>,<code>,<result>` | An MCP4725 command was attempted. |
| `D,T,<channel>,<seconds>` | A manual hour, minute, or second field changed; the value is the resulting combined duration in seconds. |
| `D,M,UNIT,<channel>,<unit>` | A Manual time-unit button press selected seconds (0), minutes (1), or hours (2). |
| `D,V,<channel>,<state>` | A physical valve output command was issued. |
| `D,TO,<channel>` | A timed valve reached its duration and was automatically switched off. |
| `D,M,SYNC` | Current Manual-mode pressure, HH:MM:SS fields, unit images, valve switches, and group-link switches were queued for LCD synchronization. |
| `D,MON,P,<channel>,<tenths>` | A Form 3 channel-pressure value was sent to its LCD label. |
| `D,MON,SRC,VIRTUAL` | The active test is using generated pressure values and does not read A8-A13. |
| `D,MON,SRC,ANALOG` | The active test is sampling the physical A8-A13 inputs. Acquisition runs even when Form 3 is hidden; this message appears only when its stream starts. |
| `D,MON,Z,<channel>,<adc>` | A physical sensor's averaged startup ADC value was stored as its 0 kPa reference. |
| `D,SD,OK` | SD card initialization on D10 succeeded at boot. |
| `D,SD,FAIL` | SD card initialization failed; both compiled factory presets remain available. |
| `D,PR,LOAD,<id>,<revision>` | The SD preset passed all parsing, range, and CRC checks. |
| `D,PR,SAVE,<id>,<revision>` | A user-preset A/B revision was written and passed full readback validation. |
| `D,PR,DELETE,<id>` | Both SD files for the user-preset ID were removed. |
| `D,PR,ERR,<id>,<code>` | The requested SD preset could not be loaded; see the error-code table below. Empty Form 7 positions are probed silently and do not generate one error per slot. |
| `D,RTC,OK` | The PCF8523 was found and its time is initialized without a lost-power flag. |
| `D,RTC,INVALID` | The PCF8523 responded but its time cannot be trusted; saves use the zero timestamp. |
| `D,RTC,FAIL` | The PCF8523 did not respond; saves use the zero timestamp. |
| `D,SET,LOAD,ACTIVE` | Form 4 copied the currently active Run preset into the setup draft. |
| `D,SET,LOAD,1` | Form 4 used factory preset 1 because no Run preset had been loaded yet. |
| `D,SET,SELECT` | A validated Form 7 preset was loaded into the Form 4 draft. |
| `D,SET,SELECT,BACK` | Form 7 Back restored the unsaved Form 4 draft without selecting a preset. |
| `D,SET,NAME,OPEN` | Form 6 opened with the current draft name in ILabelB64. |
| `D,SET,NAME,SET` | The valid temporary name was applied to the RAM draft. |
| `D,SET,NAME,CANCEL` | Form 6 Back returned to Form 4 without changing the draft name. |
| `D,SET,NAME,INVALID` | Set was ignored because the name was empty or had a leading/trailing space. |
| `D,SET,NAME,READONLY` | Userbutton52 was ignored because a factory preset was displayed. |
| `D,SET,READY` | The staged Form 4 labels, time/cycle mode and field images, and channel buttons finished synchronizing. |
| `D,SET,MODE,CYCLES` / `MODE,TIME` | ISwitch8 selected cycle or HH:MM time entry for B67/B68. Time mode starts at `00:00` and converts entered time to cycles, rounding down. |
| `D,SET,TIME,G1,HOUR` / `TIME,G1,MIN` | Userbutton115 selected the hour or minute field for group 1. |
| `D,SET,TIME,G2,HOUR` / `TIME,G2,MIN` | Userbutton116 selected the hour or minute field for group 2. |
| `D,SET,NEW` / `NEW,FULL` | A new lowest-vacant-ID draft was created, or all user IDs are occupied. |
| `D,SET,SAVE,OK` / `SAVE,FAIL` | Form 4 save completed or failed. On success, Userbutton55 shows image 1 for 3 seconds, then returns to image 0; Save presses during that interval are ignored. |
| `D,SET,DELETE,OK` / `DELETE,FAIL` | Form 4 deletion completed or failed. |
| `D,SET,LOCKED` | Setup navigation was rejected because a test is active. |
| `D,SET,EDIT,READONLY` | An edit was attempted while a factory preset was displayed. |
| `D,SET,SAVE,READONLY` / `DELETE,READONLY` | Save or delete was attempted for a factory preset. |
| `D,LOG,ARM,1` / `ARM,0` | ISwitch6 armed or disarmed logging while no test was active. |
| `D,LOG,OPEN` | A new CSV file and its metadata header were created. |
| `D,LOG,ON` | Logging started or resumed for the current test. |
| `D,LOG,OFF` | A `LOGGING_OFF` event was flushed and the current file was closed. |
| `D,LOG,END` | The final END record was flushed and the log closed successfully. |
| `D,LOG,ABORT` | Logging was abandoned while ending a test or handling an already-fatal non-logging error. |
| `D,LOG,DISABLED` | Logging was disabled after an SD failure during an otherwise continuing test. |
| `D,LOG,ERR,<code>` | A data-log operation failed; see the logging error table below. |
| `D,SW,LOAD,1,SD` | Square-wave preset 1 was committed from the SD file. |
| `D,SW,LOAD,<id>,FLASH` | The matching compiled factory preset fallback was loaded. |
| `D,SW,CACHE,<id>` | Form 2 reused the active preset from RAM without reading the SD card. |
| `D,SW,LIST,READY` | All 18 cached Form 7 preset names were sent to ILabelB46-ILabelB63. |
| `D,SW,SELECT,LOCKED` | Preset selection was requested during an active procedure, or Form 7 was reached through another route. The active procedure was preserved and Form 7 was not allowed to remain open. |
| `D,SW,SELECT,SETUP` | A Form 7 selection was routed back to Form 4 rather than committed as the active run preset. |
| `D,SW,SELECT,BACK` | Form 7 Back returned to its opening page without changing the selected preset. |
| `D,SW,READY` | Both Form 2 field-refresh passes completed, and UserImages13 was commanded to image 1. |
| `D,SW,START` | The Square-wave procedure started. |
| `D,SW,PAUSE,WAIT` | ISwitch7 was ON, so a cycle-boundary pause was requested. Each unfinished group is still completing its current cycle and UserImages12 remains at running image 1. |
| `D,SW,PAUSE` | The Square-wave procedure is fully paused and UserImages12 uses image 4. With immediate pause, valve phases are preserved; with cycle-boundary pause, both groups are OFF at their boundaries. Regulators remain active. |
| `D,SW,RESUME` | The paused Square-wave procedure resumed. |
| `D,SW,STOP` | The Square-wave procedure stopped and outputs returned to idle. |
| `D,SW,DONE` | Both finite groups completed their configured total cycle counts. |
| `D,SW,WARN,DAC,IGNORED` | A DAC did not acknowledge, but the procedure continued because DAC acknowledgment enforcement is disabled. |
| `D,SW,WARN,LOG,DISABLED` | SD logging failed and was disabled; the Square-wave procedure continues or completes normally. |
| `D,SW,ERR,DAC` | A regulator DAC command failed while starting the procedure. |
| `D,SW,ERR,PRESET` | A preset failed the firmware pressure, frequency, or duty validation before starting. |
| `D,SW,ERR,LOG` | A log finalization also failed while another fatal error was already stopping the procedure. Ordinary logging failures now report WARN and do not stop it. |

All regulator and channel numbers in the action messages are human-readable and start at `1`.

### Preset error codes

| Code | Meaning |
|---|---|
| `SD` | SD initialization was unavailable at boot. |
| `OPEN` | The requested factory file or both user A/B files could not be opened. |
| `LONG` | A CSV line exceeded the fixed 96-byte buffer. |
| `SIG` | The `CELLQ_SW` signature was wrong. |
| `VER` | The file schema version is unsupported. |
| `RANGE` | A numeric value was outside its permitted range. |
| `CHANNEL` | The channel count or ordered channel number was wrong. |
| `CRC` | The calculated CRC32 did not match the file. |
| `FIELD` | A record, timestamp, name, field count, or within-group waveform value was invalid. |
| `DIR` | The `/PRESETS/` directory could not be created for a save. |
| `SAVE` | The target A/B file could not be prepared or fully written. |
| `VERIFY` | The newly written file failed full parse/CRC readback and was removed. |
| `DELETE` | One of the preset's A/B files could not be removed. |

### Data-logging error codes

| Code | Meaning |
|---|---|
| `PATH` | `/DATA`, its date directory, or a unique 8.3 filename could not be prepared. |
| `HEADER` | The new file could not be opened or its metadata header could not be written. |
| `REOPEN` | A mid-test re-enable could not reopen the existing file. |
| `EVENT` | A `LOGGING_ON` event could not be written. |
| `WRITE` | A scheduled 50 Hz sample could not be written. |
| `FLUSH` | The one-second periodic flush failed. |
| `CLOSE` | The `LOGGING_OFF` event or its flush failed. |
| `FINALOPEN` | The closed test file could not be reopened to append END. |
| `FINAL` | The END record or final flush failed. |

## Boot message

```text
D,BOOT,OK
```

This confirms that firmware setup reached completion after valve, regulator, and display initialization.

It does not confirm that the display, valves, sensors, or regulators are physically operating.

## Display event message

```text
D,E,<cmd>,<object>,<index>,<value>
```

Fields:

- `cmd`: numeric ViSi-Genie command reported by the library
- `object`: numeric ViSi-Genie object type
- `index`: Workshop object index
- `value`: value reported by the object

This message shows that the firmware received an event. It does not by itself mean that the active mode accepted or acted on it. Look for a following mode-specific message to confirm the action.

### Current Manual-mode event indices

| LCD object | Index | Function |
|---|---:|---|
| Form | 1 | Synchronize Manual-mode pressure, HH:MM:SS fields, unit images, and valve switches |
| ISwitch | 0-5 | Valve channels 1-6 |
| ISwitch | 9 / 10 | Link channels 1-3 to channel 1 / channels 4-6 to channel 4 |
| Userbutton | 5 / 4 | Regulator 1 pressure minus / plus |
| Userbutton | 6 / 7 | Regulator 2 pressure minus / plus |
| Userbutton | 9-14 | Time minus for channels 1-6 |
| Userbutton | 15-20 | Time plus for channels 1-6 |
| Userbutton | 109-114 | Momentary unit buttons for channels 1-6; each OnChanged release reports value 0 and advances the unit |

The numeric `cmd` and `object` fields come from the Genie library. The Workshop project must use the object types and indices above and must have the necessary Report Events enabled.

UserImages17-22 display the selected unit for channels 1-6: image 0 seconds, image 1 minutes, image 2 hours. These are output objects, not touch-event sources.

Form 0 also reports activation. Moving from Manual Form 1 to Form 0 hides the Manual controls but deliberately preserves its current outputs and valve timers. Operational events received while Form 1 is hidden are logged as `D,E,...` but are ignored by Manual mode.

### Current Square-wave event indices

| LCD object | Index | Function |
|---|---:|---|
| Form | 0 | Home page navigation report |
| Form | 2 | Load factory preset 1 when idle, or synchronize the current Square-wave state |
| Form | 3 | Square-wave monitor page navigation report |
| Userbutton | 24 | Request setup Form 4 while the procedure is stopped |
| Userbutton | 27 | Start or stop the procedure |
| Userbutton | 28 | Pause or continue the procedure |
| Userbutton | 30 | Request preset-selection Form 7 while stopped |
| ISwitch | 6 | Arm logging, start it during a test, or stop it without stopping the test |

### Current monitor event indices

| LCD object | Index | Function |
|---|---:|---|
| Form | 3 | Activate channel-pressure monitoring |
| ILabelB | 8-13 | Pressure values for channels 1-6; firmware output only |
| UserImages | 0-5 | Not-running, active, or suppressed channel state; firmware output only |
| Scope | 1-2 | Scope1 traces 1-3 show channels 1-3; Scope2 traces 1-3 show channels 4-6; firmware output only |
| UserImages | 14 | Form 3 loading image 0, ready image 1, running/paused image 2; firmware output only |
| Userbutton | 26 | Request setup Form 4 while the procedure is stopped |

Any reported activation of another form disables monitor sampling and writes.

### Current setup Form 4 event indices

| LCD object | Index | Function |
|---|---:|---|
| Form | 4 | Load factory preset 1 into the setup draft |
| Userbutton | 36-39 / 44-47 | Group 1 pressure, frequency, duty, and delay decrease/increase |
| Userbutton | 43,42,41,40 / 48-51 | Group 2 pressure, frequency, duty, and delay decrease/increase |
| ILabelB | 67 / 68 | Group 1 / group 2 configured total cycles; firmware output only |
| Userbutton | 29 / 106 | Group 1 total cycles decrease/increase by 10 |
| Userbutton | 107 / 108 | Group 2 total cycles decrease/increase by 10 |
| ISwitch | 8 | ON: cycle entry; OFF: HH:MM time entry in 5-minute steps |
| UserImages | 15 / 16 | Group 1 / group 2 entry-mode image: 0 cycles, 1 time |
| Userbutton | 52 | Open Form 6 to edit an existing user-preset draft name |
| Userbutton | 53 | Open the shared Form 7 selector; Back restores the current unsaved draft |
| Userbutton | 54 | Create a new draft using the lowest vacant ID |
| Userbutton | 55 | Save the current user-preset draft; image 1 confirms success for 3 seconds |
| Userbutton | 56 | Delete the current user preset |
| Userbutton | 57-62 | Toggle channels 1-6 between enabled image 0 and suppressed image 1 |

Factory presets are read-only. Press New to create an editable user draft, or use Userbutton53 to select an existing user preset for editing. Userbutton52 edits that draft's name through Form 6.

### Current name-entry Form 6 event indices

| LCD object | Index | Function |
|---|---:|---|
| Form | 6 | Show the current draft name in ILabelB64 |
| Keyboard | 0 | Report the pressed character; key value 8 is Backspace |
| Userbutton | 32 | Validate the temporary name, apply it to the RAM draft, and return to Form 4 |
| ILabelB | 64 | Current temporary name; firmware output only |

The Form 6 Back button directly activates Form 4 and cancels the temporary name. A name is written to SD only when Userbutton55 subsequently saves the full preset.

## Queue overflow message

```text
D,E,DROP
```

The display generated events faster than the main loop could process them. The event that caused this message was discarded and its requested action did not occur.

If this happens during normal use, check for buttons configured to report repeatedly or on both press and release, and avoid generating a rapid burst of display events.

## Pressure-setting message

```text
D,P,<regulator>,<tenths>
```

Example:

```text
D,P,1,-790
```

This means regulator 1's Manual-mode setting changed to `-79.0 kPa`. The value is reported in tenths of a kPa:

- `0` means `0.0 kPa`
- `-10` means `-1.0 kPa`
- `-800` means `-80.0 kPa`

The Manual-mode range is `-80.0` to `0.0 kPa` in `1.0 kPa` steps. Each accepted change updates the stored setting and LCD label and sends the corresponding command to the regulator DAC.

## DAC-command message

```text
D,DAC,<regulator>,<code>,<result>
```

Example:

```text
D,DAC,1,3945,1
```

Fields:

- `regulator`: regulator 1 or 2
- `code`: 12-bit MCP4725 command from `0` to `4095`
- `result`: `1` if the I2C transmission was acknowledged, or `0` if it failed

The pressure magnitude maps linearly from `0-82 kPa` to DAC code `0-4095`. For example, `-80.0 kPa` produces code `3995`. Commands update the DAC register only and do not write its EEPROM.

An acknowledged I2C transmission confirms communication with a device at the configured address. It does not independently confirm the resulting analog voltage or pneumatic pressure.

## Monitor-pressure message

```text
D,MON,P,<channel>,<tenths>
```

Example:

```text
D,MON,P,2,-21
```

This means channel 2's newest qualified reading for the current valve phase was `-2.1 kPa` and was sent to ILabelB9. A valve output change invalidates the previous qualified reading, so this value is not reused from an earlier cycle. Messages occur only when a changed label is sent, at less than 4 updates per second per channel; unchanged readings are suppressed. Pressure labels, scope samples, and these messages are produced only while Form 3 is visible and the procedure is Running or PausePending. They stop while fully Paused and after the procedure becomes Idle, Done, or Error. The underlying 100 Hz acquisition still runs for the active test, independently of Form 3.

All channels are Honeywell 015PDAA5 sensors and use the same 0.5-4.5 V, +/-15 psi slope with the default 5 V ADC reference. In physical-sensor mode, firmware averages 64 readings for each channel at boot and stores that average as its per-boot 0 kPa reference. The resulting values are measurements only; they are not currently used for feedback control or a safety interlock.

Startup zeroing reports one line per channel, for example:

```text
D,MON,Z,1,512
```

This means channel 1's averaged startup reading was ADC count 512. These lines are absent when virtual sensors are enabled. Any actual differential pressure present during startup is intentionally included in the zero correction.

When virtual sensors are enabled, an active preset channel reports its assigned preset pressure while its valve output is ON and `0.0 kPa` while OFF. Suppressed channels report `0.0 kPa`. The source message printed when Form 3 pressure streaming starts confirms which path is active.

## Time-setting message

```text
D,T,<channel>,<seconds>
```

Example:

```text
D,T,3,8
```

This means the configured Manual-mode time for valve channel 3 changed to a combined duration of 8 seconds. For example, setting 01:02:03 reports 3723 seconds. The raw `D,E` event identifies which plus/minus button was pressed.

Each accepted unit-button press also reports `D,M,UNIT,<channel>,<unit>`. The unit cycles 0 to 1 to 2 to 0; a linked follower's unit-button press is ignored.

When ISwitch9 or ISwitch10 is ON, changing the group's first channel also copies its time to the two followers. Their time buttons are ignored. Copying a changed follower time can produce `D,T` for that follower.

A value of `0` means fully manual operation: the valve remains in the requested switch state until another switch command. For a nonzero value, the duration is captured when the valve is switched on. Editing the displayed time while that valve is already on does not change the active duration.

## Valve-command message

```text
D,V,<channel>,<state>
```

Examples:

```text
D,V,2,1
D,V,2,0
```

- State `1`: the firmware commanded the valve output ON (`HIGH`)
- State `0`: the firmware commanded the valve output OFF (`LOW`)

This confirms the software output command. It does not prove the electrical output, valve movement, airflow, or pneumatic fail state.

When a group link is ON, switching channel 1 or 4 produces `D,V` for all three channels in that group; their display switches follow. A follower can still be switched independently. Turning a link ON mid-run does not command valves; the next master switch action controls the whole group. A timed master expiry also turns all three OFF.

## Valve-timeout message

```text
D,TO,<channel>
```

Example timeout sequence:

```text
D,V,2,0
D,TO,2
```

The firmware commands the physical valve output OFF before sending the LCD switch-OFF update and the timeout message. Therefore, the `D,V,<channel>,0` message normally appears immediately before `D,TO,<channel>`.

## Manual-form synchronization message

```text
D,M,SYNC
```

This means the firmware processed the report event for Manual Form 1 and queued the current values for these LCD controls:

- Regulator 1 and regulator 2 pressure labels
- Valve channel 1-6 hour, minute, and second labels, UserImages17-22 unit images, and valve switches
- ISwitch9 and ISwitch10 group-link states

LCD label writes contain only numeric values. Time fields are zero-padded to at least two digits. The `kPa` and time units are static text in the Workshop GUI and are not sent by the firmware.

The stored Manual defaults are:

- Pressure: `0.0 kPa` for both regulators
- Valve time: `00:00:00` for all six channels, with seconds selected
- Group links: OFF for both groups

This message confirms that the write functions were called. Visual confirmation on the LCD still depends on the correct Workshop label indices, display baud rate, serial wiring, and Report Event configuration.

## Typical sequences

### Firmware startup

```text
D,DAC,1,0,<result>
D,DAC,2,0,<result>
D,BOOT,OK
```

Each DAC is commanded to the zero/safe code at startup. Manual pressure defaults are applied only after Manual Form 1 becomes active. Each `<result>` is `1` for an acknowledged I2C write or `0` for a failed write.

### Opening Manual Form 1

```text
D,E,<cmd>,<form-object>,1,<value>
D,DAC,1,0,<result>
D,DAC,2,0,<result>
D,M,SYNC
```

### Opening Square-wave Form 2

```text
D,E,<cmd>,<form-object>,2,<value>
D,PR,LOAD,1,1
D,SW,LOAD,1,SD
D,SW,READY
```

This is the successful SD sequence for revision 1. UserImages13 is first set to image 0. Preset labels and images are written in two staged passes, including total cycles in B65 and B66. `D,SW,READY` appears after the ready image has been commanded twice. This indicates that the page data was sent; it does not verify physical hardware or every display acknowledgement.

If the SD file is unavailable or invalid, `D,PR,ERR,1,<code>` and `D,SW,LOAD,1,FLASH` appear instead. The page still loads the compiled preset and becomes ready.

### Data logging

Turning ISwitch6 ON while idle arms the next test:

```text
D,LOG,ARM,1
```

When the test starts, or when ISwitch6 is turned ON during an active test, a new file is created if necessary and logging begins:

```text
D,LOG,OPEN
D,LOG,ON
```

Turning ISwitch6 OFF during the test writes and flushes a `LOGGING_OFF` event. The test continues:

```text
D,LOG,OFF
```

Turning it ON again appends to the same file and reports `D,LOG,ON` without another `OPEN`. A normal Done, Stop, or non-logging Error appends the END record and reports:

```text
D,LOG,END
```

Samples are intentionally not printed to USB serial. The logger aims for 50 Hz from the shared 100 Hz acquisition stream; delayed or stale samples are skipped and counted. If an SD write fails during a test, logging is disabled but the procedure continues:

```text
D,LOG,ERR,WRITE
D,LOG,DISABLED
D,SW,WARN,LOG,DISABLED
```

The logging switch is turned OFF in firmware (and on Form 2 if visible). The waveform and regulators continue according to the preset. The incomplete CSV may remain on the card.

### Opening Form 7 and selecting a preset

On the first idle Form 7 activation, firmware silently scans IDs 1-18 and caches each valid name. Missing or invalid entries are shown as `---`. After the 18 staged label writes complete:

```text
D,SW,LIST,READY
```

Later Form 7 visits reuse the RAM list and produce the same ready message without rescanning every SD file. Selecting a valid SD preset produces its normal validation record, activates Form 2, and performs a complete page synchronization:

```text
D,PR,LOAD,<id>,<revision>
D,SW,READY
```

Selecting either factory preset may instead produce `D,SW,LOAD,<id>,FLASH` when its compiled fallback is used. An unavailable `---` position is ignored.

Userbutton87 cancels selection and returns to the page that opened Form 7. From Form 2, it keeps the active run preset and reports `D,SW,SELECT,BACK`. From Form 4, it restores the unsaved setup draft and also reports `D,SET,SELECT,BACK`. The 18 Set buttons are the only controls that select a preset.

If Userbutton30 is pressed while a procedure is running, waiting for a boundary pause, or paused, firmware remains on Form 2 and reports:

```text
D,SW,SELECT,LOCKED
```

The active preset, waveform phases, valves, and regulator states remain unchanged. If Form 7 is reached through another route during an active procedure, the same message is emitted and firmware activates Form 2 again.

### Navigating while a Square-wave procedure is active

A running or paused procedure continues when the display reports Form 0, Form 1, or Form 3. These navigation events still produce their normal raw `D,E,...` record, but they do not produce `D,SW,STOP`, idle DAC commands, or Manual synchronization. Operational display events are ignored while the Square-wave procedure owns the hardware.

Returning to Form 2 reuses the preset cached in RAM and produces:

```text
D,SW,CACHE,1
```

It does not reopen the SD file. It refreshes all Form 2 fields and control images from RAM in two paced passes, followed by:

```text
D,SW,READY
```

There is no new `D,PR,LOAD,...` or `D,SW,LOAD,...` during this return synchronization, and the procedure timing is not restarted. Form 0 cannot navigate directly to Form 3 in the current GUI; any route from Manual/Home to Form 3 passes through Form 2, which safely deactivates Manual mode before the monitor page is reached.

### Starting and completing factory preset 1

```text
D,DAC,1,99,1
D,DAC,2,99,1
D,SW,START
D,DAC,1,0,1
D,DAC,2,0,1
D,SW,DONE
```

The `99` DAC code corresponds to the preset's `-2.0 kPa` command under the confirmed 0-82 kPa conversion. Cycle progress is intentionally not printed every cycle. ILabelB24 receives only the integer value from `0` through `100`; the firmware does not transmit a percent sign.

During display-only testing without DACs, the start sequence instead includes:

```text
D,DAC,1,99,0
D,DAC,2,99,0
D,SW,WARN,DAC,IGNORED
D,SW,START
```

This warning is expected when `CELLQ_REQUIRE_DAC_ACK=0`. It confirms that the procedure is continuing for GUI testing even though neither DAC acknowledged the commands.

### Pausing and continuing

ISwitch7 defaults ON at firmware startup, so cycle-boundary pausing is the default behavior. Form 2 synchronization writes this state to the display switch.

With ISwitch7 OFF, the procedure pauses immediately:

```text
D,SW,PAUSE
D,SW,RESUME
```

The immediate pause keeps each group's current valve and regulator outputs unchanged. Resume continues from each preserved waveform phase.

With ISwitch7 ON, the request and the completed pause are separate messages:

```text
D,SW,PAUSE,WAIT
D,SW,PAUSE
D,SW,RESUME
```

Between `PAUSE,WAIT` and `PAUSE`, each unfinished group completes its current cycle, turns its valves OFF, and waits. `D,SW,PAUSE` appears only after both groups are at cycle boundaries. Resume begins both next cycles together. Both regulators remain active while paused.

When one group completes all its configured cycles before the other, its regulator is idled independently and produces a zero-code DAC message such as:

```text
D,DAC,1,0,<result>
```

The other group continues until it also completes. The final group completion is followed by both zero-code DAC commands and `D,SW,DONE`.

Square-wave valve edges and finite-cycle valve shutdown run inside the Timer5 interrupt, so they do not emit serial messages from the interrupt. The channel-1 built-in LED mirror is updated directly by the same interrupt. Procedure-level `D,SW,...` messages are emitted later by the main loop.

### Changing a pressure setting

```text
D,E,<cmd>,<userbutton-object>,4,<value>
D,DAC,1,3945,1
D,P,1,-790
```

This example is regulator 1's plus button changing `-80.0 kPa` to `-79.0 kPa`. DAC result `1` means that the I2C write was acknowledged.

### Switching a timed valve on and reaching timeout

```text
D,E,<cmd>,<iswitch-object>,1,1
D,V,2,1
D,V,2,0
D,TO,2
```

This example uses ISwitch index 1, which controls valve channel 2.

## When expected messages do not appear

Check the following:

1. `CELLQ_DEBUG` is set to `1` before compiling and uploading.
2. The serial monitor baud rate is `250000`.
3. The correct USB serial port is selected.
4. The Workshop object has Report Events enabled.
5. The object type and index match the current mapping.
6. `D,E,DROP` is not appearing during a burst of events.

Because messages are non-blocking and best-effort, an individual debug line can occasionally be skipped when the transmit buffer is busy.
