# AGENTS.md

Usage, output files and building are in the [README](README.md).

DumpSource2 loads a Source 2 game's modules without running the game and dumps what they register: schemas, convars and commands, entity classes, interfaces, logging channels, module metadata and the network database. [GameTracking](https://github.com/SteamTracking/GameTracking) commits the output on every game update and [SchemaExplorer](https://github.com/ValveResourceFormat/SchemaExplorer) shows `schemas.json`, so output changes show up in both.

## Layout

- `src/main/gamedata.h` holds everything that changes with game updates: signatures, struct offsets the SDK doesn't have, and lists of special cases. Each signature's comment says how to find it again, by an engine string or nearby code, never by address.
- `src/main/dumpers/<name>` has one dumper each, most writing a text file for GameTracking and part of `schemas.json`.
- `src/main/utils` comes from another project; leave it as is, except for stubs the SDK needs to link.
- `vendor/hl2sdk-*` are the upstream [alliedmodders/hl2sdk](https://github.com/alliedmodders/hl2sdk) branches. `build_game` in `src/main/CMakeLists.txt` picks one per game target, not necessarily the game's own.
- Only current game builds are supported: older ones fail loudly instead of getting compatibility code. A one-off dump of an older engine lives on its own branch with a forked SDK.

## Reading game memory

- Keep code that touches the game minimal: the fewest structs, offsets and signatures that give the data, preferring what the game exports or registers over walking its internals, and calling the game's own function that does the work, like running its registrations, instead of copying its loop. Any of these can change with an update, so less means less to fix.
- Find things by signature or export, never by hand-written scans of code or data. Keep signatures as short as still match once; when a pattern repeats, pick the match by something checkable, like the name the code loads, and fail unless exactly one fits. Resolve RIP-relative operands with `Modules::GetGlobalFromSignatureMatch`, not inline pointer arithmetic.
- Most data is filled by static initializers on module load: convar queues, entity class lists, datamaps and schema bindings. App systems are listed by hand in `g_AppSystems` and connected without being initialized, since connecting every exported interface hangs or crashes. Interfaces that aren't app systems go in `g_FactoryInterfaces`, since connecting them calls whatever is in their first vtable slot.
- Outdated SDK structs still compile and run, reading garbage. Validate every read (valid names, counts in range, bools 0 or 1, pointers into a module) and fail with `spdlog::critical` naming what to update. A failed dump exits with 1 without writing `schemas.json`, so GameTracking keeps the previous one.
- Stay loud: don't catch crashes or exceptions to keep a dump going. List a class whose constructor crashes in `gamedata.h` instead.
- `Modules::IsValidName` only accepts strings inside a module, so strings the game copied into a `CUtlString` need another check.
- Free game-allocated memory with tier0's allocator (`memoverride.cpp`, `g_pMemAlloc`). The C runtime's `free`, like an SDK `Purge` that bypasses tier0, corrupts the heap.
- Class defaults (`MGetKV3ClassDefaults`) run the game's constructors on a zeroed stack, so uninitialized members read as zero on every run. Keys with random values are listed in `gamedata.h`.
- Linux differs: `IsInModule` only checks for null, module metadata isn't dumped, and pointers to member functions are 16 bytes, moving struct offsets after them.

## The SDK

- Use the SDK's types and names, and don't edit its submodules. For a wrong or incomplete struct, read the member at an offset in `gamedata.h`, commented with the struct and member and validated like other reads. A local copy of a struct the SDK lacks uses the SDK's names, so it can be dropped once the SDK has it.
- Mark every workaround for a missing or wrong SDK part with a `TODO:` saying what would make it unnecessary: offsets, local struct copies, raw flag values and features compiled out for a game (`#ifdef GAME_*`).
- Give SDK fixes to its maintainers as a patch instead, against the latest upstream commit (update the submodule first, so the patch only has what's still missing), without explanatory comments and keeping each file's line endings. Check that it applies, builds on Windows and Linux, and leaves the output unchanged.
- After a submodule update, go through the `TODO:`s: remove workarounds the SDK now covers and re-enable features for games whose SDK now has them. Then dump every game and diff against the previous build.

## Verifying against the game

- Take offsets, flags and enum values from the binaries by decompiling the code that reads or writes them, like copy constructors, override handlers and debug commands that print them, found through engine strings (asserts, log messages, convar and RTTI names) rather than addresses. Comment any value with no evidence in the binary. Numbering can differ between engine branches, so check each game's binary.
- An offset that reads the same for everything, like all zeros, isn't proven; find a case that sets it, in the data or the code that writes it.
- Data that looks wrong is often the engine's real layout or Valve's own data; check the binary before changing the dumper.
- Work on copies of the game's binaries, so tools that write next to them leave the install alone. Keep research notes out of the repo, as they go stale; what the code relies on goes in short comments.

## Testing a change

- Run every affected game target from its `game/bin/win64` before and after the change, into separate folders, and diff: only the intended lines should change. Compare `schemas.json` with `jq -S`, removing or mapping back new keys to check nothing else moved.
- For new output, dump each game several times and check the runs, `schemas.json` included, are identical.
- Linux must always build. Run it too, from `game/bin/linuxsteamrt64` with `LD_LIBRARY_PATH=.`, for changes to memory layouts or signatures; the output should match Windows apart from data a platform lacks.
- Some inputs, like files GameTracking extracts from VPKs, only exist in its checkout, so a dump from a plain install can lack data.
- When changing a format, check nothing is lost, for example by parsing the new one back and comparing it with the old.

## Output conventions

- Sort everything written, so the output doesn't depend on load or registration order.
- Leave out what can be rebuilt exactly, like defaults of keys that are always there and values another key gives, but not a value whose absence would mean something else. Write data the code doesn't understand as is (unknown flag bits by number, unhandled metadata keys) and log it with a sample, so new data from an update shows up instead of being dropped.
- Use the engine's names. Where it has none, pick a plain one and say so in a comment.
- `schemas.json` and the text files needn't match: explorer-only data can go in the JSON alone, which uses real JSON types rather than strings. Floats go through `format_float.h`. References to a class, enum or field carry its module, and fields are written as `Class::field`.
- SchemaExplorer reads only the latest format, without fallbacks, so describe each new, changed or removed `schemas.json` key's shape and meaning well enough to update it. Don't change SchemaExplorer from here; fix data problems in the dumper, not with workarounds there.
- Names written to dumps go to `.stringsignore` so GameTracking removes them from its strings dumps.
- Output folders may or may not be case-sensitive, so names differing only in case can be one file or two; code that renames or removes old output files has to handle both.

## Code and commits

- Match the surrounding code (`.clang-format`, `.editorconfig`, tabs) and keep diffs minimal. Tables like flag and type lists have one entry per line, in value order.
- Comments say why, or what the game does, in plain sentences that stay true as the code changes: no change history and no description of the assembly.
- Logs are one info line per stage, with detail at debug or trace. Errors say what to update, like which `gamedata.h` entry.
- One commit per concern: a single plain line saying what changed, with no body and no conventional-commit prefix. Don't commit plans or scratch files.
