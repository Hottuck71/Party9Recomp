Party9Recomp
============
Play your own copy of Mario Party 9 on a Windows 10 or 11 PC.
Built on PartyRecomp's and ResortRecomp's launcher and runtime by
Lane Carle (crazyman12).

Party9Recomp contains NO part of the game: no code, no pictures, no sounds,
nothing made from them. You need your own Mario Party 9 disc (USA, SSQE01),
copied to your PC as a disc image, and Dolphin installed (only its
DolphinTool is used, to extract the disc). The launcher then translates your
copy into a PC program and builds it, here, on your own PC.

Mario Party 9 is Nintendo's. Party9Recomp isn't made, sponsored, or endorsed
by Nintendo.

STATUS: EARLY VERSION (0.1.0). See "WHAT'S BEEN TESTED" at the end.


FIRST TIME
----------
1. Keep this folder somewhere with about 20 GB free (not inside Program Files).
2. Open Launcher.exe.
3. Play tab: pick your disc image, and DolphinTool.exe from your Dolphin
   folder under "Disc tools". Press "Set up the game".
4. Wait. The disc is extracted (a minute or two), then the game's code is
   translated: main.dol, and the 115 modules Mario Party 9 loads as it runs
   (one per board, minigame, party mode and menu). Then everything is built
   with the compiler in the builder folder: 30 minutes or more.
5. Press Play.

HOW MARIO PARTY 9 IS BUILT
--------------------------
Like Wii Party (same studio), Mario Party 9 loads nearly all of its code as
REL modules while it runs. Each module is translated ahead of time for a
fixed address (its "slot"), and the runtime stands in for the Wii's module
linker so every module the game loads lands in its slot. See builder\src\rel.c.

CONTROLLERS AND MULTIPLAYER
---------------------------
Up to four players. Wii Remotes take player 1, 2, 3 and 4 first, then
gamepads, in the order they connect (even mid-game). The keyboard and mouse
always play as player 1 too.

THE MOUSE AS THE POINTER
Move the mouse over the game window and it points for player 1, even with a
Wii Remote or gamepad in player 1's hands. It keeps the pointer while you
hold it still, until the remote or pad itself points somewhere else (turn or
tilt the remote, or move the pad's right stick); move the mouse again to take
the pointer back. Left click is A.

Wii Remotes: pair each one once (Launcher > Wii Remote tab), then hold them
sideways as on the Wii. No sensor bar is needed for the minigames you point
in: a Wii Remote Plus or MotionPlus points with its gyro. Hold it still for a
moment after it connects, then point at the middle of the screen and press A
to centre it. C re-centres, F9 / F10 flip left-right / up-down.

Nintendo Switch Joy-Cons: pair them once (Launcher > Joy-Con tab: hold the
sync button on the rail until the lights move, then click "The lights are
moving"). Each Joy-Con is a Wii Remote of its own, seated after Wii Remotes
and before gamepads. Hold it sideways, stick on the left:
  bottom button = 2 (OK)   right = 1 (back)   left = A   top = B
  stick = d-pad   SL = -   SR = +   HOME / Capture = HOME
Tilting and shaking work. SL and SR together (or F8) switch to upright, where
it points with its gyro: right Joy-Con A = A, ZR = B, Y = 1, B = 2.

SIDEWAYS CONTROLS (on by default)
Mario Party 9 is played with the Wii Remote held sideways, so gamepads and the
keyboard act as a sideways remote: the d-pad matches the screen, and

  Gamepad (Xbox, PlayStation, Switch Pro and others):
    A (bottom) = 2 (OK)      B (right) = 1 (back)
    X (left)   = A           Y (top)   = B       right trigger = B
    Start = +   Back = -     Guide/PS/Home = HOME
    D-pad or left stick = d-pad, turned to match the screen
    Right stick = pointer (click it to centre)
    Left bumper = shake      hold left trigger + left stick = tilt/steer
  Keyboard: arrows = d-pad, Z = 2, X = 1, 1 = A, 2 = B, Enter = +,
    Backspace = -, H = HOME, mouse = pointer.

F8, or pressing a gamepad's left stick, switches between sideways and upright
(upright: A = A, B = B, X = 1, Y = 2, the d-pad not turned) while you play.
Launcher > Play > "Sideways controls" sets how the game starts. Real Wii
Remotes are never changed: hold them however the game shows.

A pad with a gyro and accelerometer (PlayStation, Switch Pro, many others)
also points by turning it, and tilting and shaking it are real motion.

Pointing with no sensor bar (the virtual sensor bar): a Wii Remote Plus or
MotionPlus points with its gyro. A plain Wii Remote points by tilting it:
tip it up and down, and roll it left and right. F9 / F10 flip either way.

Lag: remotes report to the game 200 times a second, and a Wii Remote's
input goes straight to the game as it arrives.

THE FIRST TIME YOU PLAY: STUTTER AND SLOW LOADING
---------------------------------------------------
The first time each scene appears, your graphics card has to prepare the
shaders it draws with, and the game waits for them: the first trip to a board
can stutter and take several seconds longer. They're kept (in
%APPDATA%\Mario Party 9), so from then on that scene loads and plays
smoothly. Each new board or minigame does this once.

Loading itself runs at the Wii's pace: the disc and save are read instantly,
and the rest of a loading screen is the game's own timing, not your PC.

60 FPS EVERYWHERE (off by default)
----------------------------------
Mario Party 9 sets its own frame rate scene by scene; some scenes ask for 30
fps, as on the Wii. Launcher > Graphics > "60 fps everywhere" asks the game's
own frame rate setting for 60 instead, so the game works out its own timing
for 60 and plays at its normal speed. It costs more processor and graphics
card time. If anything ever runs too fast, turn it off again. Online games
always use the Wii's rates, so every PC matches.

ONLINE
------
Launcher > Online. Everyone plays the host's game and save; only controller
input travels, so it stays smooth. Input delay is measured when everyone has
joined: about 50 ms on a home network. Each PC plays as one player.
The easiest way is a relay server (no port forwarding for anyone but the
relay's computer). It runs on Ubuntu or Windows: see relay\SERVER-SETUP.txt,
or on Windows just double-click relay\Start relay.bat. A relay you already
run for Wii Party or Wii Sports Resort works for Mario Party 9 too.

YOUR SAVE
---------
Kept in the "save" folder here. Play tab > Save data exports and imports it.

WHAT'S IN THIS FOLDER
---------------------
Launcher.exe            the launcher
PairRemote.exe          pairs a Wii Remote with Windows
PairJoyCon.exe          pairs Nintendo Switch Joy-Cons with Windows
builder\                the translator (DolRecomp), the runtime as source,
                        the graphics library, and a C compiler (GCC)
relay\                  the online room server, and how to run it
licenses\               the licence of every part (GPL-3.0)
*.dll                   libraries the game and launcher use
After setting up, you'll also have: game\ (your extracted disc),
Party9Recomp-Game.exe (your build), save\, and build-log.txt.
Don't share those: they're made from your copy of the game.

WHAT'S BEEN TESTED
------------------
Played by test scripts on this build (not yet by hand):
- Boot, the strap screen, the title, the main menu, player and COM counts,
  character select, party setup, and a party on Toad Road (board 1): turn
  order, the dice block, a Free-for-All minigame chosen (Peak Precision,
  practice and the real race), the results, and back to the board. No crashes
  in over an hour of play across runs. Sound and music play.
- Online, two players through a relay (one on the same PC, and one on a home
  server over the network): each player picks their own character, then the
  board, a minigame, the results and back to the board, with 0 desyncs in
  16 minutes of play, at 60 fps with 50 ms input delay.
- Gamepads: a built-in test with a virtual pad checks every button, sideways
  and upright (Party9Recomp-Game.exe --test-gamepad 1).
- Wii Remotes: the remote driver and pointer self-tests pass.

Not yet tried: real Wii Remotes and gamepads by hand, the other six boards,
most minigames, the party modes and other menus, and 60 fps everywhere. Please
report anything that goes wrong.
