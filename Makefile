# =============================================================================
# GOOB Chess Engine - Root Makefile Forwarder
# =============================================================================

.PHONY: default all clean rebuild pgo universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4 native apple-silicon win windows linux mac macos trace tune datagen cross-setup cross-build cross-test cross-shell
.DEFAULT_GOAL := default

default:
	$(MAKE) -C src

all clean rebuild pgo universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4 native apple-silicon win windows linux mac macos trace tune datagen:
	$(MAKE) -C src $@

# Cross-platform (macOS + Linux + Windows) build & test helpers via Docker/OrbStack
cross-setup:
	./tools/cross/cross.sh setup

cross-build:
	./tools/cross/cross.sh build all

cross-test:
	./tools/cross/cross.sh test

cross-shell:
	./tools/cross/cross.sh shell
