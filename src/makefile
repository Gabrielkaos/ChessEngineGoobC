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
NAMES := $(BINDIR)/GOOB_BETA
CFLAGS := -O3 -pthread $(PLATFORM_FLAGS)
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
	$(RM) $(OBJS) $(NAMES)
	$(RM) -rf $(OBJDIR)
	$(RM) -rf $(BINDIR)
endif

# Rebuilding project
rebuild: clean all

# Dependency management
depend: $(SRCS)
	$(CC) -MM $(SRCS) | sed 's|^\(.*\)\.o:|$(OBJDIR)/\1.o:|' > .depend

-include .depend

.PHONY: all clean rebuild depend