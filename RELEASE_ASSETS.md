# CellQ firmware release assets

The firmware version remains `1.0.0` until the project owner explicitly changes it. For version `X.Y.Z`, create a GitHub Release with tag `vX.Y.Z` and exactly these two downloadable assets:

- `cellq-mega2560-vX.Y.Z.hex` — the PlatformIO `megaatmega2560` Intel HEX firmware image for the Arduino Mega 2560.
- `cellq-mega2560-vX.Y.Z.sha256` — one ASCII/UTF-8 line containing the lowercase SHA-256 of that exact `.hex` file, two spaces, the `.hex` filename, and LF. Example: `<64 lowercase hex digits>  cellq-mega2560-v1.0.0.hex`.

There is **no signature asset** in this release format. A checksum detects corruption or mismatched assets but does not authenticate the publisher by itself. The desktop app must obtain both assets from the expected GitHub repository and release, verify the SHA-256 before flashing, and reject missing, renamed, or inconsistent assets. Do not use a source archive or the latest branch as a firmware image.

## Packaging

From a clean, committed firmware worktree on Windows, run:

```powershell
.\scripts\package_firmware_release.ps1
```

If `pio` is not on `PATH`, pass its executable path with `-PlatformIOPath`. The script reads `FIRMWARE_VERSION_TEXT` from `include/BuildConfig.h`, runs `pio run -e megaatmega2560`, and produces the two assets in the ignored `dist/` directory. It refuses a dirty worktree and will not overwrite existing assets. It does not tag, publish, upload firmware to a board, or modify the source version.

If Windows PowerShell blocks local scripts, run this process-scoped command instead of changing a machine-wide setting:

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\scripts\package_firmware_release.ps1
```

For the current version, the resulting filenames are:

```text
cellq-mega2560-v1.0.0.hex
cellq-mega2560-v1.0.0.sha256
```

After the firmware is ready for release and the two files are checked, create tag `v1.0.0` at the tested commit, create the matching GitHub Release, attach **both** files, and publish it. Do not enable desktop-app firmware updates until that release exists and its board, version, and checksum have been verified. This document defines the release contract; it does not claim that a GitHub Release already exists.
