# Windows GitHub update wrapper

Optional Windows helper. The file the player starts is `openmohaa.exe` (this wrapper). The real engine is `openmohaa_game.exe`.

## Flow

1. Player starts `openmohaa.exe`
2. Wrapper runs `launch_openmohaa.ps1` to check [official GitHub releases](https://github.com/openmoh/openmohaa/releases)
3. Wrapper starts `openmohaa_game.exe` (forwards the original command line)

Enable with CMake `-DBUILD_GITHUB_UPDATE_WRAPPER=ON`. Default Windows builds stay unchanged (`openmohaa.exe` remains the engine).

## Update rules

- Source: `openmoh/openmohaa` latest release, Windows **x86** zip
- SHA256 digest is required (fail-closed)
- 6 hour API cache, 12s API timeout, 90s download timeout
- 400 MiB zip cap
- Staging extract, then replace `.exe`/`.dll` files
- Zip `openmohaa.exe` is installed as `openmohaa_game.exe`
- The wrapper `openmohaa.exe` is never overwritten
- A running `openmohaa_game.exe` is never replaced

`-CheckOnly` prints the latest official tag and local version, then exits.
