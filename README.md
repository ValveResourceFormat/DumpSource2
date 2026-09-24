# DumpSource2

A C++ Application that offline dumps schema bindings, convars/commands, entities, interfaces, logging channels and module metadata for [GameTracking](https://github.com/SteamTracking/GameTracking) purposes.

[See this file in GameTracking on how its used.](https://github.com/SteamTracking/GameTracking/blob/master/tools/dump_source2.sh)

## Usage

Each game has its own build: `DumpSource2-CS2`, `DumpSource2-DOTA` and `DumpSource2-DEADLOCK`. Run it from the game's `game/bin/win64` folder (`game/bin/linuxsteamrt64` on Linux).

`DumpSource2-CS2 <output path>`

- `output path` - absolute or relative path to an existing folder where output should be stored

Set the `LOGLEVEL` environment variable (like `LOGLEVEL=debug`) for more logging.

## Output

- `schemas/` - schema classes and enums as headers, per module
- `schemas.json` - schemas, convars, commands and entities for [SchemaExplorer](https://github.com/ValveResourceFormat/SchemaExplorer)
- `convars.txt`, `commands.txt` - convars and commands with their flags and help
- `entities/` - entity classes as FGD, per module (CS2 only)
- `interfaces.txt` - interfaces exposed by each module
- `logging_channels.txt` - logging channels with their defaults
- `module_metadata/` - metadata of each module as KV3
- `.stringsignore` - names that GameTracking removes from its strings dumps

If anything fails to dump, it exits with code 1 and `schemas.json` is not written.

For CS2, convars and commands that workshop maps can use are flagged when `csgo/pak01_dir/scripts/workshop_cvar_whitelist.txt` is extracted from the game's VPK.


# Compilation

## Windows

```sh
cmake -B build
cmake --build build --config Release
```

Or open the folder in Visual Studio.

## Linux
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## SDK Configuration

Each game target is built with `build_game(GAME, GAME_PATH, SDK)` in [`src/main/CMakeLists.txt`](./src/main/CMakeLists.txt). The third argument selects which HL2SDK from `vendor/` to link against. Games update more often than the SDKs, so it may be necessary to switch to another game's SDK.
