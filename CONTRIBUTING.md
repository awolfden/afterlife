# Contributing

This is an unpublished source developer preview. Start with [developer setup](docs/developer.md) and [preservation](docs/preservation.md).

Keep changes small, preserve original ghost identity and match existing module conventions. Add synthetic boundary tests before changing authority, reconnect, paging, settings or flash-preservation behavior. Do not include real session content, credentials, device dumps or personal identifiers in fixtures.

Run the selected portable suite with `npm test`; report exact tools, skipped checks and remaining physical evidence. Do not run hardware/audio tests unattended or silently substitute a different SDK/provider. Missing runtime compatibility should fail closed or report unknown, not manufacture support.

Release content is controlled by `release/source-files.json`. New files need explicit provenance/license classification and review before inclusion. Updating a hash is not proof of correctness, reproduction or review. Preserve vendored licenses and original-source attribution.

Do not weaken preimage checks, replay consumed notifications across reconnect, conflate ACK with resolution, remove watchdog/drain safeguards or restore retired voice entry points implicitly. Public uploads and device writes require separate approval. There are no automatic commits, dependency updates or publication steps in the provided helpers.
