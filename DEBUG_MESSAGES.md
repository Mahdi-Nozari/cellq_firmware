# CellQ Firmware Debug Messages

This guide describes the USB serial debug messages currently produced by the CellQ firmware.

## Serial monitor setup

- Port: the Arduino Mega USB serial port (`Serial`)
- Baud rate: `250000`
- Line ending: any setting is acceptable because the firmware does not read debug commands
- Debug switch: `CELLQ_DEBUG` at the top of `include/BuildConfig.h`
- DAC acknowledgment policy: `CELLQ_REQUIRE_DAC_ACK` immediately below it
- Monitor sensor source: `CELLQ_VIRTUAL_PRESSURE_SENSORS` in `include/BuildConfig.h`

```cpp
#define CELLQ_DEBUG 1
#define CELLQ_REQUIRE_DAC_ACK 0
#define CELLQ_VIRTUAL_PRESSURE_SENSORS 1
```

Use `1` to enable messages. Use `0` to compile out the debug calls and their text.

For `CELLQ_REQUIRE_DAC_ACK`, use `0` during display-only bench testing so the Square-wave procedure can run without physical DACs. Set it to `1` when the DACs are installed so a missing acknowledgment changes the procedure to ERROR. The firmware attempts the I2C writes in both modes.

Set `CELLQ_VIRTUAL_PRESSURE_SENSORS` to `1` for display-only testing. Set it to `0` when the six physical sensors are connected.

Debug output is best-effort. A message is skipped when there is not enough room in the serial transmit buffer, so debugging cannot block important firmware work.

## Message reference

| Message | Meaning |
|---|---|
| `D,BOOT,OK` | Firmware setup completed. |
| `D,E,<cmd>,<object>,<index>,<value>` | A raw event was received from the display. |
| `D,E,DROP` | The display event queue was full, so an event was discarded. |
| `D,P,<regulator>,<tenths>` | A manual pressure setting changed. |
| `D,DAC,<regulator>,<code>,<result>` | An MCP4725 command was attempted. |
| `D,T,<channel>,<seconds>` | A manual valve time setting changed. |
| `D,V,<channel>,<state>` | A physical valve output command was issued. |
| `D,TO,<channel>` | A timed valve reached its duration and was automatically switched off. |
| `D,M,SYNC` | The current Manual-mode pressure and time values were sent to the LCD labels. |
| `D,MON,P,<channel>,<tenths>` | A Form 3 channel-pressure value was sent to its LCD label. |
| `D,MON,SRC,VIRTUAL` | Form 3 is using generated pressure values and does not read A8-A13. |
| `D,MON,SRC,ANALOG` | Form 3 is sampling the physical A8-A13 inputs. |
| `D,SW,LOAD,1` | Factory Square-wave preset 1 was loaded on Form 2. |
| `D,SW,READY` | All preset fields and the Scope0 preview were sent; UserImages13 was changed to image 1. |
| `D,SW,START` | The Square-wave procedure started. |
| `D,SW,PAUSE` | The Square-wave procedure paused with outputs preserved. |
| `D,SW,RESUME` | The paused Square-wave procedure resumed. |
| `D,SW,STOP` | The Square-wave procedure stopped and outputs returned to idle. |
| `D,SW,DONE` | The configured total cycle count completed. |
| `D,SW,WARN,DAC,IGNORED` | A DAC did not acknowledge, but the procedure continued because DAC acknowledgment enforcement is disabled. |
| `D,SW,ERR,DAC` | A regulator DAC command failed while starting the procedure. |
| `D,SW,ERR,PRESET` | A preset failed the firmware pressure, frequency, or duty validation before starting. |

All regulator and channel numbers in the action messages are human-readable and start at `1`.

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
| Form | 1 | Synchronize Manual-mode pressure and time labels |
| ISwitch | 0-5 | Valve channels 1-6 |
| Userbutton | 5 / 4 | Regulator 1 pressure minus / plus |
| Userbutton | 6 / 7 | Regulator 2 pressure minus / plus |
| Userbutton | 9-14 | Time minus for channels 1-6 |
| Userbutton | 15-20 | Time plus for channels 1-6 |

The numeric `cmd` and `object` fields come from the Genie library. The Workshop project must use the object types and indices above and must have the necessary Report Events enabled.

Form 0 also reports activation. Moving from Manual Form 1 to Form 0 hides the Manual controls but deliberately preserves its current outputs and valve timers. Operational events received while Form 1 is hidden are logged as `D,E,...` but are ignored by Manual mode.

### Current Square-wave event indices

| LCD object | Index | Function |
|---|---:|---|
| Form | 0 | Home page navigation report |
| Form | 2 | Load factory preset 1 when idle, or synchronize the current Square-wave state |
| Form | 3 | Square-wave monitor page navigation report |
| Userbutton | 27 | Start or stop the procedure |
| Userbutton | 28 | Pause or continue the procedure |

### Current monitor event indices

| LCD object | Index | Function |
|---|---:|---|
| Form | 3 | Activate channel-pressure monitoring |
| ILabelB | 8-13 | Pressure values for channels 1-6; firmware output only |
| UserImages | 0-5 | Not-running, active, or suppressed channel state; firmware output only |
| Scope | 1-6 | Real-time pressure traces for channels 1-6; firmware output only |

Any reported activation of another form disables monitor sampling and writes.

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

This means channel 2 measured `-2.1 kPa` and that value was sent to ILabelB9. Messages follow the 2 Hz label rate, not the faster scope-sample rate. They stop when Form 3 is hidden.

All channels currently use the legacy 0.5-4.5 V, +/-15 psi conversion with the default 5 V ADC reference. Separate fixed-point gain and offset entries exist in `BuildConfig.h` for later individual calibration. The values are measurements only; they are not currently used for feedback control or a safety interlock.

When virtual sensors are enabled, an active preset channel reports its assigned preset pressure while its valve output is ON and `0.0 kPa` while OFF. Suppressed channels and inactive tests report `0.0 kPa`. The source message printed on Form 3 activation confirms which path is active.

## Time-setting message

```text
D,T,<channel>,<seconds>
```

Example:

```text
D,T,3,8
```

This means the configured Manual-mode time for valve channel 3 changed to 8 seconds.

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

This means the firmware processed the report event for Manual Form 1 and sent the current values to these LCD labels:

- Regulator 1 and regulator 2 pressure labels
- Valve channel 1-6 time labels

LCD label writes contain only the numeric values. The `kPa` and `s` units are static text in the Workshop GUI and are not sent by the firmware.

The stored Manual defaults are:

- Pressure: `-80.0 kPa` for both regulators
- Valve time: `0 s` for all six channels

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
D,DAC,1,3995,<result>
D,DAC,2,3995,<result>
D,M,SYNC
```

### Opening Square-wave Form 2

```text
D,E,<cmd>,<form-object>,2,<value>
D,SW,LOAD,1
D,SW,READY
```

UserImages13 is first set to image 0. Preset labels and images are then written once in staged updates, followed by the static Scope0 preview. `D,SW,READY` appears and UserImages13 changes to image 1 only after the final preview sample is sent. This indicates that the page data is ready; it does not verify physical hardware.

### Navigating while a Square-wave procedure is active

A running or paused procedure continues when the display reports Form 0, Form 1, or Form 3. These navigation events still produce their normal raw `D,E,...` record, but they do not produce `D,SW,STOP`, idle DAC commands, or Manual synchronization. Operational display events are ignored while the Square-wave procedure owns the hardware.

Returning to Form 2 starts a display-only synchronization of the current procedure state. UserImages13 changes to image 0 during the staged labels and Scope0 preview, followed by:

```text
D,SW,READY
```

There is no new `D,SW,LOAD,1` during this return synchronization, and the procedure timing is not restarted. Form 0 cannot navigate directly to Form 3 in the current GUI; any route from Manual/Home to Form 3 passes through Form 2, which safely deactivates Manual mode before the monitor page is reached.

### Starting and completing factory preset 1

```text
D,DAC,1,99,1
D,DAC,2,99,1
D,SW,START
D,DAC,1,0,1
D,DAC,2,0,1
D,SW,DONE
```

The `99` DAC code corresponds to the preset's `-2.0 kPa` command under the confirmed 0-82 kPa conversion. Cycle progress is intentionally not printed every cycle.

During display-only testing without DACs, the start sequence instead includes:

```text
D,DAC,1,99,0
D,DAC,2,99,0
D,SW,WARN,DAC,IGNORED
D,SW,START
```

This warning is expected when `CELLQ_REQUIRE_DAC_ACK=0`. It confirms that the procedure is continuing for GUI testing even though neither DAC acknowledged the commands.

### Pausing and continuing

```text
D,SW,PAUSE
D,SW,RESUME
```

Pause keeps the current valve and regulator outputs unchanged. Resume continues from the preserved elapsed time and waveform phase.

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
