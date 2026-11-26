# What's different about Mario Party 9

Party9Recomp is PartyRecomp's launcher and runtime (itself ResortRecomp's), moved to Mario Party 9 (USA, SSQE01).
Mario Party 9 and Wii Party come from the same studio and share an engine shape: a small `main.dol` with the
SDK, and nearly all game code in REL modules loaded as the game goes. This page is what had to change, and where.

## REL modules

115 LZ11-compressed modules in `files/modules/*.rel.lz` (no duplicates), 52.7 MB once unpacked: one per board,
minigame, party mode and menu. Each imports only from itself and from `main.dol`.

- **Big modules.** The seven boards, the menu, both results screens, the tutorial, the credits and the seven
  "mgm" party modes each carry the whole board engine: 1.2 to 1.7 MB each. Minigames are 0.2 to 0.6 MB.
- **Slots.** Each module is translated for a fixed address in 0x81800000 to 0x82000000, above the 24 MB the game
  is told it has and within `main.dol`'s branch reach. A module's kind is its name without the trailing number;
  kinds sharing a slot never run together. One-off modules get a slot each, all minigames share one, and the
  engine-carrying modules get four (menu and credits; boards and tutorial; both results; party modes). The table
  is `kSlots` in `native/launcher/builder.cpp` and `SLOTS` in `scripts/rel_build.py`. If two modules ever want
  one slot at the same time, the runtime stops and the report names both.
- **The linker.** The SDK's `Link` (0x80183790) and `OSUnlink` (0x80183CD0) are replaced by `native/src/rel.c`,
  unchanged from PartyRecomp.

## Addresses

Found again in Mario Party 9's `main.dol` by matching function bodies against Wii Party's
(`scripts/port_addresses.py`): `native/replacements.txt`, the `#define`s at the top of `native/src/audio.c` and
`fake_remote.c`, and the hooks in `native/src/main.c`. Of the SDK functions the runtime replaces, all but three
matched uniquely; OSReport, DSPCheckMailToDSP and memset were settled by their place next to functions that did
match, and by caller counts. The SDK structures the runtime reads (WUD, WPAD, AX voice blocks, OS threads) were
checked to have the same field offsets in both games.

## Palettes (the character select fix)

The character select cards draw each card in layers, and each layer's palette is loaded into the same TMEM spot
just before its draw (the back layer an IA8 palette, the front an RGB5A3 one with see-through entries). Aurora
keeps one palette per TLUT slot and draws the frame later, so every layer took the last palette loaded and the
front layer came out opaque, hiding the portraits. `native/src/gx_adapter.c` now gives each palette (memory,
format, size) an aurora slot of its own, least recently used first; the report counts how often one had to move
out ("palettes: ..."). The same fix applies to any game on this runtime.

## The picture's height

The frame buffer handed to aurora is 640x480, not the hardware's 640x528: the game draws and shows 480 lines, and
a 528-line buffer left a black band under the picture (`EFB_HEIGHT` in `native/src/gx_adapter.c`). With the glue's
4:3 width ratio the picture is exactly 16:9.

## Sideways controls

Mario Party 9 is played with the Wii Remote held sideways. Gamepads and the keyboard therefore press the buttons
a sideways remote would for the same thing on screen (the d-pad turned, A is 2, B is 1): `gamepad_turn` in
`native/src/gamepad.c`, on by default, switched with F8 or a pad's left stick press, `--sideways 0` to start
upright (Launcher > Play > Sideways controls). `--test-gamepad 1` checks every button with a virtual SDL pad.
In scripts for testing: message boxes take A, Yes/No prompts need a d-pad press before 2, and the dice block is 2.

## Frame rate

Mario Party 9 sets its frame rate per scene with a setter at 0x80073660 (fields a frame in r3; it works out frames
a second at r13-29776 and the time step at r13-29772), and its frame pacer at 0x8008C698 waits that many fields.
Unlike Wii Party there's no leftover "always 60" switch, so `--fps 60` (Launcher > Graphics) has the setter asked
for one field a frame; the game then computes its own time step for 60, so play keeps its proper speed.

## Loading and stutter

Disc and NAND requests finish at once (`ios.c` answers in the same call). The report's processor-use line ("CPU:
field ..., the game busy N%", every 240 fields) shows the game keeping its processor only 1 to 35% busy through boot,
menus and the board's loading, and `--cpu-speed 200` (twice a Wii's processor time a field, like Dolphin's CPU
overclock; not offered in the launcher) linked every module on exactly the same field as 100. Loading is paced by
the game itself, not by processor work, so the flag stays a test option.

What made loading slow was the graphics card preparing pipelines the first time a scene used them (586 by the first
board): the CPU thread stays within a field of the graphics thread, so while a pipeline compiles the whole game
waits, and the game's field-counted loading takes longer in real time. In a first session the board's loading ran at
49.5 ms a frame; with that session's cache it ran at 17 to 18. Aurora keeps them in `pipeline_cache.db` and
`dawn_cache.db` in the cache folder and prepares the known ones at start, earliest-used first.

## Mouse pointer

The mouse takes player 1's pointer when it moves and keeps it, held still or not, until the other pointer (a pad's,
or a real remote's MotionPlus or tilt pointer) moves 0.2 window units from where it was when the mouse stopped
(`fake_remote_mouse_aims`). Before, it let go 1.5 s after it stopped, and the remote's tilt pointer jumped back.
Mouse clicks stay A and B in sideways mode (`gamepad_turn_keyboard`); only the keys turn.

## Window title

The prebuilt `builder/lib/aurora_glue.o` was compiled when the source still named Wii Sports Resort, so the window
said so. The name is patched in the object (the cache folder comes from `--cache`, so nothing else used it), and
`aurora_glue.cpp` now says Mario Party 9 for the next time the glue is compiled.
