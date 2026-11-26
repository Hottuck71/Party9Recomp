/* Gamepads as Wii Remotes, through SDL3. See gamepad.c. */
#ifndef GAMEPAD_H
#define GAMEPAD_H
#include <stdio.h>
#include "host.h"

#define GAMEPAD_SLOTS 4

typedef struct {
    int present;
    u32 id;                 /* SDL's id for the pad, so a different pad in the same slot is noticed */
    u32 buttons;            /* Wii Remote button bits, as fake_remote.c uses them */
    float x, y;             /* pointer, window units: -1 to 1, y down */
    int pointer;            /* 1 while it is on the screen */
    float pitch, roll, swing; /* tilt in radians and a shake, 0 to 1, from the sticks and bumper */
    int motion;             /* the pad has an accelerometer: accel is what it measures */
    float accel[3];         /* in g, in the stand-in remote's axes */
    float nx, ny;           /* the left stick, -1 to 1, up positive */
} PadInput;

void gamepad_poll(void);                  /* the window's thread, once a frame */
int gamepad_get(int slot, PadInput* out); /* any thread; 1 if a pad is in that slot */
void gamepad_output(int slot, int player, int rumble); /* the player it plays as (its lights), and its rumble */
void gamepad_write_report(FILE* out);
/* Sideways controls (Mario Party 9's usual hold): gamepads and the keyboard press the buttons a sideways Wii Remote
   would for the same thing on screen. On by default; F8 or a pad's left stick press switches it. */
void gamepad_set_sideways(int on);
int gamepad_sideways(void);
u32 gamepad_turn(u32 upright_buttons); /* the buttons as pressed sideways, or unchanged when upright */
u32 gamepad_turn_keyboard(u32 upright_buttons); /* the same for the keyboard; mouse clicks stay A and B */
int gamepad_self_test(void);           /* --test-gamepad: a virtual pad's buttons checked; returns failures */

#endif
