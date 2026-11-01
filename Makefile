# MDX-4 — top-level build wiring.
#
# Delegates the actual ROM build to the vendored SGDK makefile, which
# auto-detects GDK from its own path (see vendor/sgdk/makefile.gen).
GDK_WIN := vendor/sgdk
include $(GDK_WIN)/makefile.gen

# The vendored makefile only defines FLAGS / CFLAGS / AFLAGS / LIBMD as
# target-specific variables on the 'release' / 'debug' / 'asm' targets. We build
# 'out/rom.bin' directly (to avoid the Java-based sizebnd step that 'release'
# runs), so define the SGDK compile/link flags and runtime library globally.
# These mirror the 'release' target's optimisation level without LTO (the LTO
# link step spawns lto1, which is fragile under the bundled Windows toolchain;
# the vendored link rule keeps -flto but harmlessly no-ops without LTO IR).
EXTRA_FLAGS := -Isrc/audio
FLAGS   = $(DEFAULT_FLAGS) -O3 -fomit-frame-pointer
CFLAGS  = $(FLAGS)
AFLAGS  = $(FLAGS)
LIBMD   := $(LIB)/libmd.a
