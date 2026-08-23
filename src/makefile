# Detect OS (assume Linux if not Windows)
ifeq ($(OS), Windows_NT)
    CC := gcc
    RM := del /Q
    MKDIR := mkdir
    PLATFORM_FLAGS := -DPLATFORM_WINDOWS
else
    CC := cc
    RM := rm -f
    MKDIR := mkdir -p
    PLATFORM_FLAGS := -DPLATFORM_LINUX
endif

# Project settings
SRCS := $(wildcard *.c)
OBJDIR := obj
BINDIR := bin
OBJS := $(patsubst %.c, $(OBJDIR)/%.o, $(SRCS))
NAMES := $(BINDIR)/GOOB-2.1
# -march=native: enables popcnt/BMI2 codegen for the host CPU.
# USE_PEXT: magic slider attacks use the single-instruction pext index
#   instead of mask/multiply/shift (guarded by __BMI2__ in attacks.c, so a
#   build on a non-BMI2 host silently falls back to classic magics).
# For a portable binary drop both: -march=x86-64-v3 also works on any
# AVX2/BMI2-class CPU without native tuning.
CFLAGS := -O3 -pthread -march=native -DUSE_PEXT -MMD -MP $(PLATFORM_FLAGS)
LDFLAGS := -lm

# Default target
all: $(NAMES)

# Linking the executable
$(NAMES): $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(OBJS) -o $(NAMES) $(LDFLAGS)

# Ensure obj/ directory exists and compile source files into object files
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create obj/ directory if it does not exist
$(OBJDIR):
	$(MKDIR) $(OBJDIR)

$(BINDIR):
	$(MKDIR) $(BINDIR)

# Cleaning build artifacts
clean:
ifeq ($(OS), Windows_NT)
	-for %f in ($(OBJS)) do del "%f"
	-del $(NAMES)
	-rmdir /S /Q $(OBJDIR) $(BINDIR)
else
	$(RM) $(OBJS) $(NAMES) $(BINDIR)/tuner
	$(RM) -rf $(OBJDIR) $(TUNEOBJDIR)
	$(RM) -rf $(BINDIR)
endif

# Rebuilding project
rebuild: clean all

# Texel tuner (lives in ../tools, uses the engine evaluation)
# Built from a separate object directory compiled with -DTUNE so the
# evaluation tunables are mutable there, while engine objects stay const.
TUNEOBJDIR := obj-tune
TUNER_SRCS := $(filter-out main.c, $(SRCS))
TUNER_OBJS := $(patsubst %.c, $(TUNEOBJDIR)/%.o, $(TUNER_SRCS))
TUNE_CFLAGS := $(CFLAGS) -fopenmp -DTUNE

$(BINDIR)/tuner: $(TUNER_OBJS) ../tools/tuner.c | $(BINDIR)
	$(CC) $(TUNE_CFLAGS) -I. $(TUNER_OBJS) ../tools/tuner.c -o $@ $(LDFLAGS)

$(TUNEOBJDIR)/%.o: %.c | $(TUNEOBJDIR)
	$(CC) $(TUNE_CFLAGS) -c $< -o $@

$(TUNEOBJDIR):
	$(MKDIR) $(TUNEOBJDIR)

tuner: $(BINDIR)/tuner

# Dependency management
depend: $(SRCS)
	$(CC) -MM $(SRCS) | sed 's|^\(.*\)\.o:|$(OBJDIR)/\1.o:|' > .depend

-include .depend
# auto-generated header deps (-MMD -MP): rebuild dependents when a header changes
-include $(wildcard $(OBJDIR)/*.d)
-include $(wildcard $(TUNEOBJDIR)/*.d)

.PHONY: all clean rebuild depend