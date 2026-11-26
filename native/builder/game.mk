# Builds the game on the player's own PC, from their own disc: the launcher runs it after translating their
# main.dol and the REL modules. Nothing in the toolkit is made from the game; this is where its code is first compiled.
#
# Run from the toolkit folder with relative paths only (make can't take paths with spaces):
#   builder/toolchain/bin/mingw32-make -f builder/game.mk -j N VERSION=x.y.z [PGO=gen|use] [EXE=name.exe]
#
# builder/            this toolkit's parts: runtime source, prebuilt graphics library, compiler
# build/recomp/       the translation of the player's main.dol (DolRecomp's output)
# build/chunks/       the same chunks with the runtime's hooks added (the launcher writes these)
# build/rel/chunks/   the REL modules' translations, each for its own slot, functions renamed rel<id>_...
# build/rel/rel_table.c  every module, its slot, and its lookup (src/rel.h)
# build/obj/          objects; with PGO, the .gcda counts sit beside them

TK := builder
WORK := build
SRC := $(TK)/src
GEN := $(WORK)/recomp/generated
OBJ := $(WORK)/obj
EXE ?= Party9Recomp-Game.exe
VERSION ?= dev
# The bundled GCC was built for MSYS2's own folder; --sysroot tells it where its headers and libraries are now.
CC := gcc --sysroot=$(TK)/toolchain
CXX := g++ --sysroot=$(TK)/toolchain

# x86-64-v3 is any CPU since about 2015. Contraction stays off: fused multiply-add would change the game's float
# results, and online play needs every PC to compute the same.
CPU_FLAGS := -march=x86-64-v3 -ffp-contract=off
PGO ?=
ifeq ($(PGO),gen)
PGO_FLAGS := -fprofile-generate -DPGO_GEN
PGO_LINK := -fprofile-generate
else ifeq ($(PGO),use)
PGO_FLAGS := -fprofile-use -fprofile-partial-training -Wno-missing-profile -fprofile-correction -Wno-error=coverage-mismatch
PGO_LINK :=
endif
COMMON := -O2 -g0 -w -fwrapv -fno-strict-aliasing $(CPU_FLAGS) $(PGO_FLAGS) -I$(GEN) -I$(TK)/dolrecomp -I$(SRC) -I$(TK)/include
# The modules are eight times main.dol's code. -O1 compiles them in about half the time, and most of a module's
# time goes to the same few loops main.dol's SDK code runs anyway. REL_OPT=-O2 for the slower, faster build.
REL_OPT ?= -O1
REL_COMMON := $(subst -O2,$(REL_OPT),$(COMMON))

CHUNKS := $(wildcard $(WORK)/chunks/*.c)
CHUNK_OBJS := $(patsubst $(WORK)/chunks/%.c,$(OBJ)/%.o,$(CHUNKS))
REL_CHUNKS := $(wildcard $(WORK)/rel/chunks/*.c)
REL_OBJS := $(patsubst $(WORK)/rel/chunks/%.c,$(OBJ)/%.o,$(REL_CHUNKS))
HOST_SRCS := $(wildcard $(SRC)/*.c)
HOST_OBJS := $(patsubst $(SRC)/%.c,$(OBJ)/host_%.o,$(HOST_SRCS))
ALL_OBJS := $(CHUNK_OBJS) $(REL_OBJS) $(OBJ)/rel_table.o $(HOST_OBJS) $(OBJ)/cpu_core.o

all: $(EXE)

# The object list goes in a response file: Windows' shell doesn't expand *.o, and the list is long.
$(EXE): $(ALL_OBJS)
	$(file >$(OBJ)/objects.rsp,$(ALL_OBJS))
	$(CXX) $(PGO_LINK) -o $@ @$(OBJ)/objects.rsp $(TK)/lib/aurora_glue.o -L$(TK)/lib -Wl,-u,WinMain \
		-Wl,--stack,67108864 @$(TK)/lib/link.rsp -lhid -lsetupapi -lwinmm -lws2_32 -lm

$(OBJ)/%.o: $(WORK)/chunks/%.c $(SRC)/fastmem.h | $(OBJ)
	$(CC) $(COMMON) -include $(SRC)/fastmem.h -c $< -o $@

$(OBJ)/%.o: $(WORK)/rel/chunks/%.c $(SRC)/fastmem.h | $(OBJ)
	$(CC) $(REL_COMMON) -include $(SRC)/fastmem.h -c $< -o $@

$(OBJ)/rel_table.o: $(WORK)/rel/rel_table.c $(SRC)/rel.h | $(OBJ)
	$(CC) $(COMMON) -c $< -o $@

$(OBJ)/host_%.o: $(SRC)/%.c $(wildcard $(SRC)/*.h) | $(OBJ)
	$(CC) $(COMMON) -Dmain=aurora_main "-DRR_BUILD_ID=\"Party9Recomp $(VERSION)\"" -c $< -o $@

$(OBJ)/cpu_core.o: $(TK)/dolrecomp/cpu_core.c | $(OBJ)
	$(CC) $(COMMON) -I$(TK)/dolrecomp/cpu -c $< -o $@

$(OBJ):
	mkdir $(subst /,\,$@)
