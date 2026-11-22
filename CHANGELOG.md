# Changelog

## v4.0.0 — Setup and in-app updates

- Publish five self-contained, prebuilt **Moader Setup** app downloads for macOS, Linux and Windows with WSL2. GitHub Actions packages each OS and architecture separately. The smaller backend ZIP retains the command-line tools and matched native files. Setup guides host preparation, SD copying, connection checks and support export using the existing checked installers.
- Add **Settings → Updates** to the console app for manual checks and optional checks at launch and every six hours. Updates verify both app and resident downloads before installation and can resume after interruption.
- Match the Setup window to Moader's console and browser theme. Keep the pinned kaiHEN and Seedsow A/B components. The new Setup and update flows have host validation; the exact v4.0.0 release pair and desktop kit have no Switch 2 installation result. [Release notes](docs/releases/v4.0.0.md).

## v3.5.0 — community Store

- Enable the Store in the console app and paired browser, with hosted catalog browsing, search, previews and an On this Switch game filter.
- Verify published package size and SHA-256 before import. New Store mods start disabled; removal permits later reinstallation under the same mod ID.
- Keep the pinned Seedsow standalone A/B and matched kaiHEN backend kit. The Store has host validation; physical Switch 2 Store installation with the exact v3.5.0 app and resident remains unverified. [Release notes](docs/releases/v3.5.0.md).

## v3.0.0 — Seedsow standalone backend kit

- Ship the pinned on-console Seedsow A/B implementation and the source-built standalone bootstrap in the backend kit. A installs Moader FS and HEN after one-time SD/save preparation; B follows the regular upstream Lua chain with the same staged SD set.
- Verify one Profile B Demo A boot with `KH READY`, 19 active FS hooks, a running Moader plugin and a same-boot, CRC-valid FS acknowledgement matching the current catalogue. A separate Profile B B boot reached hbmenu by owner report. Other consoles, a second A cold boot and physical mod reads remain unverified.
- Keep the console-derived FS capture and payload outside public assets. Preparing A requires the exact qualified local capture; the published kit includes the generator and bootstrap but is not sufficient on its own for a new console. [Release notes](docs/releases/v3.0.0.md).
- After publication, the exact v3 app and resident passed independent FTP readback on the original console; the owner reported a fresh Profile B A boot reaching `KH READY` and Moader v3.0.0 with runtime and FS connected. No same-boot FS acknowledgement was fetched for this later run.

## v0.2.5 — fewer resident BSD sessions during game launch

- Request one BSD operation session for the resident instead of the pinned SDK default of three. Keep its monitor session, web listener, NIFM and UDP instance lock. This lowers the resident's planned BSD session count from four to two without changing the app or filesystem protocol.
- Rebuild the complete app, resident and backend ZIPs with the same matched RO/HEN/bootstrap and full kaiHEN program set. The exact resident was reloaded on a Seedsow Demo boot where Minecraft for Switch 2 had previously crashed with v0.2.0; Minecraft then entered a world and stayed running while the resident served HTTP. Splatoon 3 and a fresh v0.2.5 kit installation remain to be tested. [Release notes](docs/releases/v0.2.5.md).
- Keep the Linux/WSL source build available when devkitPro's official `dkp-toolchain-vars` package endpoint returns HTTP 403 by allowing one alternate host for the exact checksum-pinned helper package.

## v0.2.0 — game carousel and community Store foundation

- Center the selected game in a cover carousel on the console and web. Open its mods and return to the same game; move listener and pairing controls into Settings.
- Add the native community Store reader for games, releases, galleries and independent publisher feeds. Store navigation stays disabled in this release. Publish the interface walkthroughs and publisher template links.
- Rebuild the app and matching resident with 0.2.0 labels. The backend kit keeps the source-qualified Seedsow RO/HEN/bootstrap set and the existing SmileBASIC and experimental CivSwitch host profiles. [Release notes](docs/releases/v0.2.0.md).
- Refresh the v0.2.0 app and resident after PR #23: request the compact installed game icon first, fall back to the full-size icon for the same game and Edition, and serve either JPEG or PNG artwork in the paired browser. The refreshed binaries have host validation and await a Switch 2 check.

## v0.1.3 — Seedsow Lullaby backend kit

- Add the Seedsow Lullaby Demo source-qualified entrypoint and one bounded installation record, with a matched RO/HEN/bootstrap set and patched GUI provisioning helper.
- Extend the host setup and source-build paths to Linux x86_64 and WSL2 x86_64. Linux CI checks passed; WSL2 and Linux live-device installations remain unverified.
- Refresh the two v0.1.3 ZIPs with the Seedsow backend files and 0.1.3-labelled app/resident. The rebuilt app/resident have host validation only; the exact RO/HEN/bootstrap bytes have the recorded Demo installation. [Release notes](docs/releases/v0.1.3.md).
- Fix the Seedsow GUI's first **Auto-Download Moader kaiHEN** action: a newly provisioned GUI now creates and validates its staging directory before downloading. The native component bytes are unchanged.
- Accept the Seedsow Lullaby full game's known program ID at the held `process/qualify` gate while retaining the Demo ID, current-boot code checks, and rejection of other titles. Full-game Moader installation remains to be tested on device.

## v0.1.2 — source-matched HEN refresh

- Remove Moader's local pending-launch workaround from the custom HEN source while retaining cooperative reload, cartridge checks and the supported host setup routes.
- Check official kaiHEN `20261117.1` and retain the qualified `20261114c` / `e897f991524590833b82fbe8d63a3c6775fb8101` base; the newer command-23 interface conflicts with Moader's existing reload ABI and is not integrated.
- Rebuild the app, resident, HEN/bootstrap and diagnostic app from the recorded source snapshot. Complete patch replay, source/binary qualification and host regressions pass; new hardware validation remains separate. [Release notes](docs/releases/v0.1.2.md).

## v0.1.1 — cumulative release

The version-labelled app/resident rebuild has host validation only. See [Validation](docs/VALIDATION.md) for the scope of the recorded hardware and host checks.

This release includes **all changes since v0.1.0-dev**, including the published dev.1–dev.4 updates and the previously unpublished dev.5 candidate:

- **More host setup choices:** direct held-session setup with kurumuz ns2jb on Linux/WSL2 or macOS, alongside mac2jb on macOS; Node 24 selection, immutable prepared snapshots, route checks, WSL networking guidance and Linux CI preparation coverage. Host checks do not establish Linux/WSL console acceptance.
- **Experimental CivSwitch integration:** the exact pinned `civ7-s8n` FS-only profile adds source/run qualification, authenticated bounded transports, checked RO handoff and tagged-thread adaptation. No Moader Civ7 console validation is claimed; other CivSwitch host families are not admitted by this profile.
- **Compressed ZIP imports:** valid DEFLATE streams no longer fail at 32 KiB input/output boundaries. Decoder errors identify the entry and zlib result; truncation, CRC, size, path and trailing-data checks remain enforced.
- **Responsive library and recovery:** complete cached inventories remain readable during extraction; competing writes report Busy. Failed/partial SD scans retain the previous inventory with a separate warning, pause unsafe mutations and retry without publishing an empty library. Browser Refresh explicitly rescans. Interface errors, scan warnings and the actual FS runtime result are separated.
- **Per-game availability:** shared Game copy choices gate new routing for matching physical/game-key cards or explicitly selected digital copies. Requested toggles remain saved when a game is unavailable. The Z-A false block from dev.4 is corrected: repeated matching-card observations no longer require a separate installed-content base-Program predicate. Digital still checks installed noncard base Program data; this is not ownership/license verification.
- **Cooperative service reload:** protocol 7 and **Settings → Advanced → Reload Moader service** stage and validate one replacement, coordinate shutdown, wait for the old PID, verify replacement health and reconnect the app. Busy, lost-reply and quarantined outcomes are distinguished. Two controlled reload transactions completed on an earlier candidate; those checks do not establish universal game or resource-lifetime compatibility.
- **Native ownership and installer checks:** retain SM proxy contexts after failed thread joins/closes, track GameArm's installed donor by full identity, and strengthen lifecycle/SDK host fixtures. The host installer now requires an independently observed Loader receive receipt before clearing its wake event. Installation/startup evidence is separate from complete game and reload validation.
- **Matched components and documentation:** HEN/bootstrap ship as a source/patch-qualified pair, with stricter prepared-input/package validation, complete kaiHEN disclosure, expanded installation/recovery/card/reload guides, synchronized Wiki generation and clearer issue-report entrypoint fields. The native FS executable hook is unchanged by the reload feature.

See [v0.1.1 release notes](docs/releases/v0.1.1.md) for downloads, update expectations and evidence limits; [all changes since the initial tag](https://github.com/H644b/Moader/compare/v0.1.0-dev...v0.1.1) and [PR #7](https://github.com/H644b/Moader/pull/7) provide the source history.

## v0.1.0-dev.5 — unpublished candidate, superseded by v0.1.1

This was a development candidate name, **not a published release or tag**. Its cartridge/reload changes and later ownership/installer corrections are included in v0.1.1 with their validation scope recorded separately.

- Matching inserted cartridges use repeated consistent card observations, without requiring an installed-content Application row. Digital still verifies installed noncard base Program content. Wrong, absent, changing and unverified cards stay blocked.
- Patched kaiHEN protocol 7 adds `ReloadPlugin` and authoritative identity/status/capability queries. A cooperative plugin is staged, drained, stopped and replaced once under a serialized Loader lease. Existing FS routing remains in FS.
- **Settings → Advanced → Reload Moader service** shows progress and reconnects after fresh process/runtime health. Lost replies query the existing operation; uncertain cleanup blocks another attempt. Imports and library writes deny unsafe shutdown.
- HEN and bootstrap now ship as a source/patch-bound pair, with strict setup/snapshot qualification and a complete modification disclosure. Old running HEN requires one matching per-boot installation before reload becomes available.
- Adds host regressions for restart receipts, retained file reads, lifecycle failures, shutdown acknowledgement, client recovery and paired installer/package validation. See [Validation](docs/VALIDATION.md) for the exact console boundary.

## v0.1.0-dev.4 — library recovery and game-card checks

- Library reads use the last complete cached inventory while ZIP extraction runs. Competing library writes report that the operation is busy instead of waiting on the import lock. Ordinary app/browser polls no longer walk every disabled payload. Availability withdrawal can continue while ZIP extraction is paused.
- SD enumeration/stat failures retain the previous inventory with a separate warning and pause mutations. Browser Refresh explicitly rescans; the resident also retries inventory scans in its background loop. Partial scans are never published as an empty library.
- Native operation errors no longer overwrite the filesystem runtime result. A concurrent library change retains the previous native list until a complete refresh succeeds.
- Per-game **Settings → Game copy** persists Unspecified, Cartridge/game-key or Digital selection. Current matching-card and base Program checks gate new runtime catalogues; requested mods stay saved but show **Game unavailable** with the reason. Digital is a user declaration, not detected ownership. Game-key downloads still require their matching card.
- Availability failures fail closed, and details distinguish a blocked request, pending withdrawal, an acknowledged omission and a failed withdrawal. Already loaded assets/executable changes are not retroactively undone.
- Includes dev.3's valid DEFLATE boundary import fix. Rebuilt app/resident have host validation only. HEN and native FS runtime are unchanged; the app display version remains `0.1.0-dev`.

This update fixes reproduced manager defects; the rebuilt pair has host validation only.

## v0.1.0-dev.3 — compressed ZIP boundary fix

- Fixes valid DEFLATE imports that fill a 32 KiB output buffer exactly while consuming the current input chunk. The decoder refills the next chunk for the specific no-progress case rather than rejecting a valid stream.
- Decoder errors include the entry name and zlib status. Synthetic regressions cover first/repeated boundaries and exact completion while retaining truncated-stream, CRC, length, size and trailing-data rejection.
- App/resident were rebuilt and host-tested; HEN and the native FS runtime were unchanged. No console installation was established by this update.

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

The privileged runtime is scoped to the current console boot. Universal hot swapping, automatic game-cache invalidation, arbitrary mod compatibility and console-reboot persistence are not established. Recovery from forced termination has not been hardware-tested. No original game-content hash or same-open-file whole-content O→M→O hash trace is claimed.

Removal hides the entry and republishes routing while preserving its payload for retained native owners; it is recoverable library removal, not immediate physical SD erasure. No copyrighted game assets, firmware, keys or device/account data belong in this repository or its application distribution.
