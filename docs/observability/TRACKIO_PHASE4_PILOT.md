# Trackio Phase-4 Evaluation Pilot

This is a small, optional pre-Phase-5 observability pilot. It projects the
already accepted Phase-4C evaluation report into summary metrics for a local
Trackio run. It does not add gameplay, Teacher, Battle/Lethal, trajectory,
admission, dataset, rules, deck, CMake, or acceptance behavior.

## Authority and validation

The input is the committed pair:

```text
docs/p4c/p4c_acceptance.json
docs/p4c/P4C_ACCEPTANCE.md
```

Before the report is loaded for projection, the exporter loads the existing
Phase-4C validator module and calls its `validate_files` function with:

```text
expected_source_head =
9fe935531b63aaaf9535201dd4daf3f25e0f1a93
```

It also requires `report["status"] == "PASS"`. The exporter does not call the
validator CLI `main()` and does not compare the archived report head with the
current checkout head. This is intentional post-hoc consumption of immutable
Phase-4C evidence. A changed report head, non-canonical JSON, inconsistent
Markdown, or any other validator failure stops the export before Trackio is
initialized.

The one-time historical acceptance precondition is validated at the accepted
Phase-4C H_exec `9fe935531b63aaaf9535201dd4daf3f25e0f1a93`. The temporary
detached worktree used for that check is not part of exporter runtime behavior.

Trackio is presentation infrastructure only. It is not an acceptance,
gameplay, provenance, semantic-identity, trajectory, dataset, or checkpoint
authority.

## Optional local use

Trackio is not an OCGForge runtime dependency. Install the pinned optional
version only when a real local dashboard run is wanted:

```text
python -m pip install trackio==0.37.0
```

No command auto-installs dependencies. Without Trackio, `--dry-run` still
works; a real export fails clearly before Trackio initialization. The normal
export uses only the documented public calls `trackio.init`, one
`trackio.log(..., step=0)`, and `trackio.finish`.

## Commands

Canonical deterministic projection, without importing or initializing Trackio:

```text
python -B tools/observability/phase4_trackio_export.py --dry-run
```

Normal local export:

```text
python -B tools/observability/phase4_trackio_export.py
```

Defaults are:

```text
project  = ocgforge-phase4-evaluation
run-name = phase4c-<first 12 characters of source_head>
```

The command also accepts `--report`, `--markdown`, `--project`, and
`--run-name`. It does not specify `space_id`, `bucket_id`, or `server_url`; no
Hugging Face login, network requirement, Space creation, or automatic sync is
part of this pilot.

The optional dashboard is a manual action:

```text
trackio show --project ocgforge-phase4-evaluation
```

## Projected public config

The run config contains exactly these versioned/public fields:

```text
ocgforge_source_head
ocgforge_source_base
matchup_id
rules_bundle_id
format_id
duel_mode
duel_flags
teacher_producer_identity
battle_snapshot_schema
provable_lethal_schema
integration_decision
positive_lethal_capability
acceptance_schema
acceptance_status
trackio_version
exporter_schema
```

The presentation mapping is frozen as:

```text
exporter_schema = ocgforge.trackio.phase4_evaluation_export.v1
trackio_version = 0.37.0
```

## Projected metrics

Only this compact numeric set is sent to Trackio:

```text
gates_pass_count
gates_total_count
matrix_rows_pass_count
matrix_rows_total_count
record_count
battle_decision_record_count
battle_command_candidate_count
sidecar_invalid_count
proven_lethal_count
lower_bound_present_count
command_record_count
failed_command_record_count
sidecar_influences_gameplay
```

All counts are derived from the validated report. The sidecar influence value
is encoded as `NO -> 0` and `YES -> 1`. No accepted value is hard-coded as
acceptance truth.

For the currently accepted report, the derived projection is expected to be
15/15 gates, 4/4 matrix rows, 128 records, 16 Battle decision records, 32
BattleCommand candidates, zero sidecar-invalid records, zero proven-lethal
records, zero lower-bound records, 45 command records, zero failed command
records, and sidecar influence `0`.

## Privacy and determinism boundary

The exporter never sends or logs core state, observations, private identities,
semantic keys, response bytes, submission tokens, card passcodes, observation
or physical locators, absolute paths, process/host/time data, cache paths, raw
validator output, trajectory shards, admission receipts, dataset manifests, or
acceptance files as Trackio artifacts.

The dry-run payload is canonical JSON (`sort_keys=True`, two-space indentation,
UTF-8, final newline). It contains only the projected config, metrics, project,
and run name. It contains no Trackio-generated run ID, so repeated fresh
processes produce byte-identical output for the same validated input.

Gameplay semantic change: none.

Privacy boundary change: none.

Trajectory/replay change: none.

Teacher behavior change: none.

Dataset change: none.

Acceptance authority change: none.
