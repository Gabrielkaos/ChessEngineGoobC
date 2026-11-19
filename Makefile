# =============================================================================
# GOOB Chess Engine - Root Makefile Forwarder
# =============================================================================

.PHONY: all clean rebuild pgo universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4 native apple-silicon win windows linux mac macos trace tune datagen
.DEFAULT_GOAL := default

default:
	$(MAKE) -C src

all clean rebuild pgo universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4 native apple-silicon win windows linux mac macos trace tune datagen:
	$(MAKE) -C src $@
