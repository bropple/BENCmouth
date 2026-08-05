# BENCmouth

CC      ?= cc
AR      ?= ar
CSTD    ?= -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes \
           -Wmissing-prototypes -Wpointer-arith -Wcast-qual
OPT     ?= -O2
# -MMD -MP emits a .d file per object listing the headers it included, and the
# -include below feeds those back to make. Without this, editing a header
# recompiles nothing: objects keep a stale view of any struct it defines, and
# two translation units end up disagreeing about a struct's size. That failure
# is silent, and it presents as impossible behaviour rather than as a build
# error - it cost an afternoon once already.
CFLAGS  += $(CSTD) $(WARN) $(OPT) -Iinclude -MMD -MP
LDFLAGS +=

CORE_SRC := $(wildcard src/core/*.c)
HOST_SRC := $(wildcard src/host/*.c)
CORE_OBJ := $(CORE_SRC:.c=.o)
HOST_OBJ := $(HOST_SRC:.c=.o)
# Demos supply their own main, so they link the host objects except main.o.
HOST_NOMAIN := $(filter-out src/host/main.o,$(HOST_OBJ))

LIB  := libbencmouth.a
BIN  := bm
DEMO  := vowel_demo
SPEAK := speak_demo

.PHONY: all default lib demo speak dict audio wasm gui gui-info clean-objs clean test check-freestanding gui-dict

# `make` on its own builds this: the library, the CLI and the two demos, with a
# C compiler and nothing else. That property is load-bearing - it is what lets
# someone clone this and have it work - so it stays the default goal even though
# `all` now means more.
.DEFAULT_GOAL := default

default: lib bm demo speak

lib: $(LIB)

demo: $(DEMO)

speak: $(SPEAK)

$(LIB): $(CORE_OBJ)
	$(AR) rcs $@ $^

$(DEMO): tools/vowel_demo.c $(HOST_NOMAIN) $(LIB)
	@mkdir -p render
	$(CC) $(CFLAGS) -Isrc/core -Isrc/host -o $@ $< $(HOST_NOMAIN) $(LIB) $(LDFLAGS) -lm

# The core must never reach for the host. If this fires, the embedded build is
# already broken and you would not otherwise find out until you cross-compiled.
check-freestanding:
	@! grep -rnE '#[[:space:]]*include[[:space:]]*<(stdio|stdlib|math|string)\.h>' src/core/ \
		|| (echo "core/ must not include hosted headers"; exit 1)
	@echo "core is freestanding-clean"

# Built into the working directory rather than /tmp: MinGW appends .exe to an
# extensionless -o target, so a hardcoded /tmp/bm_test compiles but then cannot
# be executed by that name on Windows.
test: $(LIB)
	@for t in tests/test_*.c; do \
		echo "== $$t"; \
		$(CC) $(CFLAGS) -Isrc/core -o bm_test $$t $(LIB) -lm && ./bm_test || exit 1; \
	done
	@rm -f bm_test bm_test.exe

clean:
	rm -f $(CORE_OBJ) $(HOST_OBJ) $(CORE_OBJ:.o=.d) $(HOST_OBJ:.o=.d) \
	      $(LIB) $(BIN) $(DEMO) $(SPEAK) bm_test
	rm -f *.exe mkdict mkembed $(DICT_DATA) $(EMBED) $(WASM_OUT) $(GUI) src/gui/*.res.o

$(SPEAK): tools/speak_demo.c $(HOST_NOMAIN) $(LIB)
	@mkdir -p render
	$(CC) $(CFLAGS) -Isrc/core -Isrc/host -o $@ $< $(HOST_NOMAIN) $(LIB) $(LDFLAGS) -lm

# The CLI links every host object including main.c.
# No `bm: $(BIN)` alias here: BIN is literally "bm", so the rule read
# `bm: bm` and make said so on every build. The $(BIN) rule below is the
# target, and `make bm` finds it by name.

ifneq (,$(WINDOWS))
  BIN_LINK := -static
else
  BIN_LINK :=
endif

$(BIN): $(HOST_OBJ) $(LIB)
	$(CC) $(CFLAGS) -Isrc/core -Isrc/host -o $@ $(HOST_OBJ) $(LIB) \
	  $(LDFLAGS) $(BIN_LINK) -lm

# ---------------------------------------------------------------------------
# Optional CMU dictionary.
#
# `make dict` compiles ref/cmudict-0.7b.txt into src/core/bm_dict_data.c and
# rebuilds everything with -DBM_WITH_DICT=1. The generated file is ~1.5 MB of
# C and is deliberately not committed - it is fully reproducible from an input
# that is.
#
# Not the default: the data is far too large for the microcontroller targets
# the core exists to support, and the letter-to-sound rules are that path.
# ---------------------------------------------------------------------------

DICT_SRC  := ref/cmudict-0.7b.txt
DICT_DATA := src/core/bm_dict_data.c

# Built from the core sources rather than from libbencmouth.a, which looks like
# the long way round and is not. The library contains bm_dict_data.o once the
# dictionary has been generated, so linking mkdict against it makes the
# generated file depend on itself:
#
#   bm_dict_data.c -> mkdict -> libbencmouth.a -> bm_dict_data.o -> bm_dict_data.c
#
# make drops the cycle, and then compiles bm_dict_data.o with no input file.
# The tree hid it: `make clean` removes the generated file, so a first build
# always worked and only a second `make dict` failed.
#
# mkdict needs the phoneme table, not the dictionary - it is what generates the
# dictionary - so the generated file is exactly what to leave out.
MKDICT_SRC := $(filter-out $(DICT_DATA),$(CORE_SRC))

mkdict: tools/mkdict.c $(MKDICT_SRC)
	$(CC) $(CFLAGS) -Isrc/core -o $@ tools/mkdict.c $(MKDICT_SRC) $(LDFLAGS) -lm

$(DICT_DATA): mkdict $(DICT_SRC)
	./mkdict $(DICT_SRC) $@

dict: $(DICT_DATA)
	$(MAKE) clean-objs
	$(MAKE) default OPT="$(OPT) -DBM_WITH_DICT=1"

# Objects only - keeps the generated dictionary, which takes a while to build.
clean-objs:
	rm -f $(CORE_OBJ) $(HOST_OBJ) $(CORE_OBJ:.o=.d) $(HOST_OBJ:.o=.d) $(LIB)

# ---------------------------------------------------------------------------
# Optional live audio output.
#
# `make audio` picks a backend from uname and rebuilds. Optional for the same
# reason the dictionary is: a plain `make` should need nothing but a C compiler,
# and ALSA headers are a package a first-time builder should not have to find.
# ---------------------------------------------------------------------------

UNAME  := $(shell uname -s)

# What the compiler is actually targeting. More reliable than uname - uname
# describes the shell you are standing in, not the object format you are
# producing - and it stays correct when cross-compiling.
TRIPLE := $(shell $(CC) -dumpmachine 2>/dev/null)
WINDOWS := $(findstring mingw,$(TRIPLE))

ifeq ($(UNAME),Darwin)
  AUDIO_FLAGS := -DBM_AUDIO_COREAUDIO
  AUDIO_LIBS  := -framework AudioToolbox -framework CoreFoundation
else ifneq (,$(findstring MINGW,$(UNAME)))
  AUDIO_FLAGS := -DBM_AUDIO_WINMM
  AUDIO_LIBS  := -lwinmm
else ifneq (,$(findstring MSYS,$(UNAME)))
  AUDIO_FLAGS := -DBM_AUDIO_WINMM
  AUDIO_LIBS  := -lwinmm
else
  AUDIO_FLAGS := -DBM_AUDIO_ALSA
  AUDIO_LIBS  := -lasound
endif

audio:
	$(MAKE) clean-objs
	$(MAKE) default OPT="$(OPT) $(AUDIO_FLAGS)" LDFLAGS="$(LDFLAGS) $(AUDIO_LIBS)"

# ---------------------------------------------------------------------------
# WebAssembly.
#
# Bare wasm32 with no libc and no Emscripten runtime, which is only possible
# because the core is freestanding - the same property `make check-freestanding`
# has been guarding all along. Needs clang and lld; nothing else does.
# ---------------------------------------------------------------------------

WASM_CC    ?= clang
WASM_OUT   := bencmouth.wasm
WASM_EXPORTS := \
  -Wl,--export=bm_wasm_init \
  -Wl,--export=bm_wasm_text_buffer \
  -Wl,--export=bm_wasm_text_capacity \
  -Wl,--export=bm_wasm_output_buffer \
  -Wl,--export=bm_wasm_output_capacity \
  -Wl,--export=bm_wasm_set_voice \
  -Wl,--export=bm_wasm_set_param \
  -Wl,--export=bm_wasm_set_effects \
  -Wl,--export=bm_wasm_set_fx_param \
  -Wl,--export=bm_wasm_set_markup \
  -Wl,--export=bm_wasm_speak \
  -Wl,--export=bm_wasm_speak_phonemes \
  -Wl,--export=bm_wasm_to_phonemes \
  -Wl,--export=bm_wasm_read \
  -Wl,--export=bm_wasm_speaking \
  -Wl,--export=bm_wasm_sample_rate \
  -Wl,--export=bm_wasm_engine_size

wasm: $(WASM_OUT)

$(WASM_OUT): $(CORE_SRC) src/wasm/bm_wasm.c
	$(WASM_CC) --target=wasm32 -std=c99 -O2 -ffreestanding -nostdlib \
	  $(WARN) -Iinclude -Isrc/core \
	  -Wl,--no-entry -Wl,--initial-memory=1048576 $(WASM_EXPORTS) \
	  -o $@ $(CORE_SRC) src/wasm/bm_wasm.c
	@echo "  $(WASM_OUT): $$(wc -c < $(WASM_OUT)) bytes"

# ---------------------------------------------------------------------------
# GUI.
#
# The only part of this project with a third-party dependency, and it is
# confined here: `make`, `make test` and `make check-freestanding` all keep
# working with raylib absent from the machine entirely.
#
#   RAYLIB=/some/prefix make gui     if raylib is not on the default paths
# ---------------------------------------------------------------------------

GUI      := bencmouth-gui
# Assets live in the binary, not beside it - see src/gui/bm_embed.h.
EMBED     := src/gui/bm_embed.c
EMBED_IN  := assets/fonts/TerminusTTF.ttf assets/brand/BENCO_Logo_Terminal.png \
             assets/icon/hex-64.png LICENSE NOTICE assets/fonts/OFL.txt

GUI_SRC  := src/gui/main.c src/gui/bm_ui.c src/gui/bm_filedlg.c \
            src/gui/bm_song_ui.c $(EMBED)

# Windows: embed the icon, and link as a GUI subsystem binary so double-clicking
# it does not also open a console behind the window. Both apply to the GUI only -
# bm.exe is a console program by design and carries no icon.
ifneq (,$(WINDOWS))
  WINDRES  ?= windres
  GUI_RES  := src/gui/bencmouth.res.o
  # -mwindows: no console behind the window.
  # -static:   a downloaded binary must not need MSYS2's DLLs on PATH, and
  #            that includes libgcc and libwinpthread, not just raylib.
  # comdlg32 is the standard save dialog. Part of Windows, not a new
  # dependency - mingw has had the import library forever.
  GUI_LINK := -mwindows -static -lcomdlg32
else
  GUI_RES  :=
  GUI_LINK :=
endif

src/gui/bencmouth.res.o: src/gui/bencmouth.rc assets/icon/bencmouth.ico
	$(WINDRES) -I. $< -O coff -o $@

# `--static` matters more than it looks. It makes pkg-config emit Libs.private,
# which is where raylib declares what *it* needs - GLFW, OpenGL, and the
# platform libraries. Without it you get a bare -lraylib, and a static
# libraylib.a then fails to link with undefined __imp_glfw* symbols, because
# nothing ever supplied GLFW.
#
# The fallback names -lglfw3 explicitly since there is no pkg-config to ask.
# That is a guess, and it is only correct when raylib was built against a system
# GLFW rather than its bundled copy - which is why pkg-config is tried first
# rather than being the afterthought.
# Whether a separate GLFW is needed depends on how raylib was built, and there
# is no way to know from here except to look.
#
# raylib bundles GLFW: build it from source and those objects end up inside
# libraylib.a, so `-lraylib` alone links. A distro package may instead build
# against the system GLFW, leaving libraylib.a with dangling __imp_glfw*
# imports that something else has to satisfy - which is the failure this
# guards against.
#
# `$(CC) -print-file-name=X` answers the only question that matters: it returns
# a path when the library exists and echoes the bare name when it does not. So
# the flag is added when it can be, and omitted when adding it would fail with
# "cannot find -lglfw3" instead - trading one link error for another.
glfw_exists = $(if $(findstring /,$(shell $(CC) -print-file-name=lib$(1).a 2>/dev/null)),-l$(1),)
GLFW_LIB := $(strip $(call glfw_exists,glfw3)$(call glfw_exists,glfw))

ifdef RAYLIB
  RL_CFLAGS := -I$(RAYLIB)/include
  # Name the archive outright when it is there, rather than leaving it to -l.
  # On macOS the linker prefers a .dylib over a .a sitting in the same
  # directory, and the resulting binary then needs a raylib the person who
  # downloaded it has no reason to have installed - it aborts at launch with a
  # dyld error naming a Homebrew path. `make gui` on a developer's machine can
  # link dynamically all it likes; a build that names a prefix is asking for
  # that prefix's static library.
  RL_STATIC := $(wildcard $(RAYLIB)/lib/libraylib.a)
  RL_LIBS   := $(if $(RL_STATIC),$(RL_STATIC),-L$(RAYLIB)/lib -lraylib)
else
  RL_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
  # --static emits Libs.private, where raylib declares GLFW and friends.
  RL_LIBS   := $(shell pkg-config --libs --static raylib 2>/dev/null)
  ifeq (,$(RL_LIBS))
    RL_LIBS := -lraylib $(firstword $(GLFW_LIB))
  endif
endif

# Keyed off the compiler triple like everything else, not uname.
ifneq (,$(WINDOWS))
  RL_SYS := -lopengl32 -lgdi32 -lwinmm
else ifeq ($(UNAME),Darwin)
  RL_SYS := -framework Cocoa -framework IOKit -framework CoreVideo \
            -framework CoreAudio -framework OpenGL
else
  RL_SYS := -lGL -lm -lpthread -ldl -lrt -lX11
endif

# Prints what the build decided. `make gui-info` is the first thing to run when
# a Windows binary comes out without an icon.
gui-info:
	@echo "  CC       = $(CC)"
	@echo "  triple   = $(TRIPLE)"
	@echo "  windows  = $(if $(WINDOWS),yes,no)"
	@echo "  GUI_RES  = $(if $(GUI_RES),$(GUI_RES),(none - no icon will be embedded))"
	@echo "  GUI_LINK = $(GUI_LINK)"
	@echo "  RL_CFLAGS= $(RL_CFLAGS)"
	@echo "  RL_LIBS  = $(if $(RL_LIBS),$(RL_LIBS),(empty - pkg-config found nothing))"
	@echo "  RL_SYS   = $(RL_SYS)"
	@echo "  GLFW     = $(if $(GLFW_LIB),$(firstword $(GLFW_LIB)) (found),none found - assuming raylib bundles it)"

mkembed: tools/mkembed.c
	$(CC) $(CFLAGS) -o $@ $<

# Makefile is a prerequisite because the list of symbols lives here: adding an
# asset changes what the generated file must contain without changing any of
# the files it is generated from, and make would otherwise call it up to date.
$(EMBED): mkembed $(EMBED_IN) Makefile
	./mkembed $@ \
	  BM_FONT_TTF     assets/fonts/TerminusTTF.ttf \
	  BM_LOGO_PNG     assets/brand/BENCO_Logo_Terminal.png \
	  BM_ICON_PNG     assets/icon/hex-64.png \
	  BM_LICENSE_MIT  LICENSE \
	  BM_NOTICE       NOTICE \
	  BM_LICENSE_OFL  assets/fonts/OFL.txt

gui: $(GUI)

# The GUI with the dictionary compiled in.
#
# This exists because `make dict && make gui` only worked by luck. The gui
# target links whatever libbencmouth.a happens to be sitting in the tree, so
# whether the window pronounced "robot" as R OW B AA T or R AA B AA T came down
# to which target had been run last - and the CI jobs that publish the GUI
# never ran `make dict` at all, so every artifact was the rules-only build. A
# named target says what it builds instead of depending on the order somebody
# typed things in.
gui-dict: $(DICT_DATA)
	$(MAKE) clean-objs
	$(MAKE) gui OPT="$(OPT) -DBM_WITH_DICT=1"

$(GUI): $(GUI_SRC) $(GUI_RES) $(HOST_NOMAIN) $(LIB)
	$(CC) $(CSTD) $(OPT) -Iinclude -Isrc/host -Isrc/gui $(RL_CFLAGS) \
	  -o $@ $(GUI_SRC) $(GUI_RES) $(HOST_NOMAIN) $(LIB) \
	  $(RL_LIBS) $(RL_SYS) $(GUI_LINK) -lm

# ---------------------------------------------------------------------------
# Everything.
#
# `make` builds the part that needs nothing but a C compiler. `make all` builds
# the lot: dictionary, live audio, GUI and wasm, each in its best configuration.
#
# The two are separate targets on purpose. A plain `make` that needed raylib,
# clang and ALSA headers would stop being the thing you can run straight after a
# clone, and that is the property this project is least willing to give up - so
# `default` stays the default goal and `all` is the one you ask for.
#
# The optional pieces are *tested for* rather than assumed, and a missing one is
# reported and skipped rather than failing the build. A convenience target that
# cannot run on a machine without raylib would not be much of a convenience,
# and silently doing less than it said would be worse than either.
#
# Order matters, and not obviously. The GUI links the host objects, one of which
# is bm_audio.o - so a GUI linked against objects compiled for ALSA needs
# -lasound on its own link line, which it does not have. Building the GUI first
# (via gui-dict, which clears the objects itself) and the audio-enabled CLI
# second keeps them apart, and leaves the tree holding the CLI build, which is
# the more useful thing to find there afterwards.
# ---------------------------------------------------------------------------

# Does this machine have what the optional pieces need? Asked, not assumed.
HAVE_RAYLIB := $(if $(RAYLIB),yes,$(shell pkg-config --exists raylib 2>/dev/null && echo yes))
ifeq (,$(HAVE_RAYLIB))
  HAVE_RAYLIB := $(if $(findstring /,$(shell $(CC) -print-file-name=libraylib.a 2>/dev/null)),yes,)
endif

HAVE_WASM_CC := $(shell command -v $(WASM_CC) >/dev/null 2>&1 && echo yes)

# Only Linux needs a package for this; the macOS and Windows backends are part
# of the system. `-E` on an empty translation unit that includes the header is
# the question actually being asked: can this compiler find it?
ifeq ($(UNAME),Linux)
  HAVE_AUDIO := $(shell echo '\#include <alsa/asoundlib.h>' | \
                  $(CC) -E -xc - >/dev/null 2>&1 && echo yes)
else
  HAVE_AUDIO := yes
endif

ifeq (yes,$(HAVE_AUDIO))
  ALL_AUDIO_FLAGS := $(AUDIO_FLAGS)
  ALL_AUDIO_LIBS  := $(AUDIO_LIBS)
  ALL_AUDIO_NOTE  := with live audio
else
  ALL_AUDIO_FLAGS :=
  ALL_AUDIO_LIBS  :=
  ALL_AUDIO_NOTE  := without live audio - no ALSA headers found
endif

all:
	@echo "== dictionary"
	$(MAKE) $(DICT_DATA)
ifeq (yes,$(HAVE_RAYLIB))
	@echo "== GUI, with the dictionary"
	$(MAKE) gui-dict
else
	@echo "== GUI: skipped - no raylib found. See 'make gui-info'."
endif
	@echo "== library, CLI and demos: dictionary, $(ALL_AUDIO_NOTE)"
	$(MAKE) clean-objs
	$(MAKE) default OPT="$(OPT) -DBM_WITH_DICT=1 $(ALL_AUDIO_FLAGS)" \
	                LDFLAGS="$(LDFLAGS) $(ALL_AUDIO_LIBS)"
ifeq (yes,$(HAVE_WASM_CC))
	@echo "== wasm"
	$(MAKE) wasm
else
	@echo "== wasm: skipped - $(WASM_CC) not found."
endif
	@echo
	@echo "everything built."

# Header dependencies emitted by -MMD. Silent if absent (first build).
-include $(CORE_OBJ:.o=.d) $(HOST_OBJ:.o=.d)
