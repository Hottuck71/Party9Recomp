/* REL modules: Mario Party 9 (like Wii Party) keeps nearly all of its game code in modules it loads while it runs (one per minigame,
   board, menu...). Each was translated ahead of time for one fixed address, its slot (scripts/rel_build.py);
   rel.c stands in for the SDK's linker so every module the game loads lands in its slot. */
#ifndef REL_H
#define REL_H
#include <stdio.h>
#include "cpu/cpu.h"

/* Slots sit above the 24 MB of MEM1 the game is told about, in memory only the runtime uses. */
#define REL_SPACE_BASE 0x81800000u
#define REL_SPACE_END 0x82000000u
#define HOST_MEM1_SIZE (REL_SPACE_END - 0x80000000u)

typedef void (*RelFunction)(CPUState* ctx);

typedef struct {
    u32 id;
    const char* name;
    u32 base, end; /* the slot's start, and the end of this module's BSS in it */
    u32 bss, bss_size;
    RelFunction (*find)(u32 address);
} RelModule;

extern const RelModule rel_modules[]; /* rel_table.c, made by scripts/rel_build.py */
extern const unsigned rel_module_count;

/* The translated code at pc in a module that is linked now, or NULL. */
RelFunction rel_find(u32 pc);
/* Replacements for the SDK's Link(module, bss, fixed) and OSUnlink(module). Both answer in r3. */
void rel_link(CPUState* ctx);
void rel_unlink(CPUState* ctx);
void rel_write_report(FILE* out);

#endif
