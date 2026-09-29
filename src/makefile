# =============================================================================
# GOOB Chess Engine – Multi-ISA Makefile
# =============================================================================
# Produces one binary per x86-64 ISA tier, named:
#
#   Linux  : bin/linux/GOOB-2.2-BETA-universal     (Autodetects AVX-512/AVX2/BMI2)
#            bin/linux/GOOB-2.2-BETA-x86-64
#            bin/linux/GOOB-2.2-BETA-x86-64-v2
#            bin/linux/GOOB-2.2-BETA-x86-64-v3     (BMI2/PEXT)
#            bin/linux/GOOB-2.2-BETA-x86-64-v4     (AVX-512)
#            bin/linux/GOOB-2.2-BETA-native         (host-tuned, local dev)
#
#   Windows: bin/windows/GOOB-2.2-BETA-universal.exe
#            bin/windows/GOOB-2.2-BETA-x86-64.exe
#            bin/windows/GOOB-2.2-BETA-x86-64-v2.exe
#            bin/windows/GOOB-2.2-BETA-x86-64-v3.exe
#            bin/windows/GOOB-2.2-BETA-x86-64-v4.exe
#            bin/windows/GOOB-2.2-BETA-native.exe
#
# ISA tier summary
# -----------------------------------------------------------------------------
#  universal: Single binary containing AVX-512, AVX2, BMI2, SSE4.2, and baseline
#             code paths; autodetects host CPU hardware at runtime (Stockfish style).
#  x86-64   : baseline SSE2 – widest compatibility, magic slider fallback.
#  x86-64-v2: + SSE4.2 / POPCNT.
#  x86-64-v3: + AVX2 / BMI2.  attacks.c switches to PEXT.
#  x86-64-v4: + AVX-512 (BW, F, VL, DQ).
#  native   : tuned to the host CPU; for local benchmarking.
#
# Key design decisions
# -----------------------------------------------------------------------------
#  * Each tier gets its own obj subdirectory so parallel builds never clobber
#    each other (obj/x86-64, obj/x86-64-v2, …).
#  * USE_PEXT is passed for all tiers.  attacks.c gates actual PEXT use on
#    __BMI2__ which is only set when -march implies BMI2 (v3 / native).
#    On x86-64 / v2 it silently falls back to classic magics – no extra flag.
#  * On Linux we link with -pthread (tinycthread wraps pthreads).
#    On Windows we link without -pthread (tinycthread uses Win32 API directly),
#    with -static (no missing DLL dependencies), and with a 16MB stack.
#  * tbprobe.c detects _WIN32 at compile time for its own platform paths.
# =============================================================================

# ---------------------------------------------------------------------------
# Target platform detection
# ---------------------------------------------------------------------------
TARGET ?=
ifeq ($(TARGET),)
    ifneq ($(findstring mingw,$(CC)),)
        TARGET := windows
    else ifeq ($(OS),Windows_NT)
        TARGET := windows
    else
        TARGET := linux
    endif
endif

# ---------------------------------------------------------------------------
# Host shell detection (Windows cmd.exe vs POSIX shell: Linux, MSYS2, Git-Bash)
# ---------------------------------------------------------------------------
IS_CMD := 0
ifeq ($(OS),Windows_NT)
    ifeq ($(findstring sh,$(SHELL)),)
        IS_CMD := 1
    endif
endif

ifeq ($(IS_CMD),1)
    MKDIR_CMD = if not exist $(subst /,\,$(1)) mkdir $(subst /,\,$(1))
    RMDIR_CMD = if exist $(subst /,\,$(1)) rmdir /s /q $(subst /,\,$(1))
    RM_CMD    = if exist $(subst /,\,$(1)) del /q /f $(subst /,\,$(1))
else
    MKDIR_CMD = mkdir -p $(1)
    RMDIR_CMD = rm -rf $(1)
    RM_CMD    = rm -f $(1)
endif

# ---------------------------------------------------------------------------
# Platform-specific toolchain and flags
# ---------------------------------------------------------------------------
ifeq ($(TARGET),windows)
    ifeq ($(origin CC),default)
        ifeq ($(OS),Windows_NT)
            CC := gcc
        else
            CC := x86_64-w64-mingw32-gcc
        endif
    endif
    EXE            := .exe
    THREAD_FLAGS   :=
    PLATFORM_FLAGS := -DPLATFORM_WINDOWS
    OUTDIR         := bin/windows
    LDFLAGS        := -lm -flto=auto -static -Wl,--stack,16777216
else
    ifeq ($(origin CC),default)
        CC := cc
    endif
    EXE            :=
    THREAD_FLAGS   := -pthread
    PLATFORM_FLAGS := -DPLATFORM_LINUX
    OUTDIR         := bin/linux
    LDFLAGS        := -lm -flto=auto
endif

# ---------------------------------------------------------------------------
# Source / name settings
# ---------------------------------------------------------------------------
NAME     := GOOB-2.2-BETA
SRCS     := $(wildcard *.c)
EVALFILE ?= weights/quantised.bin

# ---------------------------------------------------------------------------
# Common compiler flags (ISA-neutral)
# ---------------------------------------------------------------------------
# -DUSE_PEXT  – tells attacks.c to include <immintrin.h> and define the PEXT
#               path; the inner #ifdef __BMI2__ guard makes it a no-op on
#               CPUs that do not have BMI2.
BASE_CFLAGS := -O3 -flto=auto $(THREAD_FLAGS) -DUSE_PEXT -MMD -MP $(PLATFORM_FLAGS) -DEVALFILE=\"$(EVALFILE)\"

# Trace search heuristic tracking
TRACE ?= 0
ifeq ($(TRACE),1)
    BASE_CFLAGS += -DTRACE
    OBJ_SUFFIX := -trace
    BIN_SUFFIX := -trace
else
    OBJ_SUFFIX :=
    BIN_SUFFIX :=
endif

# ---------------------------------------------------------------------------
# Per-ISA march flags
# ---------------------------------------------------------------------------
MARCH_universal := -march=x86-64 -DUNIVERSAL_BUILD
MARCH_x86-64    := -march=x86-64
MARCH_x86-64-v2 := -march=x86-64-v2
MARCH_x86-64-v3 := -march=x86-64-v3
MARCH_x86-64-v4 := -march=x86-64-v4
MARCH_native    := -march=native

# ---------------------------------------------------------------------------
# Build rule generator macro
# ---------------------------------------------------------------------------
# For each ISA tier we synthesise:
#   * A dedicated obj directory   obj/<arch>/ (or obj/<arch>-trace/)
#   * A pattern rule              obj/<arch>/%.o : %.c
#   * A link rule                 $(OUTDIR)/NAME-<arch>[.exe]
#   * A phony shorthand           make universal  make x86-64-v3  (etc.)
#
# Invoked as: $(call ISA_RULES,<arch>)
# ---------------------------------------------------------------------------
define ISA_RULES
OBJS_$(1)   := $$(patsubst %.c, obj/$(1)$(OBJ_SUFFIX)/%.o, $(SRCS))
BIN_$(1)    := $(OUTDIR)/$(NAME)-$(1)$(BIN_SUFFIX)$(EXE)
CFLAGS_$(1) := $(BASE_CFLAGS) $$(MARCH_$(1))

ALL_BINS += $$(BIN_$(1))

$$(BIN_$(1)): $$(OBJS_$(1)) | $(OUTDIR)
	$(CC) $$(CFLAGS_$(1)) $$(OBJS_$(1)) -o $$@ $(LDFLAGS)
	@echo "  LD  $$@"
ifeq ($(TRACE),1)
	@cp -f $$@ $(OUTDIR)/$(NAME)-$(1)$(EXE) 2>/dev/null || true
endif

obj/$(1)$(OBJ_SUFFIX)/%.o: %.c | obj/$(1)$(OBJ_SUFFIX)
	$(CC) $$(CFLAGS_$(1)) -c $$< -o $$@

obj/$(1)$(OBJ_SUFFIX):
	@$$(call MKDIR_CMD,obj/$(1)$(OBJ_SUFFIX))

.PHONY: $(1)
$(1): $$(BIN_$(1))

endef

# Expand rules for every architecture tier
ARCHS := universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4 native
$(foreach arch,$(ARCHS),$(eval $(call ISA_RULES,$(arch))))

.PHONY: trace
trace:
	$(MAKE) TRACE=1 native

# ---------------------------------------------------------------------------
# Default target – build universal binary (or all with 'make all')
# ---------------------------------------------------------------------------
.PHONY: all universal-default
.DEFAULT_GOAL := universal
all: $(ALL_BINS)

# ---------------------------------------------------------------------------
# OS explicit targets
# ---------------------------------------------------------------------------
.PHONY: windows win linux
windows win:
	$(MAKE) TARGET=windows universal

linux:
	$(MAKE) TARGET=linux universal

# ---------------------------------------------------------------------------
# Output directory
# ---------------------------------------------------------------------------
$(OUTDIR):
	@$(call MKDIR_CMD,$(OUTDIR))

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
.PHONY: clean
clean:
	@$(call RMDIR_CMD,obj)
	@$(call RMDIR_CMD,bin/windows)
	@$(call RMDIR_CMD,bin/linux)
	@$(call RM_CMD,.depend)

# ---------------------------------------------------------------------------
# Rebuild
# ---------------------------------------------------------------------------
.PHONY: rebuild
rebuild: clean all

# ---------------------------------------------------------------------------
# Profile-Guided Optimization (PGO) — native tier only
# Three-phase build: instrument → profile → optimise.  Yields ~5-10% NPS.
# ---------------------------------------------------------------------------
PGO_CFLAGS_GEN  := $(BASE_CFLAGS) $(MARCH_native) -fprofile-generate
PGO_CFLAGS_USE  := $(BASE_CFLAGS) $(MARCH_native) -fprofile-use -fno-peel-loops -fno-tracer
PGO_OBJS_GEN    := $(patsubst %.c, obj/pgo-gen/%.o, $(SRCS))
PGO_OBJS_USE    := $(patsubst %.c, obj/pgo-use/%.o, $(SRCS))
PGO_BIN_GEN     := $(OUTDIR)/$(NAME)-pgo-gen$(EXE)
PGO_BIN         := $(OUTDIR)/$(NAME)-native$(EXE)

.PHONY: pgo
pgo:
	@echo "=== PGO Phase 1: instrumented build ==="
	@$(call MKDIR_CMD,obj/pgo-gen)
	@$(call MKDIR_CMD,$(OUTDIR))
	$(foreach src,$(SRCS),$(CC) $(PGO_CFLAGS_GEN) -c $(src) -o obj/pgo-gen/$(basename $(src)).o;)
	$(CC) $(PGO_CFLAGS_GEN) $(PGO_OBJS_GEN) -o $(PGO_BIN_GEN) $(LDFLAGS)
	@echo "=== PGO Phase 2: collecting profile ==="
	$(PGO_BIN_GEN) bench || true
	@echo "=== PGO Phase 3: optimised build ==="
	@$(call MKDIR_CMD,obj/pgo-use)
	$(foreach src,$(SRCS),$(CC) $(PGO_CFLAGS_USE) -c $(src) -o obj/pgo-use/$(basename $(src)).o;)
	$(CC) $(PGO_CFLAGS_USE) $(PGO_OBJS_USE) -o $(PGO_BIN) $(LDFLAGS)
	@echo "  LD  $(PGO_BIN) (PGO)"

# ---------------------------------------------------------------------------
# Auto-generated header dependency includes (-MMD -MP)
# Rebuilds dependents whenever a header changes.
# ---------------------------------------------------------------------------
-include $(wildcard obj/*/*.d)

# Rebuild nnue_loader.o whenever the embedded network binary changes
obj/%/nnue_loader.o: $(EVALFILE)

# ---------------------------------------------------------------------------
# Legacy 'depend' target (kept for compatibility)
# ---------------------------------------------------------------------------
.PHONY: depend
depend: $(SRCS)
	$(CC) -MM $(SRCS) | sed 's|^\(.*\)\.o:|obj/x86-64/\1.o:|' > .depend
-include .depend
