/*
 * BENCmouth - QEMU TCG plugin: count guest instructions retired
 *
 * QEMU ships plugin support and the header but no prebuilt plugins, so this is
 * the smallest thing that answers "how many instructions did the guest run".
 *
 * Counting happens per translation block rather than per instruction: the
 * translate callback records how many instructions the block contains, and the
 * execute callback adds that many. Same total, a fraction of the overhead.
 *
 * This is an instruction count, NOT a cycle count. QEMU models no wait states,
 * no flash accelerator and no bus contention, and its Cortex-M models are not
 * cycle accurate. See embedded/README.md.
 *
 *   gcc -shared -fPIC -O2 -o insn_count.so insn_count.c
 *   qemu-system-arm -plugin ./insn_count.so ...
 */

#include <stdio.h>
#include <stdint.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t insns;

static void tb_exec(unsigned int vcpu_index, void *udata)
{
    (void)vcpu_index;
    insns += (uint64_t)(uintptr_t)udata;
}

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    (void)id;
    qemu_plugin_register_vcpu_tb_exec_cb(tb, tb_exec, QEMU_PLUGIN_CB_NO_REGS,
                                         (void *)(uintptr_t)n);
}

static void at_exit(qemu_plugin_id_t id, void *p)
{
    (void)id; (void)p;
    fprintf(stderr, "INSNS %llu\n", (unsigned long long)insns);
    fflush(stderr);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    (void)info; (void)argc; (void)argv;
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
