# CellQ Firmware Debug Messages

This guide describes the USB serial debug messages currently produced by the CellQ firmware.

## Serial monitor setup

- Port: the Arduino Mega USB serial port (`Serial`)
- Baud rate: `250000`
- Line ending: any setting is acceptable because the firmware does not read debug commands
- Debug switch: `CELLQ_DEBUG` at the top of `include/BuildConfig.h`

```cpp
#define CELLQ_DEBUG 1
```

Use `1` to enable messages. Use `0` to compile out the debug calls and their text.

Debug output is best-effort. A message is skipped when there is not enough room in the serial transmit buffer, so debugging cannot block important firmware work.

## Message reference

| Message | Meaning |
|---|---|
| `D,BOOT,OK` | Firmware setup completed. |
| `D,E,<cmd>,<object>,<index>,<value>` | A raw event was received from the display. |
| `D,E,DROP` | The display event queue was full, so an event was discarded. |
| `D,P,<regulator>,<tenths>` | A manual pressure setting changed. |
| `D,T,<channel>,<seconds>` | A manual valve time setting changed. |
| `D,V,<channel>,<state>` | A physical valve output command was issued. |
| `D,TO,<channel>` | A timed valve reached its duration and was automatically switched off. |
| `D,M,SYNC` | The current Manual-mode pressure and time values were sent to the LCD labels. |

All regulator and channel numbers in the action messages are human-readable and start at `1`.

## Boot message

```text
D,BOOT,OK
```

This confirms that firmware setup reached completion after valve initialization and display startup.

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

This message shows that the firmware received an event. It does not by itself mean that Manual mode accepted or acted on it. Look for a following `D,P`, `D,T`, `D,V`, or `D,M,SYNC` message to confirm the action.

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
D,P,1,12
```

This means regulator 1's Manual-mode setting changed to `1.2 kPa`. The value is reported in tenths of a kPa:

- `0` means `0.0 kPa`
- `10` means `1.0 kPa`
- `50` means `5.0 kPa`

The current firmware updates the stored setting and LCD label only. It does not yet send this setting to either MCP4725 DAC or physically control regulator pressure.

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

At boot, the current defaults are:

- Pressure: `1.0 kPa` for both regulators
- Valve time: `0 s` for all six channels

This message confirms that the write functions were called. Visual confirmation on the LCD still depends on the correct Workshop label indices, display baud rate, serial wiring, and Report Event configuration.

## Typical sequences

### Firmware startup

```text
D,BOOT,OK
```

### Opening Manual Form 1

```text
D,E,<cmd>,<form-object>,1,<value>
D,M,SYNC
```

### Changing a pressure setting

```text
D,E,<cmd>,<userbutton-object>,4,<value>
D,P,1,11
```

This example is regulator 1's plus button changing `1.0 kPa` to `1.1 kPa`.

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
