/* Loads the built module the way an engine does (dlopen + dlsym) and checks
 * the info, the acceptance test, and that a driver can be created. */
#include "recomp_net/module.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(int argc, char **argv) {
    CHECK(argc >= 2);
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    size_t (*info)(RNetModuleInfo *, size_t) = (size_t (*)(RNetModuleInfo *, size_t))dlsym(h, "rnet_module_info");
    CHECK(info);
    RNetModuleInfo m;
    size_t n = info(&m, sizeof m);
    CHECK(n == sizeof m && m.struct_size == sizeof m);
    CHECK(rnet_module_check(&m, n, RNET_MODULE_FEATURE_RBENGINE) == 0);
    CHECK(m.wire_version >= 2); /* RNET_WIRE_VERSION, rnet_protocol.h */
    RNetModuleInfo bad = m; bad.abi_version += 1;
    CHECK(rnet_module_check(&bad, n, 0) != 0);
    CHECK(dlsym(h, "rnet_rb_driver_create"));
    CHECK(dlsym(h, "rnet_session_create"));
    CHECK(dlsym(h, "rbe_snap_ring_create"));
    CHECK(!dlsym(h, "rnet_proto_internal_not_exported_xyz"));
    void *(*mk)(void) = (void *(*)(void))dlsym(h, "rnet_rb_driver_create");
    void (*rm)(void *) = (void (*)(void *))dlsym(h, "rnet_rb_driver_destroy");
    void *d = mk();
    CHECK(d);
    rm(d);
    if (argc == 3 && !strcmp(argv[2], "--info")) {
        /* what scripts/package-module.sh records in the manifest */
        printf("version=%u.%u.%u abi=%u wire=%u build=%s\n", m.version_major, m.version_minor,
               m.version_patch, m.abi_version, m.wire_version, m.build_id);
        return 0;
    }
    printf("module_test ok: recomp-net %u.%u.%u abi %u.%u wire %u features 0x%x\n", m.version_major,
           m.version_minor, m.version_patch, m.abi_version, m.abi_minor, m.wire_version, m.features);
    return 0;
}
