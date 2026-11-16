# Changelog

## Unreleased — 0.1.2-dev.0

- Preserve resident callback state and remaining platform resources when final startup/error shutdown retains worker ownership. Mark the service Failed and keep its owner alive without repeating that final stop.
- Add a fixed HEN host-lifetime recorder, bounded SM mailbox sampling and cached game-arm identity observations for investigating the existing `2011-0102` failure.
- Export acknowledged batches over the existing resident control connection, with strict reply validation, explicit gaps/backpressure and retained ownership after uncertain delivery or cleanup.
- Add an authenticated cached diagnostic route and a macOS/Linux/WSL JSONL capture tool. Diagnostic reads acquire no new FS/HEN service sessions or SD writer.
- Add production IPC/receiver pipeline regressions and a [capture guide](docs/HOST_EXIT_DIAGNOSTICS.md). No console acceptance or crash repair is claimed; v0.1.1 downloads are unchanged.

## v0.1.1 — cumulative release

**Known unresolved system crash:** a fresh-boot test reached HEN/resident startup and the browser, then Minecraft launched without R unexpectedly opened hbmenu. After HOME → X closed that host, launching Minecraft again produced `2011-0102`. No service Reload had been requested that boot. This does not isolate the cause or rule out shared reload/HEN/filesystem lifecycle code. v0.1.1 is a regular release with this known issue; no crash repair or complete device lifecycle acceptance is claimed. Its version-labelled app/resident rebuild has host validation only.

This release includes **all changes since v0.1.0-dev**, including the published dev.1–dev.4 updates and the previously unpublished dev.5 candidate:

- **More host setup choices:** direct held-session setup with kurumuz ns2jb on Linux/WSL2 or macOS, alongside mac2jb on macOS; Node 24 selection, immutable prepared snapshots, route checks, WSL networking guidance and Linux CI preparation coverage. Host checks do not establish Linux/WSL console acceptance.
- **Experimental CivSwitch integration:** the exact pinned `civ7-s8n` FS-only profile adds source/run qualification, authenticated bounded transports, checked RO handoff and tagged-thread adaptation. No Moader Civ7 console validation is claimed; other CivSwitch host families are not admitted by this profile.
- **Compressed ZIP imports:** valid DEFLATE streams no longer fail at 32 KiB input/output boundaries. Decoder errors identify the entry and zlib result; truncation, CRC, size, path and trailing-data checks remain enforced.
- **Responsive library and recovery:** complete cached inventories remain readable during extraction; competing writes report Busy. Failed/partial SD scans retain the previous inventory with a separate warning, pause unsafe mutations and retry without publishing an empty library. Browser Refresh explicitly rescans. Interface errors, scan warnings and the actual FS runtime result are separated.
- **Per-game availability:** shared Game copy choices gate new routing for matching physical/game-key cards or explicitly selected digital copies. Requested toggles remain saved when a game is unavailable. The Z-A false block from dev.4 is corrected: repeated matching-card observations no longer require a separate installed-content base-Program predicate. Digital still checks installed noncard base Program data; this is not ownership/license verification.
- **Cooperative service reload:** protocol 7 and **Settings → Advanced → Reload Moader service** stage and validate one replacement, coordinate shutdown, wait for the old PID, verify replacement health and reconnect the app. Busy, lost-reply and quarantined outcomes are distinguished. Two reload transactions completed on an earlier candidate, but subsequent game lifecycle acceptance failed; a later zero-reload boot reproduced the fatal.
- **Native ownership and installer checks:** retain SM proxy contexts after failed thread joins/closes, track GameArm's installed donor by full identity, and strengthen lifecycle/SDK host fixtures. The host installer now requires an independently observed Loader receive receipt before clearing its wake event. A successful install/startup with that correction is not proof of healthy host exit or next-game admission.
- **Matched components and documentation:** HEN/bootstrap ship as a source/patch-qualified pair, with stricter prepared-input/package validation, complete kaiHEN disclosure, expanded installation/recovery/card/reload guides, synchronized Wiki generation and clearer issue-report entrypoint fields. The native FS executable hook is unchanged by the reload feature.

See [v0.1.1 release notes](docs/releases/v0.1.1.md) for downloads, update expectations and evidence limits; [all changes since the initial tag](https://github.com/H644b/Moader/compare/v0.1.0-dev...v0.1.1) and [PR #7](https://github.com/H644b/Moader/pull/7) provide the source history.

## v0.1.0-dev.5 — unpublished candidate, superseded by v0.1.1

This was a development candidate name, **not a published release or tag**. Its cartridge/reload changes and later ownership/installer corrections are included in v0.1.1 with the unresolved system fatal disclosed.

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

## v0.1.0-dev.3 — compressed ZIP boundary fix

- Fixes valid DEFLATE imports that fill a 32 KiB output buffer exactly while consuming the current input chunk. The decoder refills the next chunk for the specific no-progress case rather than rejecting a valid stream.
- Decoder errors include the entry name and zlib status. Synthetic regressions cover first/repeated boundaries and exact completion while retaining truncated-stream, CRC, length, size and trailing-data rejection.
- App/resident were rebuilt and host-tested; HEN and the native FS runtime were unchanged. No console installation or game-crash fix was established by this update.

## v0.1.0-dev.2 — experimental CivSwitch host profile

- Adds the pinned `civ7-s8n` host integration: held FS-only proof qualification, authenticated bounded transport, source/materialized-run checks and Civ7's tagged-thread/bootstrap adaptation.
- Adds CivSwitch instructions, evidence boundaries, full kaiHEN modification disclosure, SD layout guidance, Wiki pages and entrypoint fields in issue reports.
- App, resident, patched HEN and native runtime bytes were unchanged. This release added host validation only; Moader acquisition, installation and gameplay through CivSwitch were not console-tested.

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
