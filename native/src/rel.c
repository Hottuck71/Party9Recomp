/* Stands in for the SDK's module linker (OSLink.c's Link and OSUnlink), so each module the game loads runs from
   the translation made for it. See rel.h.

   The game reads a module into a heap buffer and asks the SDK to link it there. Here the buffer keeps the header
   the game looks at, but the module itself is copied to its slot and relocated for the slot: its sections, its
   BSS, and every pointer in its data then agree with the addresses its translated code was made for. The header's
   section addresses, prolog and epilog point into the slot too, so the game's call of the prolog lands in
   translated code. The module list at 0x800030C8 holds the game's buffer, as on the console.

   The relocation format is the SDK's REL format, as documented publicly (for example wiibrew.org, "REL"). */
#include <string.h>
#include "rel.h"

extern FILE* report;
u8* plain_memory(u32 addr, u32 size);
u64 load_be(const u8* p, u32 size);
void store_be(u8* p, u64 v, u32 size);
void host_stop(const char* reason);

#define MODULE_HEAD 0x800030C8u
#define MODULE_TAIL 0x800030CCu
#define STRING_TABLE 0x800030D0u
#define MAX_LINKED 16

static struct {
    u32 header; /* the game's buffer */
    const RelModule* m;
} linked[MAX_LINKED];
static u32 links, unlinks, relocations;

static u8* at(u32 addr, u32 size) { return plain_memory(addr & 0x3FFFFFFFu, size); }

static u32 rd32(u32 a) {
    u8* p = at(a, 4);
    return p ? (u32)load_be(p, 4) : 0;
}
static void wr32(u32 a, u32 v) {
    u8* p = at(a, 4);
    if (p) store_be(p, v, 4);
}
static void wr16(u32 a, u32 v) {
    u8* p = at(a, 2);
    if (p) store_be(p, v & 0xFFFFu, 2);
}
static void wr8(u32 a, u32 v) {
    u8* p = at(a, 1);
    if (p) *p = (u8)v;
}

static const RelModule* module_by_id(u32 id) {
    for (unsigned i = 0; i < rel_module_count; i++)
        if (rel_modules[i].id == id) return &rel_modules[i];
    return NULL;
}

RelFunction rel_find(u32 pc) {
    for (int i = 0; i < MAX_LINKED; i++) {
        const RelModule* m = linked[i].m;
        if (m && pc >= m->base && pc < m->end) return m->find(pc);
    }
    return NULL;
}

/* Where section i of the module lives once linked: in its slot, or for BSS the slot's BSS. */
static u32 section_address(const RelModule* m, u32 header_offsets[], u32 sizes[], u32 i) {
    if (header_offsets[i] & ~1u) return m->base + (header_offsets[i] & ~1u);
    if (sizes[i]) return m->bss;
    return 0;
}

static int apply(u32 type, u32 p, u32 s) {
    u32 w;
    switch (type) {
    case 0: return 1;                                    /* R_PPC_NONE */
    case 1: wr32(p, s); return 1;                        /* R_PPC_ADDR32 */
    case 2: w = rd32(p); wr32(p, (w & ~0x03FFFFFCu) | (s & 0x03FFFFFCu)); return 1; /* ADDR24 */
    case 3: case 4: wr16(p, s); return 1;                /* ADDR16, ADDR16_LO */
    case 5: wr16(p, s >> 16); return 1;                  /* ADDR16_HI */
    case 6: wr16(p, (s >> 16) + ((s & 0x8000u) ? 1u : 0u)); return 1; /* ADDR16_HA */
    case 7: case 8: case 9: w = rd32(p); wr32(p, (w & ~0xFFFCu) | (s & 0xFFFCu)); return 1; /* ADDR14 */
    case 10: w = rd32(p); wr32(p, (w & ~0x03FFFFFCu) | ((s - p) & 0x03FFFFFCu)); return 1; /* REL24 */
    case 11: case 12: case 13: w = rd32(p); wr32(p, (w & ~0xFFFCu) | ((s - p) & 0xFFFCu)); return 1; /* REL14 */
    default: return 0;
    }
}

void rel_link(CPUState* ctx) {
    u32 x = ctx->gpr[3], bss = ctx->gpr[4], fixed = ctx->gpr[5];
    u32 id = rd32(x), version = rd32(x + 0x1C), count = rd32(x + 0x0C);
    const RelModule* m = module_by_id(id);
    ctx->gpr[3] = 0;
    if (!m || version > 3 || count > 64) {
        fprintf(report, "REL: module %u (version %u) at %08X has no translation\n", id, version, x);
        host_stop("a module with no translation");
        return;
    }
    int free_slot = -1;
    for (int i = 0; i < MAX_LINKED; i++) {
        const RelModule* o = linked[i].m;
        if (!o) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (o->base < m->end && m->base < o->end) {
            fprintf(report, "REL: %s (%u) wants its slot %08X while %s (%u) is still linked there\n", m->name, m->id,
                    m->base, o->name, o->id);
            host_stop("two modules in one slot");
            return;
        }
    }
    if (free_slot < 0) {
        host_stop("too many modules linked");
        return;
    }

    u32 fix = version >= 3 ? rd32(x + 0x48) : 0;
    u32 sec_info = rd32(x + 0x10), imp = rd32(x + 0x28), imp_size = rd32(x + 0x2C), rel = rd32(x + 0x24);
    if (!fix) fix = m->bss - m->base;
    u8* from = at(x, fix);
    u8* to = at(m->base, fix);
    u8* bss_slot = at(m->bss, m->end - m->bss);
    if (!from || !to || (!bss_slot && m->end > m->bss)) {
        fprintf(report, "REL: %s at %08X: buffer or slot out of memory (fix %u)\n", m->name, x, fix);
        host_stop("module buffer out of memory");
        return;
    }
    memcpy(to, from, fix);
    if (bss_slot) memset(bss_slot, 0, m->end - m->bss);

    /* The header, as Link leaves it. */
    wr8(x + 0x33, 0);
    u32 tail = rd32(MODULE_TAIL);
    if (tail) wr32(tail + 4, x);
    else wr32(MODULE_HEAD, x);
    wr32(x + 8, tail);
    wr32(x + 4, 0);
    wr32(MODULE_TAIL, x);

    u32 offsets[64], sizes[64];
    for (u32 i = 0; i < count; i++) {
        offsets[i] = rd32(x + sec_info + i * 8u);
        sizes[i] = rd32(x + sec_info + i * 8u + 4u);
    }
    u32 bss_section = 0;
    for (u32 i = 1; i < count; i++) {
        u32 addr = section_address(m, offsets, sizes, i);
        if (!(offsets[i] & ~1u) && sizes[i]) bss_section = i;
        if (addr) wr32(x + sec_info + i * 8u, addr | (offsets[i] & 1u));
    }
    wr8(x + 0x33, bss_section);
    wr32(x + 0x10, x + sec_info);
    wr32(x + 0x28, x + imp);
    wr32(x + 0x24, x + rel);
    for (u32 o = 0; o + 8 <= imp_size; o += 8) wr32(x + imp + o + 4, x + rd32(x + imp + o + 4));
    static const u32 entry_fields[3][2] = {{0x30, 0x34}, {0x31, 0x38}, {0x32, 0x3C}};
    for (int e = 0; e < 3; e++) {
        u8* sec = at(x + entry_fields[e][0], 1);
        if (sec && *sec && *sec < count)
            wr32(x + entry_fields[e][1], rd32(x + entry_fields[e][1]) + section_address(m, offsets, sizes, *sec));
    }
    if (rd32(STRING_TABLE) && rd32(x + 0x14)) wr32(x + 0x14, rd32(x + 0x14) + rd32(STRING_TABLE));

    /* Relocate the slot's copy: imports from main.dol (id 0) and from the module itself. */
    for (u32 o = 0; o + 8 <= imp_size; o += 8) {
        u32 from_id = rd32(x + imp + o);
        if (from_id != 0 && from_id != id) {
            fprintf(report, "REL: %s imports from module %u, which isn't supported\n", m->name, from_id);
            continue;
        }
        u32 r = rd32(x + imp + o + 4); /* the import's relocation list, made absolute above */
        u32 p = 0;
        for (u32 guard = 0; guard < 4000000u; guard++, r += 8) {
            u8* e = at(r, 8);
            if (!e) break;
            u32 off = (u32)load_be(e, 2), type = e[2], section = e[3], addend = (u32)load_be(e + 4, 4);
            if (type == 203) break; /* R_DOLPHIN_END */
            p += off;
            if (type == 202) { /* R_DOLPHIN_SECTION: patches continue at the start of this section */
                p = section < count ? section_address(m, offsets, sizes, section) : 0;
                continue;
            }
            if (type == 201) continue; /* R_DOLPHIN_NOP: only moves p */
            u32 s = from_id ? (section < count ? section_address(m, offsets, sizes, section) : 0) + addend : addend;
            if (!apply(type, p, s)) {
                fprintf(report, "REL: %s: relocation type %u at %08X isn't known\n", m->name, type, p);
                host_stop("unknown relocation");
                return;
            }
            relocations++;
        }
    }
    if (fixed) { /* as the SDK does: keep only imports from other modules, which come first */
        for (u32 o = 0; o + 8 <= imp_size; o += 8) {
            u32 from_id = rd32(x + imp + o);
            if (from_id == 0 || from_id == id) {
                wr32(x + 0x2C, o);
                break;
            }
        }
    }
    if (bss) {
        u8* b = at(bss, m->bss_size);
        if (b) memset(b, 0, m->bss_size);
    }
    linked[free_slot].header = x;
    linked[free_slot].m = m;
    links++;
    fprintf(report, "REL: linked %s (%u) from %08X into %08X-%08X\n", m->name, m->id, x, m->base, m->end);
    ctx->gpr[3] = 1;
}

void rel_unlink(CPUState* ctx) {
    u32 x = ctx->gpr[3];
    u32 next = rd32(x + 4), prev = rd32(x + 8);
    if (next) wr32(next + 8, prev);
    else wr32(MODULE_TAIL, prev);
    if (prev) wr32(prev + 4, next);
    else wr32(MODULE_HEAD, next);
    for (int i = 0; i < MAX_LINKED; i++)
        if (linked[i].m && linked[i].header == x) {
            fprintf(report, "REL: unlinked %s (%u)\n", linked[i].m->name, linked[i].m->id);
            linked[i].m = NULL;
            linked[i].header = 0;
        }
    unlinks++;
    ctx->gpr[3] = 1;
}

void rel_write_report(FILE* out) {
    fprintf(out, "modules: %u linked, %u unlinked, %u relocations; linked now:", links, unlinks, relocations);
    for (int i = 0; i < MAX_LINKED; i++)
        if (linked[i].m) fprintf(out, " %s", linked[i].m->name);
    fprintf(out, "\n");
}
