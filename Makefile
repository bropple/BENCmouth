# BENCmouth

CC      ?= cc
AR      ?= ar
CSTD    ?= -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes \
           -Wmissing-prototypes -Wpointer-arith -Wcast-qual
# The GUI gets the two that find bugs and not the ones that find style. It was
# built with no warnings at all until an apostrophe written as a \u escape -
# which is not legal C, and which the compiler here happened to accept - broke
# the build on every runner at once. -Wpedantic would have caught it, and is left
# out anyway: the help and licence text are single string literals longer than
# the 4095 characters C99 obliges a compiler to take, so it fires constantly on
# something that is not wrong. Splitting those is a change to make deliberately,
# not as the price of turning on -Wall.
GUI_WARN := -Wall -Wextra
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

# bm_shm.c comes with it: the GUI is also the plugin's editor, and that mode
# attaches to the block the plugin created. Nothing else in src/plugin/ is
# needed here - spawning is the plugin's side of the arrangement.
GUI_SRC  := src/gui/main.c src/gui/bm_ui.c src/gui/bm_filedlg.c \
            src/gui/bm_song_ui.c src/gui/bm_roll_ui.c \
            src/plugin/bm_shm.c $(EMBED)

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
	$(CC) $(CSTD) $(GUI_WARN) $(OPT) -Iinclude -Isrc/host -Isrc/gui -Isrc/plugin $(RL_CFLAGS) \
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
ifeq (yes,$(CLAP_FOUND))
	@echo "== CLAP"
	$(MAKE) clap
else
	@echo "== CLAP: skipped - no headers. Run 'make clap-fetch'."
endif
	@echo
	@echo "everything built."

# ------------------------------------------------------------------ #
# CLAP
#
# The plugin is a score player: it owns the song, the host owns the transport,
# and the two meet at a position in seconds. See src/clap/bencmouth_clap.c.
#
# The headers are fetched rather than vendored in history - MIT, a tagged
# tarball, and an input the build can reproduce. `make clap-fetch` gets them.
# ------------------------------------------------------------------ #

# Read from the public header, so the bundle and the library cannot disagree
# about what version this is.
BM_VERSION := $(shell awk '/define BM_VERSION_MAJOR/{a=$$3} /define BM_VERSION_MINOR/{b=$$3} /define BM_VERSION_PATCH/{c=$$3} END{print a"."b"."c}' include/bencmouth.h)

CLAP_VERSION := 1.2.10
CLAP_INCLUDE ?= vendor/clap/include
CLAP_FOUND   := $(if $(wildcard $(CLAP_INCLUDE)/clap/clap.h),yes,)

# The host-layer pieces the plugin needs, which is everything except the CLI's
# own main and the audio backend: a plugin is given its samples to fill and
# must never open a device of its own.
CLAP_HOST_SRC := src/host/bm_songfile.c src/host/bm_roll.c \
                 src/host/bm_render.c src/host/bm_player.c \
                 src/plugin/bm_shm.c src/plugin/bm_spawn.c

# The core goes in as source rather than as libbencmouth.a. A shared object
# needs position-independent code throughout and the static library is not
# built that way - `ld` says so, in a message about relocations that takes a
# minute to recognise as "you cannot put a .a in a .so". Compiling the sources
# in also keeps the plugin one file with nothing to install beside it.
#
# The dictionary is deliberately not part of it. A score is phonemes, so the
# only thing that spells words is the editor, and that is a different process
# with its own copy - carrying 1.5 MB of compiled CMUdict into every plugin
# instance would be paying for a lookup this side never does.
CLAP_SRC      := src/clap/bencmouth_clap.c $(CLAP_HOST_SRC) \
                 $(filter-out src/core/bm_dict_data.c,$(CORE_SRC))

CW_VERSION   := v0.16.0
CW_DIR       := vendor/clap-wrapper
VST3_SDK_DIR := vendor/vst3sdk
VST3_SDK_COMMIT := 58f8da7936800732561402d7936584ca4505de07
AU_SDK_DIR   := vendor/AudioUnitSDK
VST3_BUILD   := build/vst3
AU_BUILD     := build/au

# The mini-host dlopen()s the bundle, so it needs whatever provides that.
CLAP_DL := -ldl
EXE     :=

ifeq ($(UNAME),Darwin)
  CLAP_BUNDLE := build/BENCmouth.clap
  CLAP_BINARY := $(CLAP_BUNDLE)/Contents/MacOS/BENCmouth
  # Both architectures, always. A runner - and most machines now - is Apple
  # Silicon, so a plain build is arm64-only and simply absent for anyone on an
  # Intel Mac. This is one of the three things that make macOS report a plugin
  # as damaged or missing, and BENCsynth shipped a release failing all three.
  MAC_ARCHS   := -arch x86_64 -arch arm64
  CLAP_SHARED := -dynamiclib $(MAC_ARCHS)
  CLAP_DL     :=
  CLAP_INSTALL_DIR ?= $(HOME)/Library/Audio/Plug-Ins/CLAP
else ifneq (,$(WINDOWS))
  CLAP_BUNDLE := build/BENCmouth.clap
  CLAP_BINARY := $(CLAP_BUNDLE)
  CLAP_SHARED := -shared
  # Static for the reason the GUI is: a host will not have MSYS2's runtime
  # DLLs on its PATH, and LoadLibrary failing looks exactly like the plugin
  # not being installed.
  CLAP_LINK   := -static-libgcc
  CLAP_DL     :=
  EXE         := .exe
  CLAP_INSTALL_DIR ?= $(LOCALAPPDATA)/Programs/Common/CLAP
else
  CLAP_BUNDLE := build/BENCmouth.clap
  CLAP_BINARY := $(CLAP_BUNDLE)
  CLAP_SHARED := -shared
  CLAP_INSTALL_DIR ?= $(HOME)/.clap
endif

.PHONY: clap clap-fetch clap-test clap-install editor-test

clap:
ifeq ($(CLAP_FOUND),yes)
	@$(MAKE) --no-print-directory $(CLAP_BINARY)
else
	@echo "  no CLAP headers at $(CLAP_INCLUDE)."
	@echo "  run 'make clap-fetch', or point CLAP_INCLUDE at the include/"
	@echo "  directory of a CLAP checkout."
	@false
endif

clap-fetch:
	mkdir -p vendor
	curl -sL -o /tmp/clap-$(CLAP_VERSION).tar.gz \
	    https://github.com/free-audio/clap/archive/refs/tags/$(CLAP_VERSION).tar.gz
	tar xzf /tmp/clap-$(CLAP_VERSION).tar.gz -C vendor
	rm -rf vendor/clap
	mv vendor/clap-$(CLAP_VERSION) vendor/clap
	@echo "CLAP $(CLAP_VERSION) headers in vendor/clap"

# -isystem, not -I: the CLAP headers use unnamed unions, which are C11 and
# which -Wpedantic reports on every file that includes them. They are correct
# and not ours to fix, and turning the warning off for our own code to silence
# theirs would be the wrong trade.
$(CLAP_BINARY): $(CLAP_SRC) include/bencmouth.h $(wildcard src/clap/Info.plist)
	@mkdir -p $(dir $(CLAP_BINARY))
	$(CC) $(filter-out -MMD -MP,$(CFLAGS)) -isystem $(CLAP_INCLUDE) \
	    -Isrc/core -Isrc/host -Isrc/plugin \
	    -fPIC -fvisibility=hidden $(CLAP_SHARED) $(CLAP_LINK) \
	    -o $(CLAP_BINARY) $(CLAP_SRC) -lm
ifeq ($(UNAME),Darwin)
	# A .clap on macOS is a bundle, and a bundle without an Info.plist is a
	# folder the host will not look inside. The version comes from the header
	# so there is one place it is written down.
	sed 's/@BM_VERSION@/$(BM_VERSION)/g' src/clap/Info.plist \
	    > $(CLAP_BUNDLE)/Contents/Info.plist
	# Ad-hoc, which identifies nobody and is not the point: unsigned arm64
	# code does not load at all, and the message a host shows when it refuses
	# says nothing about signatures. Notarization is a separate question.
	@command -v codesign >/dev/null 2>&1 && { \
	    codesign --force --sign - --timestamp=none $(CLAP_BUNDLE) && \
	    codesign --verify --deep --strict $(CLAP_BUNDLE) && \
	    echo "signed $(CLAP_BUNDLE)"; } || \
	    echo "warning: no codesign - the bundle will not load on Apple Silicon"
endif
	@echo "built $(CLAP_BUNDLE)"

# Loads it the way a host does and plays it. Compiling proves nothing about
# whether the transport lands on the right sample or whether a project's state
# survives a round trip; this does.
CLAP_TEST := bm_clap_test$(EXE)

clap-test: clap
	$(CC) $(filter-out -MMD -MP,$(CFLAGS)) -isystem $(CLAP_INCLUDE) \
	    -o $(CLAP_TEST) tools/clap_host.c $(CLAP_DL) -lm
	# A song out of songs/, which is in the repository. It was pointed at
	# render/ - which is generated and gitignored, so the file existed on the
	# machine this was written on and nowhere else, and every CI runner failed
	# the preset test on a file it had never had.
	./$(CLAP_TEST) $(CLAP_BINARY) songs/daisy.bmsong
	@rm -f $(CLAP_TEST)

# The plugin and the editor, against each other. Needs a display, because the
# editor is a real window - so it is its own target rather than part of
# clap-test, which needs nothing.
EDITOR_TEST := bm_editor_test$(EXE)

editor-test: clap $(GUI)
	$(CC) $(filter-out -MMD -MP,$(CFLAGS)) -isystem $(CLAP_INCLUDE) -Isrc/plugin \
	    -o $(EDITOR_TEST) tools/editor_test.c src/plugin/bm_shm.c $(CLAP_DL) -lm
	BENCMOUTH_EDITOR="$(CURDIR)/$(GUI)" ./$(EDITOR_TEST) $(CLAP_BINARY)
	@rm -f $(EDITOR_TEST)

# ------------------------------------------------------------------ #
# VST3, and AU on macOS.
#
# Both are wrappers around the CLAP rather than ports of anything: the shim
# finds BENCmouth.clap at runtime and loads it. So they always install together
# with it, and a shim on its own does nothing - which is worth knowing before
# debugging why a VST3 that is definitely installed does not appear.
#
# VST3 has been MIT since October 2025, so this is a build problem and not a
# licensing one.
# ------------------------------------------------------------------ #

ifeq ($(UNAME),Darwin)
  VST3_BUNDLE := build/BENCmouth.vst3
  AU_BUNDLE   := build/BENCmouth.component
  VST3_INSTALL_DIR ?= $(HOME)/Library/Audio/Plug-Ins/VST3
  AU_INSTALL_DIR   ?= $(HOME)/Library/Audio/Plug-Ins/Components
else ifneq (,$(WINDOWS))
  VST3_BUNDLE := build/BENCmouth.vst3
  VST3_INSTALL_DIR ?= $(COMMONPROGRAMFILES)/VST3
else
  VST3_BUNDLE := build/BENCmouth.vst3
  VST3_INSTALL_DIR ?= $(HOME)/.vst3
endif

.PHONY: vst3 vst3-fetch vst3-install vst3-test au au-fetch au-install

vst3-fetch:
	mkdir -p vendor
	rm -rf $(CW_DIR) $(VST3_SDK_DIR)
	git clone -q --depth 1 --branch $(CW_VERSION) \
	    https://github.com/free-audio/clap-wrapper.git $(CW_DIR)
	git clone -q --depth 1 https://github.com/steinbergmedia/vst3sdk.git $(VST3_SDK_DIR)
	# Pinned, not taken at tip. The SDK at 3.8.1 declares `iid` in more than
	# one base class, and every clap-wrapper up to v0.16.0 fails to compile
	# against it - dozens of "reference to 'iid' is ambiguous", none of which
	# name a version as the cause. This commit is the one the wrapper is known
	# to build against; move it when the wrapper moves, not before.
	cd $(VST3_SDK_DIR) && git fetch -q --depth 1 origin $(VST3_SDK_COMMIT) && \
	    git checkout -q FETCH_HEAD && \
	    git submodule update --init --depth 1 base pluginterfaces public.sdk cmake
	@echo "clap-wrapper $(CW_VERSION) and the VST3 SDK are in vendor/"

# clap-wrapper *and* Apple's AudioUnit SDK. Neither fetch implies the other,
# which is why cmake/CMakeLists.txt guards each wrapper on its own SDK.
au-fetch:
	mkdir -p vendor
	rm -rf $(AU_SDK_DIR)
	test -d $(CW_DIR) || git clone -q --depth 1 --branch $(CW_VERSION) \
	    https://github.com/free-audio/clap-wrapper.git $(CW_DIR)
	git clone -q --depth 1 https://github.com/apple/AudioUnitSDK.git $(AU_SDK_DIR)
	@echo "clap-wrapper $(CW_VERSION) and the AudioUnit SDK are in vendor/"

vst3:
ifeq ($(CLAP_FOUND),yes)
	@test -d $(CW_DIR) || { echo "  run 'make vst3-fetch' first."; false; }
	cmake -S cmake -B $(VST3_BUILD) -DCMAKE_BUILD_TYPE=Release \
	    -DCLAP_WRAPPER_DIR="$(CURDIR)/$(CW_DIR)" \
	    -DCLAP_SDK_ROOT="$(CURDIR)/vendor/clap" \
	    -DBM_VERSION="$(BM_VERSION)" \
	    -DVST3_SDK_ROOT="$(CURDIR)/$(VST3_SDK_DIR)"
	cmake --build $(VST3_BUILD) --target bencmouth_as_vst3 --config Release -j
	rm -rf $(VST3_BUNDLE)
	cp -r "$$(find $(VST3_BUILD) -name 'BENCmouth.vst3' -maxdepth 4 | head -1)" $(VST3_BUNDLE)
	# The module inside the bundle has to be named after the bundle, or a host
	# opens it, finds nothing it recognises, and lists no plugin - with no error
	# anywhere. MinGW shipped a libBENCmouth.vst3 exactly once.
	@test -n "$$(find $(VST3_BUNDLE)/Contents -type f -name 'BENCmouth.*')" || { \
	    echo "  the module in $(VST3_BUNDLE) is not named BENCmouth - no host will load it:"; \
	    find $(VST3_BUNDLE) -type f; false; }
	@echo "built $(VST3_BUNDLE)  (loads BENCmouth.clap at run time)"
else
	@echo "  no CLAP headers - run 'make clap-fetch' first."
	@false
endif

au:
ifeq ($(UNAME),Darwin)
	@test -d $(AU_SDK_DIR) || { echo "  run 'make au-fetch' first."; false; }
	cmake -S cmake -B $(AU_BUILD) -DCMAKE_BUILD_TYPE=Release \
	    -DCLAP_WRAPPER_DIR="$(CURDIR)/$(CW_DIR)" \
	    -DCLAP_SDK_ROOT="$(CURDIR)/vendor/clap" \
	    -DBM_VERSION="$(BM_VERSION)" \
	    -DAUDIOUNIT_SDK_ROOT="$(CURDIR)/$(AU_SDK_DIR)"
	cmake --build $(AU_BUILD) --target bencmouth_as_auv2 --config Release -j
	rm -rf $(AU_BUNDLE)
	cp -r "$$(find $(AU_BUILD) -name 'BENCmouth.component' -maxdepth 4 | head -1)" $(AU_BUNDLE)
	@echo "built $(AU_BUNDLE)"
else
	@echo "  AU is a macOS format."
	@false
endif

# Asks the built shim what it contains, the way a host would. A wrapper that
# builds and finds nothing has an empty factory, which in a DAW is
# indistinguishable from a plugin that failed to install.
#
# Needs the CLAP installed where the wrapper looks, which is what clap-install
# does - so it depends on it rather than assuming somebody remembered.
VST3_TEST := bm_vst3_test$(EXE)

vst3-test: vst3 clap-install
	$(CC) $(filter-out -MMD -MP,$(CFLAGS)) -o $(VST3_TEST) tools/vst3_probe.c $(CLAP_DL)
	./$(VST3_TEST) "$$(find $(VST3_BUNDLE) -name '*.so' -o -name '*.dll' | head -1)"
	@rm -f $(VST3_TEST)

# The shims go beside nothing; the CLAP is what they load, so installing one
# without the other is the mistake worth making impossible.
vst3-install: vst3 clap-install
	mkdir -p "$(VST3_INSTALL_DIR)"
	rm -rf "$(VST3_INSTALL_DIR)/$(notdir $(VST3_BUNDLE))"
	cp -R $(VST3_BUNDLE) "$(VST3_INSTALL_DIR)/"
	@echo "installed to $(VST3_INSTALL_DIR)"

au-install: au clap-install
	mkdir -p "$(AU_INSTALL_DIR)"
	rm -rf "$(AU_INSTALL_DIR)/$(notdir $(AU_BUNDLE))"
	cp -R $(AU_BUNDLE) "$(AU_INSTALL_DIR)/"
	@echo "installed to $(AU_INSTALL_DIR)"

clap-install: clap
	mkdir -p "$(CLAP_INSTALL_DIR)"
	rm -rf "$(CLAP_INSTALL_DIR)/$(notdir $(CLAP_BUNDLE))"
	cp -R $(CLAP_BUNDLE) "$(CLAP_INSTALL_DIR)/"
	@echo "installed to $(CLAP_INSTALL_DIR)"

# Header dependencies emitted by -MMD. Silent if absent (first build).
-include $(CORE_OBJ:.o=.d) $(HOST_OBJ:.o=.d)
