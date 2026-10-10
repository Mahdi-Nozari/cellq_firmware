# CellQ USB protocol v1

The Arduino Mega USB serial port uses 250000 baud, 8-N-1. The PC application owns the port and must open it with DTR and RTS disabled for ordinary debug and log transfer. Merely opening the port, receiving `D,...` debug lines, or sending `CQ1,HELLO` never opens Form 8. A firmware uploader deliberately resets the board through the bootloader; a hardware reset cannot be vetoed by firmware.

Commands are ASCII lines terminated by LF (`CRLF` is accepted). Responses are ASCII LF lines except the binary data frames described below. Send one request at a time and wait for its response. The port may also carry independent `D,...` debug lines; the PC application must demultiplex these from `CQ1,...` responses and binary frames. Command names and hexadecimal CRC values are uppercase. Each command line is at most 79 bytes including the terminating LF.

| Command | Response | Purpose |
|---|---|---|
| `CQ1,HELLO` | `CQ1,HELLO,<firmware-version>,IDLE\|MANUAL\|TEST\|MAINT` | Identify the device without changing its screen or outputs. |
| `CQ1,ENTER` | `CQ1,OK,ENTER` | Enter Form 8. Rejected if a square-wave procedure is running/paused or any Manual valve is ON. Manual regulator outputs are idled on accepted entry. |
| `CQ1,LIST,<directory>` | `CQ1,LIST,BEGIN`, zero or more `CQ1,ITEM,<name>,D\|F,<size>`, `CQ1,LIST,END` | List `/DATA`, a dated `/DATA/YYYYMMDD` folder, or `/DATA/NO_RTC`. |
| `CQ1,GET,<path>,<offset>` | `CQ1,GET,<file-size>,<offset>` | Open a log at a byte offset, including 0. |
| `CQ1,NEXT` | One binary data frame, or `CQ1,EOF,<file-size>,<stream-crc32>` | Request the next chunk of an open GET. Repeat until EOF. |
| `CQ1,HASH,<path>` | `CQ1,OK,HASHING`, then `CQ1,HASH,<size>,<crc32>` | Compute a whole-file CRC32, including when a GET was resumed. |
| `CQ1,DELETE,<path>,<size>,<crc32>` | `CQ1,OK,CHECKING`, then `CQ1,OK,DELETE` or an error | Delete one log only if its current size and CRC32 match. The app must confirm the user's intent and verify its local copy first. |
| `CQ1,CANCEL` | `CQ1,OK,CANCEL` | Close the current list/transfer/check, remaining on Form 8. |
| `CQ1,FWPREP` | `CQ1,OK,FWPREP` | Show `Updating firmware` on Form 8 and block further SD commands. This does **not** flash the Mega. The PC closes the port and invokes the bootloader uploader. |
| `CQ1,EXIT` | `CQ1,OK,EXIT` | End maintenance and activate Form 0. The app sends this after the full log-transfer workflow, not after every file. It can also recover from a failed FWPREP. |

`LIST`, `GET`, `HASH`, `DELETE`, `NEXT`, `CANCEL`, `FWPREP`, and `EXIT` require maintenance mode. Only generated log paths under `/DATA/YYYYMMDD/` or `/DATA/NO_RTC/` can be read, hashed, or deleted; `/PRESETS` is inaccessible. `DELETE` never removes directories. The firmware does not automatically leave maintenance on a PC disconnect; the PC can reconnect and send `EXIT` or the user can restart the device. Incomplete GET transfers leave SD files unchanged.

`ENTER` and `FWPREP` reply only after their Form 8 display commands have drained from the ACK-paced display queue. An absent display may cause those commands to time out and be dropped; the host response confirms the firmware state, not physical visibility of the screen.

One data frame is 10-58 bytes:

| Bytes | Meaning |
|---|---|
| 0-1 | `A5 5A` marker |
| 2 | type `01` (file data) |
| 3 | payload length, 1-48 |
| 4-7 | absolute file offset, unsigned little-endian |
| 8 through `8+length-1` | unmodified CSV bytes |
| last 2 | CRC16-CCITT-FALSE, little-endian, over marker through payload |

CRC16 uses polynomial `0x1021`, initial value `0xFFFF`, no reflection or final XOR. CRC32 uses the standard reflected polynomial `0xEDB88320`, initial `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. The `EOF` CRC32 covers only bytes transmitted by that GET (from its requested offset). The application checks each frame's CRC16 and expected offset, checks the final byte count and stream CRC32, and can use `HASH` to verify the complete file before requesting `DELETE`. On a CRC error or timeout, send `CANCEL` and restart `GET` at the last verified offset.

Status text on ILabelB82 is `Ready`, `Listing logs`, `Transferring logs`, `Transfer complete`, `Checking log`, `Transfer error`, or `Updating firmware`. The display is only a maintenance indicator; actual bootloader upload progress belongs in the Windows app because the Mega resets during upload. Form 8 has no test controls, and firmware ignores touch reports while in maintenance. A debug-console connection alone must never send `ENTER`.
