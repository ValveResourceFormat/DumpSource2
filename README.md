# DumpSource2

A C++ Application that offline dumps schema bindings and convars/commands for [GameTracking](https://github.com/SteamDatabase/GameTracking) purposes.

## Usage

Run DumpSource2 from the rootbin folder of a source 2 game. `game/bin/win64`

`DumpSource2 <mod name> <output path>`

- `mod name` - name of the mod folder (csgo, dota2, citadel)
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