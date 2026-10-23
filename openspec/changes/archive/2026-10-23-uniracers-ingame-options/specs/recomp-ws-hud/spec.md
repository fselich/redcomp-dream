## MODIFIED Requirements

### Requirement: Independent HUD switch

The change SHALL provide `SNESRECOMP_WS_HUD` and `[Widescreen] Hud`, independent of the cull switch. The environment variable SHALL win over the config key.

#### Scenario: HUD falls back

- **WHEN** `SNESRECOMP_WS_HUD=0`
- **THEN** the HUD returns to the authentic clamp while cull stays active
- **AND** 4:3 is unaffected

#### Scenario: HUD off from config.ini

- **WHEN** `SNESRECOMP_WS_HUD` is unset and `config.ini` has `[Widescreen] Hud = 0`
- **THEN** the HUD returns to the authentic clamp while cull stays active

#### Scenario: Single global switch

- **WHEN** the HUD shares the cull toggle
- **THEN** it is rejected
- **AND** a separate gate is required
