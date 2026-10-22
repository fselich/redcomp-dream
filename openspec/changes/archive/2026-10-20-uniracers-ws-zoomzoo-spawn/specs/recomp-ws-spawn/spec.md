## ADDED Requirements

### Requirement: Track-scoped N/A evidence

A spawn/staging N/A SHALL name the track it measured and SHALL test the second
track before generalizing, when the two tracks have different content.

#### Scenario: N/A names the track

- **WHEN** a change records P9–P13 as not applicable
- **THEN** the report names the track and the frame set used
- **AND** the result is not described as engine-wide

#### Scenario: Second track differs

- **WHEN** Dragster (first track) is N/A and Zoom Zoo (second track) has
  different track art or HUD
- **THEN** Zoom Zoo is measured independently
- **AND** its spawn/staging result is recorded separately

#### Scenario: Generalization without a second-track test

- **WHEN** an N/A from the first track is applied to the second without evidence
- **THEN** the claim is rejected
- **AND** the missing track measurement is recorded as a follow-up
