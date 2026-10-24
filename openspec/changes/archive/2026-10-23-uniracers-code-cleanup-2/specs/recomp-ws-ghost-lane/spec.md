## REMOVED Requirements

### Requirement: Ghost lane harvest

**Reason**: The ghost lane (`SNESRECOMP_WS_OFFWINDOW=ghost`) was parked as too slow, was never the default and was selectable only from the environment; `source` serves the leading margin from the track table at negligible cost.

**Migration**: `ghost` resolves to `source` (see `recomp-ws-source-default`). The implementation is in git history before `uniracers-code-cleanup-2`.

### Requirement: Leading cells served by exact world match

**Reason**: Part of the removed ghost lane; `source` serves leading cells from the `$7F` table instead.

**Migration**: None needed; `ghost` resolves to `source`.

### Requirement: Real lane unchanged

**Reason**: The speculative lane no longer exists, so there is no real/speculative split to keep apart.

**Migration**: None needed.

### Requirement: Zero wrong cells and measured coverage

**Reason**: Part of the removed ghost lane; the zero-wrong-cells rule for `source` is in `recomp-ws-source`.

**Migration**: None needed.

### Requirement: Cost budget

**Reason**: Part of the removed ghost lane; the cost rule for `source` is in `recomp-ws-source`.

**Migration**: None needed.
