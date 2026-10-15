# Security and privacy

This developer preview is not independently security-certified. Use the repository's **Security → Report a vulnerability** private reporting route when enabled. If it is unavailable, open an issue requesting a private reporting channel without disclosing the vulnerability, exploit details or affected private data. Never upload credentials, flash dumps, recordings, transcripts or a reproducible secret to a public issue.

## Boundaries

- Private local IPC uses owner-only directory/socket checks. It is not isolation from another process already acting as the same OS user.
- Normal status may include sanitized workspace captions; users must consider those sensitive even though prompts and tool content are not included.
- LAN uses explicit RAM-only provisioning and pairing. IP addresses, certificates and network credentials remain sensitive; memory erasure across Python/JavaScript/native libraries is not guaranteed.
- This is watcher-only source: microphone acquisition, transcription and reply submission are removed. Retired voice entry points must remain rejected. Reconnect must not introduce a content/reply path or replay consumed notifications.
- A device receipt or acknowledgement does not authorize a tool, resolve a source request or prove a displayed result.
- No local speech recognizer or cloud transcription fallback is part of this preview. Restoring those capabilities requires a separate design and explicit approval.
- Source packaging uses pinned allowlists and supplemental privacy checks. Independent review remains required; the checks are not a secret-scanner guarantee.

Do not disable safety gates to investigate an issue. Prefer synthetic tests and content-free counters; obtain explicit approval for hardware or audio reproduction. Follow [preservation](docs/preservation.md) for every potential device-write path.
