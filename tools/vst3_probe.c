/*
 * Does the VST3 shim actually find the plugin?
 *
 * `make vst3-test` builds this and points it at the built bundle. That the
 * wrapper compiles says nothing about the thing most likely to be wrong: a
 * VST3 built this way contains no BENCmouth at all - it looks for
 * BENCmouth.clap at run time, in the places a host would - so a wrapper that
 * builds perfectly and finds nothing produces a plugin whose factory is empty.
 * In a DAW that looks exactly like a plugin that failed to install.
 *
 * VST3's factory is a COM interface, which is a vtable of function pointers in
 * a fixed order. Declaring that order in C is all a host needs to enumerate
 * what is inside a module, and it is much less code than it sounds.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

typedef int32_t  tresult;
typedef char     tuid[16];

/* PClassInfo, laid out as the SDK declares it. */
typedef struct {
    char    cid[16];
    int32_t cardinality;
    char    category[32];
    char    name[64];
} class_info;

/* IPluginFactory: FUnknown's three, then the factory's four, in declaration
 * order - which is the order they sit in the vtable. */
typedef struct factory_vtbl {
    tresult  (*queryInterface)(void *self, const tuid iid, void **obj);
    uint32_t (*addRef)(void *self);
    uint32_t (*release)(void *self);
    tresult  (*getFactoryInfo)(void *self, void *info);
    int32_t  (*countClasses)(void *self);
    tresult  (*getClassInfo)(void *self, int32_t index, class_info *info);
    tresult  (*createInstance)(void *self, const char *cid, const char *iid,
                               void **obj);
} factory_vtbl;

typedef struct { factory_vtbl *vtbl; } factory;

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1]
                     : "build/BENCmouth.vst3/Contents/x86_64-linux/BENCmouth.so";
    void *lib;
    int  (*module_entry)(void *);
    factory *(*get_factory)(void);
    factory *f;
    int32_t  n, i, found = 0;

    printf("\nBENCmouth VST3 probe\n\n  %s\n\n", path);

    lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (lib == 0) { printf("  cannot load: %s\n\nFAILED\n\n", dlerror()); return 1; }

    /* Linux modules are initialized through ModuleEntry, and a factory asked
     * for before that has not had a chance to look for anything. */
    *(void **)&module_entry = dlsym(lib, "ModuleEntry");
    if (module_entry != 0 && !module_entry(lib)) {
        printf("  ModuleEntry failed\n\nFAILED\n\n");
        return 1;
    }

    *(void **)&get_factory = dlsym(lib, "GetPluginFactory");
    if (get_factory == 0) {
        printf("  no GetPluginFactory - this is not a VST3\n\nFAILED\n\n");
        return 1;
    }

    f = get_factory();
    if (f == 0 || f->vtbl == 0) {
        printf("  the factory is null\n\nFAILED\n\n");
        return 1;
    }

    n = f->vtbl->countClasses((void *)f);
    printf("  the factory offers %d class%s\n", n, n == 1 ? "" : "es");

    for (i = 0; i < n; i++) {
        class_info info;
        memset(&info, 0, sizeof info);
        if (f->vtbl->getClassInfo((void *)f, i, &info) != 0) continue;
        printf("    %-28s %s\n", info.name, info.category);
        if (strstr(info.name, "BENCmouth") != 0) found = 1;
    }

    if (n == 0) {
        printf("\n  An empty factory means the wrapper did not find "
               "BENCmouth.clap.\n"
               "  It searches the standard CLAP directories - install the "
               "CLAP first.\n");
    }

    printf("\n%s\n\n", found ? "all passed (0 failures)"
                             : "FAILED (the factory does not offer BENCmouth)");
    return found ? 0 : 1;
}
