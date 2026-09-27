# =============================================================================
# GOOB Chess Engine - Root Makefile Forwarder
# =============================================================================

.PHONY: all clean rebuild pgo x86-64 x86-64-v2 x86-64-v3 native win windows linux

all clean rebuild pgo x86-64 x86-64-v2 x86-64-v3 native win windows linux:
	$(MAKE) -C src $@
