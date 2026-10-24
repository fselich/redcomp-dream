## MODIFIED Requirements

### Requirement: Verified ring cells

With the `source` margin policy, the system SHALL serve a BG1 margin cell from the ring when its world key and tile word were confirmed in the native view. A cell that is not verified SHALL be served from the track table as `recomp-ws-source` specifies, and otherwise rendered as background.

#### Scenario: Trailing cell verified

- **WHEN** a margin cell's ring entry was seen in the native view with the same world cell and the VRAM word is unchanged
- **THEN** the cell is drawn from the ring

#### Scenario: Stale leading cell

- **WHEN** a margin cell's ring entry holds a different world cell, or was never seen natively, and the track table cannot serve it
- **THEN** the cell is drawn as background
- **AND** it is counted as masked, not as wrong

#### Scenario: Direction change

- **WHEN** the camera reverses, stops or moves vertically
- **THEN** the same per-cell rule applies without a direction heuristic

### Requirement: Dedicated switch

The margin policy SHALL be chosen as `recomp-ws-source-default` specifies. With `source`, the global BG1 clamp SHALL NOT be applied to BG1; with `clamp`, the margin hooks SHALL NOT be installed. HUD and cull policies SHALL be independent of the margin policy.

#### Scenario: Source

- **WHEN** the policy is `source`
- **THEN** the global BG1 clamp is not applied
- **AND** HUD and cull policies are unchanged

#### Scenario: Clamp

- **WHEN** the policy is `clamp`
- **THEN** no margin line or tile hook is installed for the frame
