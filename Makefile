# =============================================================================
# GOOB Chess Engine - Root Makefile Forwarder
# =============================================================================

.PHONY: all clean rebuild pgo universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4 native win windows linux trace
.DEFAULT_GOAL := universal

all clean rebuild pgo universal x86-64 x86-64-v2 x86-64-v3 x86-64-v4 native win windows linux trace:
	$(MAKE) -C src $@
