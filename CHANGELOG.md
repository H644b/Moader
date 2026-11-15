# Changelog

## v0.1.0-dev — prerelease

Initial public development release. Application binaries identify themselves as `0.1.0-dev`.

- Native Switch 2 manager and self-contained browser interface for ZIP import, enable/disable requests, conflict reporting, shared settings and recoverable library removal.
- Optional resident service with pairing, explicit web listener controls and access after the foreground manager exits.
- Checked filesystem catalogues, current-session generation receipts, retained in-flight views and same-size loose RomFS updates on tracked mounts. Added/resized files, images and executable changes have additional apply requirements.
- Native debug output through separately closed, CRC/epoch-checked snapshots, visible in the manager and browser while the original report writer remains retained.
- Bounded MK8 validation: one existing course-preview texture visibly followed disabled → enabled → disabled → re-enabled → removed in the same running game process. It refreshed after a race → quit to main menu → reopen course-selection scene reload; character-selection back-out alone did not refresh it.
- Normal MK8 close/reopen without a console reboot and bounded resident HTTP availability checks passed. Host sanitizer tests cover core, HTTP, routing, log snapshots and lifecycle behavior; installer tests use mocked transports.

### Limits of this prerelease

The privileged runtime is scoped to the current console boot. Universal hot swapping, automatic game-cache invalidation, arbitrary mod compatibility and console-reboot persistence are not established. Forced crashes and crash recovery have not been hardware-tested. No original game-content hash or same-open-file whole-content O→M→O hash trace is claimed.

Removal hides the entry and republishes routing while preserving its payload for retained native owners; it is recoverable library removal, not immediate physical SD erasure. No copyrighted game assets, firmware, keys or device/account data belong in this repository or its application distribution.
