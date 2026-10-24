# recomp-docs Specification

## Purpose

Rules for the project documentation: one current English set in docs/, configuration documented from the code, rejected approaches recorded with evidence, links that resolve only to live targets, a temporary unlinked history folder, and filled spec purposes.

## Requirements
### Requirement: One current documentation set

The project SHALL keep one current description of the port in `docs/` (`README.md`, `ARCHITECTURE.md`, `WIDESCREEN.md`, `TRACK_MARGIN.md`, `CONFIG.md`, `VALIDATION.md`, `ROADMAP.md` and `formats/`), written in English, in the present tense, describing the code as it is. Documents outside `docs/history/` SHALL NOT contradict each other or the code.

#### Scenario: Margin policy

- **WHEN** a reader looks up how the BG1 margin is drawn
- **THEN** every live document that mentions it names `source` as the default and `clamp` as the only alternative

#### Scenario: Language

- **WHEN** any live document is opened
- **THEN** its prose is English

### Requirement: Configuration documented from the code

`docs/CONFIG.md` SHALL list every user option with its `config.ini` key, environment variable, values and default as defined in `game/src/ws_settings.c`, and every `SNESRECOMP_*` variable read in `game/src`, split into user and debug variables.

#### Scenario: No undocumented switch

- **WHEN** every `SNESRECOMP_*` string in `game/src` is collected
- **THEN** each appears in `docs/CONFIG.md`

#### Scenario: No phantom switch

- **WHEN** every `SNESRECOMP_*` name in `docs/CONFIG.md` and `README.md` is collected
- **THEN** each is read in `game/src` or is a framework variable documented as such

### Requirement: Rejected approaches recorded

`docs/TRACK_MARGIN.md` SHALL record each approach tried and rejected for the widescreen BG1 margin, with what it was, why it was rejected and a link to the archived OpenSpec change or report holding the evidence.

#### Scenario: Removed policies

- **WHEN** a reader looks for why `ring`, `wrap` or `ghost` is not available
- **THEN** each has an entry with its reason and evidence link

#### Scenario: Earlier experiments

- **WHEN** a reader considers edge fill, tilemap copy, a synthetic generator call, VRAM stitching or track repeat
- **THEN** each has an entry with its reason and evidence link

### Requirement: Links resolve to live targets

Live documents (every Markdown file outside `docs/history/`, `openspec/` and the vendored framework, and the comments in `game/src`) SHALL link only to files that exist in the repository and are not under `docs/history/` or `work/`. References to OpenSpec changes SHALL use their archived path.

#### Scenario: Archived change reference

- **WHEN** a live document cites an OpenSpec change
- **THEN** the path is `openspec/changes/archive/<date>-<name>/…` or an active change, and it exists

#### Scenario: Link check

- **WHEN** the link check script runs over the live documents
- **THEN** it reports no missing target and no target under `docs/history/` or `work/`

### Requirement: Temporary history folder

The superseded investigation logs SHALL be kept unchanged in `docs/history/`, with a `docs/history/README.md` stating that its contents are obsolete, not maintained, may contradict the code, and are kept only temporarily for reading. Deleting `docs/history/` SHALL NOT break any link in a live document.

#### Scenario: Reading a log

- **WHEN** a reader opens `docs/history/README.md`
- **THEN** it says the files are obsolete and temporary and points to `docs/README.md` for the current documentation

#### Scenario: Deleting the folder

- **WHEN** `docs/history/` is removed and the link check runs
- **THEN** it reports no broken link

### Requirement: Entry points

The root `README.md` SHALL describe the project for a player or contributor (what it is, the legal note, build, run, options summary, documentation index). `game/README.md` SHALL describe the `game/` directory and SHALL NOT contain scaffold placeholder text.

#### Scenario: Scaffold text gone

- **WHEN** `game/README.md` is searched for "Add a short description"
- **THEN** nothing is found

### Requirement: Spec purposes filled

Every spec in `openspec/specs/` SHALL have a Purpose that says what the capability covers instead of the archive placeholder.

#### Scenario: No placeholder

- **WHEN** `openspec/specs/*/spec.md` is searched for "TBD - created by archiving"
- **THEN** nothing is found

