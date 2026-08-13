#!/bin/bash
#
# BENCmouth - build the core for an emulated Cortex-M, run it, and report
# instructions per sample plus a checksum that must match the host build.
#
# Usage:  embedded/qemu/run.sh              all configurations
#         embedded/qemu/run.sh m3-fixed     just one
#         OPT=-O2 embedded/qemu/run.sh      at a different optimisation level
#         EXTRA=-DBM_NFORMANTS=3 embedded/qemu/run.sh
#
# Requires qemu-system-arm, arm-none-eabi-gcc and glib headers (for the plugin).
# Newlib, specs files and semihosting libraries are deliberately unused - see
# startup.c and shim.c.
#
# Three things here look like paranoia and are not. Each one cost a debugging
# round the first time:
#
#   cd to the repo root, then relative paths only. An absolute path containing
#   a space - and this repository lives in one - is shredded by the unquoted
#   word splitting that building a compiler command line from a variable needs.
#
#   qemu reads stdin under -nographic. Inside a `while read` loop that means it
#   eats the remaining configuration lines, and the loop resumes mid-line with
#   the fields shifted. Hence </dev/null on the qemu invocation.
#
#   The FPU is off at reset, which is startup.c's problem but shows up here as
#   a hard-float target that runs forever and reports nothing.

set -u

HERE_ABS="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE_ABS/../.." || exit 1

H=embedded/qemu
OUT="${TMPDIR:-/tmp}/bm-qemu-$$"
mkdir -p "$OUT" || exit 1
trap 'rm -rf "$OUT"' EXIT

CORE=$(ls src/core/*.c | grep -v dict_data)

# -ffp-contract=off on both sides of the comparison: without it the compiler is
# free to fuse a*b+c into a single rounding on ARM and not on x86, and the
# checksums would differ for a reason that is not a bug.
COMMON="-std=c99 -ffreestanding -ffp-contract=off -fno-common -Iinclude -Isrc/core -I$H"
HOSTCOMMON="-std=c99 -ffp-contract=off -Iinclude -Isrc/core -I$H"
OPT="${OPT:--Os}"
EXTRA="${EXTRA:-}"
RATE="${RATE:-22050}"

# name|machine|cpu|arch flags|core config
CONFIGS="
m3-fixed|mps2-an385|cortex-m3|-mcpu=cortex-m3 -mthumb -mfloat-abi=soft|-DBM_FIXED_POINT=1 -DBM_SAMPLE_FLOAT=0 -DBM_WITH_EFFECTS=0
m3-float|mps2-an385|cortex-m3|-mcpu=cortex-m3 -mthumb -mfloat-abi=soft|-DBM_SAMPLE_FLOAT=0 -DBM_WITH_EFFECTS=0
m4f-float|mps2-an386|cortex-m4|-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard|-DBM_SAMPLE_FLOAT=0 -DBM_WITH_EFFECTS=0
m4f-full|mps2-an386|cortex-m4|-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard|-DBM_SAMPLE_FLOAT=0
m7-float|mps2-an500|cortex-m7|-mcpu=cortex-m7 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard|-DBM_SAMPLE_FLOAT=0 -DBM_WITH_EFFECTS=0
"

PLUGIN="$OUT/insn_count.so"
gcc -shared -fPIC -O2 $(pkg-config --cflags glib-2.0 2>/dev/null) \
    -o "$PLUGIN" "$H/insn_count.c" || { echo "plugin build FAILED"; exit 1; }

host_run() {   # $1 = core config
    local cfg="$1" d="$OUT/host" f
    rm -rf "$d"; mkdir -p "$d"
    for f in $CORE; do
        gcc $HOSTCOMMON $OPT $cfg $EXTRA -c "$f" -o "$d/$(basename "$f" .c).o" \
            2>"$d/cc.err" || { echo "host compile FAILED $f"; return 1; }
    done
    gcc $HOSTCOMMON $OPT $cfg $EXTRA -DBENCH_RATE=$RATE "$H/bench.c" "$d"/*.o \
        -o "$d/bench" -lm 2>"$d/ld.err" || { echo "host link FAILED"; return 1; }
    "$d/bench"
}

# The core does not depend on BENCH_RENDER, so it is compiled once per
# configuration and both firmwares link against it. Only bench.c is built twice.
build_core() { # $1 name  $2 arch  $3 cfg
    local name="$1" arch="$2" cfg="$3" d="$OUT/$name" f
    rm -rf "$d"; mkdir -p "$d"
    for f in $CORE "$H/startup.c" "$H/shim.c"; do
        arm-none-eabi-gcc $COMMON $OPT $arch $cfg $EXTRA \
            -c "$f" -o "$d/$(basename "$f" .c).o" 2>"$d/cc.err" || {
                echo "compile FAILED $(basename "$f")"; sed 's/^/      /' "$d/cc.err" | head -3; return 1; }
    done
}

run_one() { # $1 name  $2 machine  $3 cpu  $4 arch  $5 cfg  $6 render
    local name="$1" machine="$2" cpu="$3" arch="$4" cfg="$5" render="$6"
    local d="$OUT/$name"

    # $EXTRA must reach bench.c too. BM_NFORMANTS sizes arrays inside bm_voice,
    # which bm_config embeds, which bench.c puts on its stack - build the two
    # halves with different values and the struct layouts disagree silently.
    arm-none-eabi-gcc $COMMON $OPT $arch $cfg $EXTRA -DBM_QEMU_TARGET=1 \
        -DBENCH_RATE=$RATE -DBENCH_RENDER=$render \
        -c "$H/bench.c" -o "$d/bench.o" 2>"$d/cc.err" || {
            echo "compile FAILED bench.c"; sed 's/^/      /' "$d/cc.err" | head -3; return 1; }

    arm-none-eabi-gcc $arch -nostdlib -nostartfiles -T "$H/mps2.ld" \
        -o "$d/fw.elf" "$d"/*.o -lgcc 2>"$d/ld.err" || {
            echo "link FAILED"; sed 's/^/      /' "$d/ld.err" | head -4; return 1; }

    echo "text=$(arm-none-eabi-size "$d/fw.elf" | awk 'NR==2{print $1}')"

    timeout 240 qemu-system-arm -M "$machine" -cpu "$cpu" -nographic -no-reboot \
        -semihosting-config enable=on,target=native \
        -plugin "$PLUGIN" -d plugin \
        -kernel "$d/fw.elf" </dev/null 2>&1
}

want="${1:-}"
echo
echo "opt=$OPT  rate=$RATE  |  text = firmware .text, engine = bm_engine_size()"
printf '%-11s %8s %8s %8s %9s %12s  %s\n' \
    CONFIG SAMPLES TEXT ENGINE INS/SMPL CHECKSUM VS-HOST
printf '%.0s-' {1..74}; echo

while IFS='|' read -r name machine cpu arch cfg; do
    [ -z "${name:-}" ] && continue
    [ -n "$want" ] && [ "$want" != "$name" ] && continue

    if ! out=$(build_core "$name" "$arch" "$cfg"); then
        printf '%-11s  %s\n' "$name" "core build failed"
        printf '%s\n' "$out" | sed 's/^/    /'
        continue
    fi

    full=$(run_one "$name" "$machine" "$cpu" "$arch" "$cfg" 1)
    if ! printf '%s\n' "$full" | grep -q '^samples='; then
        printf '%-11s  %s\n' "$name" "run failed"
        printf '%s\n' "$full" | sed 's/^/    /' | head -6
        continue
    fi
    base=$(run_one "$name" "$machine" "$cpu" "$arch" "$cfg" 0)

    samples=$(printf '%s\n' "$full" | sed -n 's/^samples=//p')
    engine=$( printf '%s\n' "$full" | sed -n 's/^engine_bytes=//p')
    sum=$(    printf '%s\n' "$full" | sed -n 's/^checksum=//p')
    text=$(   printf '%s\n' "$full" | sed -n 's/^text=//p')
    i_full=$( printf '%s\n' "$full" | sed -n 's/^INSNS //p')
    i_base=$( printf '%s\n' "$base" | sed -n 's/^INSNS //p')

    per="?"
    [ -n "${i_full:-}" ] && [ -n "${i_base:-}" ] && [ "$samples" -gt 0 ] && \
        per=$(( (i_full - i_base) / samples ))

    hostout=$(host_run "$cfg")
    hsum=$(printf '%s\n' "$hostout" | sed -n 's/^checksum=//p')
    verdict="** MISMATCH ${hsum:-none} **"
    [ "$hsum" = "$sum" ] && verdict="bit-exact"

    printf '%-11s %8s %8s %8s %9s %12s  %s\n' \
        "$name" "$samples" "${text:-?}" "$engine" "$per" "$sum" "$verdict"
done <<< "$CONFIGS"
echo
