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

.PHONY: all lib bm demo speak dict audio wasm gui clean-objs clean test check-freestanding

# The bm CLI has no main yet; until it does, the demo is what you run.
all: lib bm demo speak

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
	rm -f *.exe mkdict $(DICT_DATA) $(WASM_OUT) $(GUI)

$(SPEAK): tools/speak_demo.c $(HOST_NOMAIN) $(LIB)
	@mkdir -p render
	$(CC) $(CFLAGS) -Isrc/core -Isrc/host -o $@ $< $(HOST_NOMAIN) $(LIB) $(LDFLAGS) -lm

# The CLI links every host object including main.c.
bm: $(BIN)

$(BIN): $(HOST_OBJ) $(LIB)
	$(CC) $(CFLAGS) -Isrc/core -Isrc/host -o $@ $(HOST_OBJ) $(LIB) $(LDFLAGS) -lm

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

mkdict: tools/mkdict.c $(LIB)
	$(CC) $(CFLAGS) -Isrc/core -o $@ $< $(LIB) $(LDFLAGS) -lm

$(DICT_DATA): mkdict $(DICT_SRC)
	./mkdict $(DICT_SRC) $@

dict: $(DICT_DATA)
	$(MAKE) clean-objs
	$(MAKE) all OPT="$(OPT) -DBM_WITH_DICT=1"

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

UNAME := $(shell uname -s)

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
	$(MAKE) all OPT="$(OPT) $(AUDIO_FLAGS)" LDFLAGS="$(LDFLAGS) $(AUDIO_LIBS)"

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
GUI_SRC  := src/gui/main.c src/gui/bm_ui.c

ifdef RAYLIB
  RL_CFLAGS := -I$(RAYLIB)/include
  RL_LIBS   := -L$(RAYLIB)/lib -lraylib
else
  RL_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
  RL_LIBS   := $(shell pkg-config --libs raylib 2>/dev/null || echo -lraylib)
endif

ifeq ($(UNAME),Darwin)
  RL_SYS := -framework Cocoa -framework IOKit -framework CoreVideo \
            -framework CoreAudio -framework OpenGL
else ifneq (,$(findstring MINGW,$(UNAME)))
  RL_SYS := -lopengl32 -lgdi32 -lwinmm
else ifneq (,$(findstring MSYS,$(UNAME)))
  RL_SYS := -lopengl32 -lgdi32 -lwinmm
else
  RL_SYS := -lGL -lm -lpthread -ldl -lrt -lX11
endif

gui: $(GUI)

$(GUI): $(GUI_SRC) $(HOST_NOMAIN) $(LIB)
	@mkdir -p render
	$(CC) $(CSTD) $(OPT) -Iinclude -Isrc/host -Isrc/gui $(RL_CFLAGS) \
	  -o $@ $(GUI_SRC) $(HOST_NOMAIN) $(LIB) $(RL_LIBS) $(RL_SYS) -lm

# Header dependencies emitted by -MMD. Silent if absent (first build).
-include $(CORE_OBJ:.o=.d) $(HOST_OBJ:.o=.d)
