# Party9Recomp

Play **your own copy** of Mario Party 9 on a Windows 10 or 11 PC, as a native PC program: real Wii Remotes (pointing
without a sensor bar), Nintendo Switch Joy-Cons, gamepads and the keyboard with sideways controls, the mouse as a pointer, up to four players,
and online play with friends.

By **Lane Carle (crazyman12)**. Built on [PartyRecomp](https://github.com/Hottuck71/PartyRecomp) and
[ResortRecomp](https://github.com/Hottuck71/ResortRecomp), the same launcher and runtime made for Wii Party and
Wii Sports Resort.

> **Party9Recomp contains no part of the game.** No code, no graphics, no sound, nothing made from them, in this
> repository or in its downloads. Each player provides their own Mario Party 9 disc, and the launcher translates and
> builds that copy into a PC program **on their own PC**. Mario Party 9 is © Nintendo. Party9Recomp is not made,
> sponsored, or endorsed by Nintendo.

**Status: early version (0.1.0).** The game builds, boots, and plays: the title, menus, character select, a party
on Toad Road with its minigames and results, sound, Wii Remotes, gamepads, and online play through a relay (0
desyncs in testing). The other boards, most minigames and the other modes haven't been played through yet, so
please report anything that goes wrong.

## Playing

1. From [Releases](../../releases), download **`Party9Recomp-<version>-Setup.exe`** and run it (it asks where to
   put Party9Recomp), or download the `.zip` and unzip it yourself. Allow about 20 GB once set up, not inside
   Program Files. Windows may warn about an unknown program: "More info", then "Run anyway".
2. You need: your own **Mario Party 9 (USA, SSQE01)** as a disc image you made from your disc, and
   [Dolphin](https://dolphin-emu.org) installed (only its `DolphinTool.exe` is used, to extract the image).
3. Open `Launcher.exe`. On the **Play** tab pick your disc image and `DolphinTool.exe`, then press
   **Set up the game**. The launcher extracts the disc, translates the game's code with
   [DolRecomp](https://github.com/ExpansionPak/DolRecomp) (the main program and the 115 modules Mario Party 9 loads
   as it goes, one per board, minigame, party mode and menu), and compiles it with the bundled GCC: 30 minutes or
   more.
4. Press **Play**.

The first time each scene appears, the graphics card prepares its shaders and that scene can stutter for a moment;
they're kept, so it's smooth from then on. On the Graphics tab, **Vulkan** prepares them fastest.

## Controllers and multiplayer

Up to four players. Wii Remotes take player 1, 2, 3 and 4 first, then gamepads, as they connect, even mid-game;
the keyboard and mouse always play as player 1 too.

- **Wii Remotes:** pair each once on the launcher's Wii Remote tab, then hold them sideways as on the Wii. A
  **Wii Remote Plus points with its gyro: no sensor bar needed** (the virtual sensor bar); a plain Wii Remote points
  by tilting.
- **Sideways controls** (on by default): gamepads and the keyboard act as a sideways Wii Remote, the d-pad matching
  the screen. Gamepad A = 2 (OK), B = 1 (back), X = A, Y or right trigger = B, Start = +, Back = −, Guide = HOME,
  right stick = pointer, left bumper = shake, left trigger + left stick = tilt. Keyboard: arrows, Z = 2, X = 1.
  F8 or a press of the left stick switches to upright.
- **Nintendo Switch Joy-Cons**, each one a Wii Remote Plus of its own: pair them with Windows once (hold the sync
  button on the rail, then **Settings > Bluetooth & devices > Add device**) and they take seats as they connect,
  after any Wii Remotes. Hold one sideways like a sideways Wii Remote, stick on the left: the bottom face button
  is 2 (OK), the right one 1 (back), the left one A, the top one B, the stick is the d-pad, SL = −, SR = +,
  HOME/Capture = HOME. Tilting and shaking work, and rumble and player lights. **SL and SR together** (or F8)
  switch to upright, top towards the screen, where it points with its gyro (no sensor bar): right Joy-Con A = A,
  ZR = B, stick = d-pad, Y = 1, B = 2; R or pressing the stick re-centres the pointer.
- **The mouse** points for player 1 whenever it moves, even with a Wii Remote or pad in hand, and keeps the pointer
  until that remote or pad points somewhere else. Left click is A, right click is B.

## Playing online

Everyone plays the host's game and save; only controller input travels (lockstep, like Dolphin's netplay), so it
stays smooth: about 50 ms of input delay on a home network. Players connect through a small relay server that
anyone can run, on **Windows** (double-click `relay\Start relay.bat`) or **Ubuntu** (one C file, a systemd
service). One relay serves Party9Recomp, PartyRecomp and ResortRecomp. Step by step:
**[docs/ONLINE-SERVER.md](docs/ONLINE-SERVER.md)** (also in the download as `relay\SERVER-SETUP.txt`).

**Please don't share** your extracted game, your built `Party9Recomp-Game.exe`, or anything else made from your
copy. Everyone sets up from their own disc.

## How it works

The game's PowerPC code is translated statically to C by DolRecomp, then compiled for x86-64 with this project's
runtime: a hand-written replacement for the Wii's operating system, graphics (through
[aurora](https://github.com/encounter/aurora) and Dawn), sound mixer, disc and save access, Wii Remote driver,
and online lockstep. Mario Party 9 keeps nearly all of its code in REL modules linked at run time: each is translated
ahead of time for a fixed address, and the runtime stands in for the Wii's module linker so every module lands where
its translation expects. See [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md) and
[docs/MARIO-PARTY-9.md](docs/MARIO-PARTY-9.md).

## Building

[docs/BUILDING.md](docs/BUILDING.md). Building the toolkit never needs the game.

## Licence

Party9Recomp is free software under the [GNU GPL, version 3](LICENSE), because it builds on DolRecomp (GPL-3.0).
Third-party parts keep their own licences: [native/toolkit/THIRD-PARTY.txt](native/toolkit/THIRD-PARTY.txt).
Mii pictures in the launcher: My Avatar Editor, © 2009 Trevor McCauley, MIT.
