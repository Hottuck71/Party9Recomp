# What's different about Wii Party

PartyRecomp is ResortRecomp's launcher and runtime, moved to Wii Party (USA, SUPE01). This page is what had to
change, and where it lives.

## REL modules

Wii Party keeps most of its code in 115 LZ11-compressed REL modules (`files/rel/*.rel.lz`, 106 different ones):
one per minigame, party mode, board and menu, loaded and unloaded as the game goes. Each imports only from itself
and from `main.dol`.

- **Slots.** Each module is translated for a fixed address in 0x81800000 to 0x82000000, above the 24 MB the game
  is told it has and within `main.dol`'s branch reach. Modules of the same kind share a slot (all minigames one,
  all party modes another): `kSlots` in `native/launcher/builder.cpp`, `scripts/rel_build.py`. Emulated MEM1 is
  32 MB to hold them.
- **The linker.** The SDK's `Link` (0x8013CB10) and `OSUnlink` (0x8013D050) are replaced by `native/src/rel.c`:
  it copies the module into its slot, applies its relocations there, rewrites its header and section table to
  match, and keeps the SDK's module list.
- **Dispatch.** Module functions are renamed `rel<id>_<address>` and each module gets a lookup; the CPU loop asks
  `rel_find` for any address in the slot range. The generated `rel_table.c` lists them.
- **Building.** The launcher unpacks, translates (in parallel), renames and lists the modules after `main.dol`
  (`build_modules` in `builder.cpp`); the scripts do the same by hand.

## Addresses

The runtime's replaced functions and the data it reads were found again in Wii Party's `main.dol` by matching
function bodies against ResortRecomp's (`scripts/port_addresses.py`): `native/replacements.txt` and the
`#define`s at the top of `native/src/audio.c` and `fake_remote.c`.

## Controllers

The game only ever talks to stand-in remotes (`native/src/fake_remote.c`). In local play each seat is filled by
a real Wii Remote, driven by the runtime itself (`remote_driver.c`, up to four, through `real_remote.c`), or a
gamepad (`gamepad.c`, SDL3), as they connect. Driving the remote is what makes pointing work without a sensor bar:
Wii Party never switches on a MotionPlus, so the runtime does, and turns its turning into sensor bar dots
(`motion_pointer.c`); a remote with no MotionPlus points by tilt.

## Frame rate

Wii Party sets its frame rate per scene with a function at 0x80072020 (fields a frame; from that it works out
frames a second and a time step that animation and timers use). It asks a debug switch at 0x8005CE90, compiled to
answer no, whether to force one field a frame. `--fps 60` (Launcher > Graphics) makes the switch answer yes.

## The shader cache

The graphics library crashed at start on a shader cache its SQLite read as malformed. `native/src/cache_check.c`
reads the cache the same way before the window opens, and rebuilds it or moves it aside if it can't.
