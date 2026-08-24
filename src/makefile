# =============================================================================
# GOOB Chess Engine – Multi-ISA Makefile
# =============================================================================
# Produces one binary per x86-64 ISA tier, named:
#
#   Linux  : bin/linux/GOOB-2.2-BETA-x86-64
#             bin/linux/GOOB-2.2-BETA-x86-64-v2
#             bin/linux/GOOB-2.2-BETA-x86-64-v3     (BMI2/PEXT)
#             bin/linux/GOOB-2.2-BETA-native         (host-tuned, local dev)
#
#   Windows: bin/windows/GOOB-2.2-BETA-x86-64.exe
#             bin/windows/GOOB-2.2-BETA-x86-64-v2.exe
#             bin/windows/GOOB-2.2-BETA-x86-64-v3.exe
#             bin/windows/GOOB-2.2-BETA-native.exe
#
# ISA tier summary
# -----------------------------------------------------------------------------
#  x86-64   : baseline SSE2 – widest compatibility, magic slider fallback.
#  x86-64-v2: + SSE4.2 / POPCNT.  tbprobe gets HW popcount via __SSE4_2__.
#  x86-64-v3: + AVX2 / BMI2.  attacks.c switches to PEXT (USE_PEXT+__BMI2__).
#  native   : same ISA as v3 but tuned to the host CPU; for local benchmarking.
#
# Key design decisions
# -----------------------------------------------------------------------------
#  * Each tier gets its own obj subdirectory so parallel builds never clobber
#    each other (obj/x86-64, obj/x86-64-v2, …).
#  * USE_PEXT is passed for all tiers.  attacks.c gates actual PEXT use on
#    __BMI2__ which is only set when -march implies BMI2 (v3 / native).
#    On x86-64 / v2 it silently falls back to classic magics – no extra flag.
#  * On Linux   we link with -pthread (tinycthread wraps pthreads).
#    On Windows  we link without -pthread (tinycthread uses Win32 API directly).
#  * tbprobe.c detects _WIN32 at compile time for its own platform paths.
# =============================================================================

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
ifeq ($(OS), Windows_NT)
    CC             := gcc
    RM             := del /Q
    MKDIR          := mkdir
    EXE            := .exe
    THREAD_FLAGS   :=
    PLATFORM_FLAGS := -DPLATFORM_WINDOWS
    OUTDIR         := bin/windows
else
    CC             := cc
    RM             := rm -f
    MKDIR          := mkdir -p
    EXE            :=
    THREAD_FLAGS   := -pthread
    PLATFORM_FLAGS := -DPLATFORM_LINUX
    OUTDIR         := bin/linux
endif

# ---------------------------------------------------------------------------
# Source / name settings
# ---------------------------------------------------------------------------
NAME := GOOB-2.2-BETA
SRCS := $(wildcard *.c)

# ---------------------------------------------------------------------------
# Common compiler flags (ISA-neutral)
# ---------------------------------------------------------------------------
# -DUSE_PEXT  – tells attacks.c to include <immintrin.h> and define the PEXT
#               path; the inner #ifdef __BMI2__ guard makes it a no-op on
#               CPUs that do not have BMI2.
BASE_CFLAGS := -O3 $(THREAD_FLAGS) -DUSE_PEXT -MMD -MP $(PLATFORM_FLAGS)
LDFLAGS     := -lm

# ---------------------------------------------------------------------------
# Per-ISA march flags
# ---------------------------------------------------------------------------
MARCH_x86-64    := -march=x86-64
MARCH_x86-64-v2 := -march=x86-64-v2
MARCH_x86-64-v3 := -march=x86-64-v3
MARCH_native    := -march=native

# ---------------------------------------------------------------------------
# Build rule generator macro
# ---------------------------------------------------------------------------
# For each ISA tier we synthesise:
#   * A dedicated obj directory   obj/<arch>/
#   * A pattern rule              obj/<arch>/%.o : %.c
#   * A link rule                 $(OUTDIR)/NAME-<arch>[.exe]
#   * A phony shorthand           make x86-64-v3   (etc.)
#
# Invoked as: $(call ISA_RULES,<arch>)
# ---------------------------------------------------------------------------
define ISA_RULES
OBJS_$(1)   := $$(patsubst %.c, obj/$(1)/%.o, $(SRCS))
BIN_$(1)    := $(OUTDIR)/$(NAME)-$(1)$(EXE)
CFLAGS_$(1) := $(BASE_CFLAGS) $$(MARCH_$(1))

ALL_BINS += $$(BIN_$(1))

$$(BIN_$(1)): $$(OBJS_$(1)) | $(OUTDIR)
	$(CC) $$(CFLAGS_$(1)) $$(OBJS_$(1)) -o $$@ $(LDFLAGS)
	@echo "  LD  $$@"

obj/$(1)/%.o: %.c | obj/$(1)
	$(CC) $$(CFLAGS_$(1)) -c $$< -o $$@

obj/$(1):
	$(MKDIR) obj/$(1)

.PHONY: $(1)
$(1): $$(BIN_$(1))

endef

# Expand rules for every architecture tier
ARCHS := x86-64 x86-64-v2 x86-64-v3 native
$(foreach arch,$(ARCHS),$(eval $(call ISA_RULES,$(arch))))

# ---------------------------------------------------------------------------
# Default target – build all ISA tiers
# ---------------------------------------------------------------------------
.PHONY: all
.DEFAULT_GOAL := all
all: $(ALL_BINS)

# ---------------------------------------------------------------------------
# Output directory
# ---------------------------------------------------------------------------
$(OUTDIR):
ifeq ($(OS), Windows_NT)
	$(MKDIR) $(subst /,\,$(OUTDIR))
else
	$(MKDIR) $(OUTDIR)
endif

# ---------------------------------------------------------------------------
# Texel tuner
# Built from a separate object directory with -DTUNE so evaluation tunables
# are mutable there while engine objects stay const.
# Always compiled native for speed; -fopenmp enables parallel tuning.
# ---------------------------------------------------------------------------
TUNEOBJDIR  := obj/tune
TUNER_SRCS  := $(filter-out main.c, $(SRCS))
TUNER_OBJS  := $(patsubst %.c, $(TUNEOBJDIR)/%.o, $(TUNER_SRCS))
TUNE_CFLAGS := $(BASE_CFLAGS) $(MARCH_native) -fopenmp -DTUNE

$(OUTDIR)/tuner$(EXE): $(TUNER_OBJS) ../tools/tuner.c | $(OUTDIR)
	$(CC) $(TUNE_CFLAGS) -I. $(TUNER_OBJS) ../tools/tuner.c -o $@ $(LDFLAGS)
	@echo "  LD  $@"

$(TUNEOBJDIR)/%.o: %.c | $(TUNEOBJDIR)
	$(CC) $(TUNE_CFLAGS) -c $< -o $@

$(TUNEOBJDIR):
	$(MKDIR) $(TUNEOBJDIR)

.PHONY: tuner
tuner: $(OUTDIR)/tuner$(EXE)

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
.PHONY: clean
clean:
ifeq ($(OS), Windows_NT)
	-rmdir /S /Q obj bin\windows bin\linux 2>NUL
else
	$(RM) -rf obj $(OUTDIR) $(TUNEOBJDIR)
endif

# ---------------------------------------------------------------------------
# Rebuild
# ---------------------------------------------------------------------------
.PHONY: rebuild
rebuild: clean all

# ---------------------------------------------------------------------------
# Auto-generated header dependency includes (-MMD -MP)
# Rebuilds dependents whenever a header changes.
# ---------------------------------------------------------------------------
-include $(wildcard obj/*/*.d)
-include $(wildcard $(TUNEOBJDIR)/*.d)

# ---------------------------------------------------------------------------
# Legacy 'depend' target (kept for compatibility)
# ---------------------------------------------------------------------------
.PHONY: depend
depend: $(SRCS)
	$(CC) -MM $(SRCS) | sed 's|^\(.*\)\.o:|obj/x86-64/\1.o:|' > .depend
-include .depend