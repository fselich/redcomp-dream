# Loadable module

`-DRNET_BUILD_MODULE=ON -DRNET_RBENGINE_DIR=<retcomm-rbengine>` also builds
`recomp_net_module` (`.so` / `.dll` / `.dylib`): recomp-net, rbengine and, with
`RNET_ENABLE_ICE`, libjuice, in one shared library. An engine or runner loads it
at run time so the netcode updates without rebuilding the engine.

Contract: `include/recomp_net/module.h`.

- `rnet_module_info()` reports `abi_version`, `abi_minor`, `wire_version`,
  `features`, the library version and a build id. `rnet_module_check()` is the
  header-only acceptance test a loader runs first.
- Every public `rnet_*` / `rbe_*` function is exported under its own name
  (Linux: `module/exports.map`; Windows: export-all). Nothing else is.
- **Bump `RNET_MODULE_ABI_VERSION`** when a struct layout or signature changes;
  **`ABI_MINOR`** when symbols or trailing fields are added;
  the wire version is `RNET_WIRE_VERSION` (rnet_protocol.h): bump it on any change two peers must agree on, and the module reports it. Engines
  fold the wire version into the identity passed to
  `rnet_rb_driver_set_identity`.
- Gate: `module_test` dlopens the built library and resolves the driver.

## Shipping it

`scripts/package-module.sh <rbengine> <out>` builds the module, runs
`module_test`, archives the library and writes the platform's manifest fragment.
It records what the shipped file says about itself (`module_test <lib> --info`),
and refuses when its build id is not the commit. `scripts/merge-module-manifest.py`
joins the platforms into `netplay-module-manifest.json` (refusing if they
disagree on version, commit, ABI or wire version). `.github/workflows/release.yml`
runs both on a tag. Linux x86_64 was built and installed by hand; the workflow
and the arm64 / macOS legs have not run.

Launchers read the manifest (retcomm-launcher `docs/NETPLAY_MODULE.md`): it
needs `module_abi.version` to equal the one their runners were built for, checks
size and SHA-256, and installs only if the extracted library reports the
manifest's version, commit, ABI and wire version.
