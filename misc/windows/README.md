# Windows GitHub update wrapper

Optional Windows helper. The file the player starts is `openmohaa.exe` (this wrapper). The real engine is `openmohaa_game.exe`.

## Flow

1. Player starts `openmohaa.exe`
2. First run: choose architecture (`Architectuur? [1] x86  [2] x64`). Later runs use the saved choice.
3. Wrapper runs `launch_openmohaa.ps1` to check [official GitHub releases](https://github.com/openmoh/openmohaa/releases) for that architecture only
4. Wrapper starts `openmohaa_game.exe` (forwards the original command line, except launcher switches)

Enable with CMake `-DBUILD_GITHUB_UPDATE_WRAPPER=ON`. Default Windows builds stay unchanged (`openmohaa.exe` remains the engine).

## Architecture

The player must choose **x86** or **x64**. There is no silent default and no fallback to the other zip.

- First run (no saved choice): short prompt `Architectuur? [1] x86  [2] x64`. Press `1` (or Enter) for x86, `2` for x64.
- Saved in `launcher_state`:
  - `%APPDATA%\openmohaa\launcher_state.txt` (usual)
  - or `launcher_state.txt` next to `openmohaa.exe` (portable / fallback)
  - line: `arch=x86` or `arch=x64`
- Change later:
  - `openmohaa.exe -x86` or `openmohaa.exe -x64` (saved)
  - or edit the `arch=` line in the state file
- If that architecture's official zip is missing: short error, then start the existing game if present

`-x86`, `-x64`, and `-CheckOnly` are not forwarded to the engine.

## Update rules

- Source: `openmoh/openmohaa` latest release, the **chosen** Windows zip only
- SHA256 digest is required (fail-closed)
- 6 hour API cache, 12s API timeout, 90s download timeout
- 400 MiB zip cap
- Staging extract, then replace `.exe`/`.dll` files
- Zip `openmohaa.exe` is installed as `openmohaa_game.exe`
- The wrapper `openmohaa.exe` is never overwritten
- A running `openmohaa_game.exe` is never replaced

`-CheckOnly` prints the latest official tag and local version, then exits.
