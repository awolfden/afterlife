# Source-preview release process

Version: **0.1.0-dev.1**. This is a locally prepared candidate, not a published release, public firmware installer or clean-machine certification.

## Contents and exclusions

`release/source-files.json` is the explicit source allowlist. Each entry has a SHA-256 and license classification; the manifest is copied separately to avoid a self-hash cycle. The tool adds a normalized inventory and `publicationApproved:false` status. No recursive checkout archive is permitted.

Included: selected original host/extensions/firmware/rendering source, reviewed third-party codec/IMU source with notices, generated original artwork, portable tests/fixtures, illustrated user/operator/developer docs and build/staging helpers. Documentation SVGs are labeled artwork/layout illustrations, not physical screenshots; their provenance is recorded in `docs/images/README.md`.

Excluded: historical private notes, device identities, flash plans/updaters, backups/readbacks, compiled firmware, SDK/GFX checkouts, pairing material, audio/transcripts, local launch configuration, personal runtime paths and installed Pi bundles. The source archive is not an npm package; `package.json` is private and its npm file allowlist is deliberately minimal.

## Local staging

After reviewing changed files, intentionally refresh only their manifest hashes and review any allowlist/provenance changes. The packager does not refresh pins or approve content on your behalf.

```sh
python3 tools/stage-source.py --root . --output "$HOME/watcher-source-candidate"
```

The output directory must not exist and must be outside the source tree. The command reads only allowlisted regular same-owner files without following symlinks/hardlinks, rejects unsupported types, verifies hashes and runs supplemental content checks. Matching private content is not printed. It creates:

- `source/`: the candidate tree, including `release/inventory.json`.
- `pi-watcher-pet-0.1.0-dev.1-source.tar.gz`: deterministic source archive with normalized modes/timestamps and no links.
- `receipt.json`: archive SHA-256, file count and unpublished status.

A failed attempt may leave partial output after output creation. Never use partial output as a release; inspect/remove it deliberately and choose a new destination. Privacy-pattern checks are heuristic and cannot certify absence of every secret, license issue or private identifier.

## Validation and release gates

1. Review the actual staged inventory, license mapping and notices independently; inspect changed source and supplemental privacy results.
2. Run `npm test` from an extracted/staged path, not just the working tree. Record exact tools, test count/skips and failures.
3. Build a fresh external firmware profile using the pinned preprovisioned SDK/GFX trees; record the profile, flags and output hashes. Reusing the same machine/toolchain is **not** clean-machine reproduction.
4. Verify archive determinism and notice preservation; retain hashes/evidence outside the release tree. Recheck all pins after fixes.
5. State remaining hardware and external-Pi-runtime qualification gaps. Generic profiles keep touch default-off; the Afterlife profile enables it. Selected owner acceptance of Afterlife Settings/navigation does not establish exhaustive touch/output or sustained LAN qualification. Do not generalize historical voice checks to the watcher-only release.
6. Obtain explicit publication approval and a final release/privacy/license review before uploading anything. The staging tool never sets publication approval true and never pushes, installs or publishes.

Use the developer-preview version `0.1.0-dev.1` consistently for the initial source tag/package; do not label it a stable `0.1.0` installer release. Before creating a public repository or tag, review the exact staged tree/archive, notices, documentation links and image provenance, and obtain final publication approval. Initialize any future repository from the approved staged source, never the private development directory or its surrounding evidence.

Physical firmware acceptance, public binaries, generic installation and browser-based installation need separate approval and policies. Response-context delivery is deferred; the limitation must stay visible, but is not by itself a blocker to a narrowly described source preview.
