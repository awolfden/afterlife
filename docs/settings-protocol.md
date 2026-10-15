# Desk settings protocol v1

New owned prefixes S0–S3 were checked against current host/main/LAN parsers before implementation; no collision was found. No D2 bit is allocated. ASCII, single spaces, canonical unsigned decimal uint32, lowercase 32-hex-digit synchronization token, CRC32 over everything through the space preceding CRC, LF terminator. Payload excluding LF is at most 191 bytes. Parsers reject whitespace variations, extra fields, zero boot/sequence/revision, invalid flags/brightness and overflow. CRC detects corruption, not authentication; transport ownership remains mandatory.

```
S0 boot seq 1 crc
S1 boot seq token revision ack status flags brightness forced crc
S2 boot seq token edit baseRevision flags brightness crc
S3 boot seq token stateId activity crc
```

- S0: watch advertisement only; no preferences and no authority/liveness renewal.
- S1: host persisted-value echo/restoration. `ack=0` means restore/no request yet. `status=0` means durable; `status=1` means save unavailable (never Saved). `flags` bits 0 vibration, 1 sounds, 2 animations, 3 cycle sessions. Brightness is 80/160/240. `forced` is 0/1 for host-forced reduced motion. A fresh host/link generation uses a fresh random 128-bit token. Restore only on the exact current accepted W1 sequence and active transport. No data supplies a session ACK.
- S2: watch edit, edit/baseRevision nonzero. Match active token, boot, fresh acknowledged W1 sequence. One transmitted edit at a time; identical retry re-echoes without writing. A conflicting duplicate, gap/backward ID or stale base revision is rejected. The first edit ID is 1. Automatic transmission expires after 1500 ms; retry no faster than 250 ms while the same epoch is live. Keep the one outstanding ID/payload as bounded correlation state, label it unsaved, and accept a late successful echo. A new explicit local change may resume that identical outstanding request before sending the coalesced new desired value; never reuse its ID with different data. Host status=1 alone latches persistence failure until a new synchronization epoch; a watch timeout does not imply a failed store.
- S3: local interaction, stateId nonzero. `activity` bits 0 Settings open, 1 contact, 2 settling/quiet interval. A repeated identical state refreshes the interaction hold only while ordinary W1/D2 freshness is already valid. Lower/conflicting IDs are rejected. State counters never wrap. An expired/missing interaction witness cannot authorize cycling: synchronized live state must explicitly establish activity=0 before timed cycling begins.

S1 may accompany a new W1 at most every 250 ms, at most once per increasing sequence within a boot, including across host-module resets. Parse W1 before its S1 and update the native context synchronously. S0/S2/S3 must follow the D2 acknowledging the SAME frozen W1 sequence in the same ordered bounded emission batch. Immediate interaction transitions require a D2-led batch too, never a standalone settings line with an unacknowledged sequence. USB admission must account for the whole batch; LAN retains its per-line queue limit and drop-on-full behavior. Settings traffic never selects a transport or renews its liveness. The caller owns monotonic nonwrapping interaction IDs and cadence; native interaction() is only a validated encoder.

Native same-frame token tombstones are retained until accepted W1 sequence advances (which must be monotonic within a boot), not for the lifetime of the boot. Fresh-frame restoration can rebind an unchanged host token and adopt its acknowledged edit baseline after a watch-only freshness lapse. A status=1 restoration still binds and restores the last host value, but is labeled Not saved on Mac and cannot submit edits. Clock reversal retires synchronization until the monotonic clock catches up; it does not authorize lower timestamps.

Device behavior: immediately apply local changes in RAM; keep at most one in-flight edit plus one coalesced newer desired value. An old successful echo updates the known committed baseline but never overwrites a newer desired value or marks it saved. A failed echo preserves local values and displays Not saved on Mac. Loss/replacement of token discards pending transmissions. New epoch restoration replaces temporary offline values with the Mac values, rather than uploading them. No settings packet creates output, session selection or attention acknowledgment.

Host integration must reset synchronization at every device boot, host start and active-link retirement. Host uses only committed settings, plus master mute and existing freshness/capability/fault gates. Old peers receive no settings commands absent S0; their basic metadata remains supported, not an assertion that settings are enforced. Settings snapshots and support must not establish W1/D2 readiness.

If rename succeeded but directory fsync failed, the on-disk value may be newer than the host's last confirmed in-memory value. It is deliberately reported unsaved until a fresh host startup reads it; the subsequent restoration can therefore recover a value previously labeled unsaved. No claim of rollback or guaranteed loss is made.

This document describes the matching host/firmware protocol. It is not a public installation procedure or qualification of mixed-version deployments.
