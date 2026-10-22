#include "recomp_net/module.h"
#include "recomp_net/recomp_net.h"
#include "protocol/rnet_protocol.h"

#include <string.h>

#ifndef RNET_BUILD_ID
#define RNET_BUILD_ID "unknown"
#endif

size_t rnet_module_info(RNetModuleInfo *out, size_t out_size) {
    RNetModuleInfo m;
    memset(&m, 0, sizeof m);
    m.struct_size = (rnet_u32)sizeof m;
    m.abi_version = RNET_MODULE_ABI_VERSION;
    m.abi_minor = RNET_MODULE_ABI_MINOR;
    m.wire_version = RNET_WIRE_VERSION;
#ifdef RNET_ENABLE_ICE
    m.features |= RNET_MODULE_FEATURE_ICE;
#endif
#ifdef RNET_MODULE_HAS_RBENGINE
    m.features |= RNET_MODULE_FEATURE_RBENGINE;
#endif
#ifdef RNET_MODULE_VERSION_MAJOR /* a release build: the tag's version */
    m.version_major = RNET_MODULE_VERSION_MAJOR;
    m.version_minor = RNET_MODULE_VERSION_MINOR;
    m.version_patch = RNET_MODULE_VERSION_PATCH;
#else
#ifdef RNET_MODULE_VERSION_MAJOR /* a release build: the tag's version */
    m.version_major = RNET_MODULE_VERSION_MAJOR;
    m.version_minor = RNET_MODULE_VERSION_MINOR;
    m.version_patch = RNET_MODULE_VERSION_PATCH;
#else
    m.version_major = RNET_VERSION_MAJOR;
    m.version_minor = RNET_VERSION_MINOR;
    m.version_patch = RNET_VERSION_PATCH;
#endif
#endif
    m.build_id = RNET_BUILD_ID;
    if (!out) return 0;
    size_t n = out_size < sizeof m ? out_size : sizeof m;
    memcpy(out, &m, n);
    return n;
}
