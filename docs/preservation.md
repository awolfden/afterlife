# Device preservation boundary

The source preview intentionally contains **no public installer**, full-flash dump, provisioned image, device-specific plan, OTA endpoint or factory restore workflow. Building source does not establish compatibility with another device.

The development installation retained the init() conference application, boot chooser, original application slot, NVS, calibration and personal data. Its private updater is pinned to one audited device and exact before/after images. Do not turn it into a generic installer by relaxing identity, hash, preimage or readback checks.

Any future supervised installation requires an independently reviewed policy covering:

- Actual device identity, flash size/layout, secure-boot/encryption state and exact existing images.
- A private verified complete preimage, with credentials/personal data treated as sensitive.
- One serial owner; explicit power/boot coordination with the person holding the device.
- The precise permitted application region, no expanded erase/write range and byte equality everywhere protected.
- Fresh full-preimage equality before writing; bounded app-only writes and complete post-write readback.
- A concrete rollback to the immediately preceding qualified application, subject to the same exact checks.
- Independent source/policy review, artifact hashes, stop conditions and a physically supervised acceptance window.

Never flash a generated bootloader, partition table, merged image or erased application slot as a shortcut. Never perform whole-chip erase, forced factory restore or guessed rollback. A preserved-byte readback is necessary but does not replace application/chooser boot acceptance.

The included partition CSV supports linking and size checks only. Public downloadable binaries, a compatibility-aware installer and an optional browser installer are later milestones. OTA is deferred. None is authorized by running the source tests or packaging tool.
