# Changelog

## v0.1.0-dev.5 — cartridge correction and plugin reload

- Matching inserted cartridges use repeated consistent card observations, without requiring an installed-content Application row. Digital still verifies installed noncard base Program content. Wrong, absent, changing and unverified cards stay blocked.
- Patched kaiHEN protocol 7 adds `ReloadPlugin` and authoritative identity/status/capability queries. A cooperative plugin is staged, drained, stopped and replaced once under a serialized Loader lease. Existing FS routing remains in FS.
- **Settings → Advanced → Reload Moader service** shows progress and reconnects after fresh process/runtime health. Lost replies query the existing operation; uncertain cleanup blocks another attempt. Imports and library writes deny unsafe shutdown.
- HEN and bootstrap now ship as a source/patch-bound pair, with strict setup/snapshot qualification and a complete modification disclosure. Old running HEN requires one matching per-boot installation before reload becomes available.
- Adds host regressions for restart receipts, retained file reads, lifecycle failures, shutdown acknowledgement, client recovery and paired installer/package validation. See [Validation](docs/VALIDATION.md) for the exact console boundary; this is not a claim to repair the earlier system fatal.

## v0.1.0-dev.4 — library recovery and game-card checks

- Library reads use the last complete cached inventory while ZIP extraction runs. Competing library writes report that the operation is busy instead of waiting on the import lock. Ordinary app/browser polls no longer walk every disabled payload. Availability withdrawal can continue while ZIP extraction is paused.
- SD enumeration/stat failures retain the previous inventory with a separate warning and pause mutations. Browser Refresh explicitly rescans; the resident also retries inventory scans in its background loop. Partial scans are never published as an empty library.
- Native operation errors no longer overwrite the filesystem runtime result. A concurrent library change retains the previous native list until a complete refresh succeeds.
- Per-game **Settings → Game copy** persists Unspecified, Cartridge/game-key or Digital selection. Current matching-card and base Program checks gate new runtime catalogues; requested mods stay saved but show **Game unavailable** with the reason. Digital is a user declaration, not detected ownership. Game-key downloads still require their matching card.
- Availability failures fail closed, and details distinguish a blocked request, pending withdrawal, an acknowledged omission and a failed withdrawal. Already loaded assets/executable changes are not retroactively undone.
- Includes dev.3's valid DEFLATE boundary import fix. Rebuilt app/resident have host validation only. HEN and native FS runtime are unchanged; the app display version remains `0.1.0-dev`.

This update fixes reproduced manager defects. It does not establish or claim a fix for the reported fatal Minecraft/hbmenu system restart.

## v0.1.0-dev.1 — host setup update

- Direct setup from a held kurumuz ns2jb session on Linux or WSL, alongside the existing mac2jb workflow. The preparation helper accepts a run directory and a local Node 24 executable.
- Beginner Linux/WSL instructions, including WSL LAN networking requirements, plus the official GitHub Wiki for setup and everyday use.
- Real Linux host-kit preparation coverage in CI. Console acquisition and WSL-to-console networking remain separate from host checks.

The app still displays `0.1.0-dev`. App, resident service, filesystem runtime and patched HEN binaries are unchanged from the first prerelease; this update changes the host tools and documentation.

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
