# DumpSource2

A C++ Application that offline dumps schema bindings and convars/commands for [GameTracking](https://github.com/SteamTracking/GameTracking) purposes.

[See this file in GameTracking on how its used.](https://github.com/SteamTracking/GameTracking/blob/master/tools/dump_source2.sh)

## Usage

Run DumpSource2 from the rootbin folder of a source 2 game. `game/bin/win64`

`DumpSource2 <output path>`

- `output path` - absolute or relative path to a folder where output should be stored


# Compilation

## Windows

```sh
mkdir build
cd build
cmake ..
# Open VS solution and compile from VS
```

## Linux
```sh
mkdir build
cd build
cmake ..
make
```

## SDK Configuration

Each game target is built with `build_game(GAME, GAME_PATH, SDK)` in [`src/main/CMakeLists.txt`](./src/main/CMakeLists.txt). The third argument selects which HL2SDK from `vendor/` to link against. Games update more often than the SDKs, so it may be necessary to switch to another game's SDK.
