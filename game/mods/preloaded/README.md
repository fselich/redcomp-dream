# Preloaded mods

Ship reviewed, default-disabled packages here:

```text
packages/<package-id>/<version>/
  manifest.toml
  ...
```

A manifest's `[[target]]` names this title as `game_id = "uniracers-usa"` with the
ROM's SHA-256 (both live in `rom_identity.txt`). CMakeLists.txt declares this
directory with `snesrecomp_target_mod_catalog` and the FRAMEWORK stages
`packages/` beside the executable on every build -- do not add a copy step of
your own, and do not spell the destination: it belongs to snesrecomp, so a
future change to the layout touches one file instead of every port. Nothing
placed beside the executable by hand survives a build.
Players install `.snesmod` archives through the launcher's Mods page, which
the runtime keeps under its own state beside the executable.

See `snesrecomp/docs/MOD_PACKAGES.md` for the manifest format and the
trusted-plugin registration a package can activate.
