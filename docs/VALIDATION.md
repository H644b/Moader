# What has been tested

Moader supports [RomFS and ExeFS payloads](USAGE.md#upload-your-first-mod). The records below cover specific versions and actions. Game-launch compatibility and changes inside a running game are different tests.

## Carousel interface

On November 19, 2026, the maintainer reported that the redesigned app worked on their console. The NRO supplied for that check had SHA-256 `bfb876a0de82a9dc1224b70a09fc8fd86e7ddb0a4d03c645234864161046eb57`. [Tested source](https://github.com/H644b/Moader/commit/cda6ecc31acd90a8be179938f0062a515911c903)

Host checks covered controller and touch navigation, mod actions, settings, browser pairing, and the disabled Store. The native catalog reader also loaded the example publisher feed and decoded its preview over HTTPS. [Interface design](../DESIGN.md) · [Publisher template](https://github.com/H644b/moader-mod-repo-template)

## Refreshed v0.1.3 assets

The v0.1.3 backend ZIP includes the exact RO/HEN/bootstrap component hashes from the Seedsow Demo installation below. Its 0.1.3-labelled app (`c271a67c2e48aab2ebfb079acdcc2cc7785ee1a0180534f9fa2612ca038af97b`) and resident (`3830ca2129bae1e10c19084afb274e98772b34a6fff499b0eeca1690f4c76217`) are rebuilt bytes. They passed host compilation and packaging checks, but have not been installed on the console as this exact pair. The prior Demo installation used app `e251020bcd550afc72982ac80bf54a1060ff6aca8b3ffdb401a36c01c44ae9eb` and resident `4e17afa1c1f04f172fd91c9411e239e95eaf9c6067492921b769cda794ed13a0`. The extracted v0.1.3 Seedsow kit passed offline local setup, read-only qualification and pinned GUI provision/check. The current SmileBASIC and CivSwitch host preparations passed with pinned downloads and console transports blocked. None of these new kit checks is a live installation. [v0.1.3 release notes](releases/v0.1.3.md)

The [upstream full-game launcher work](https://github.com/alula/switch-artemis-launcher/pull/10) records full-game ID `0100ee5021c9e000` and the same main/SDK binaries as the Demo. That ID is now admitted alongside Demo ID `010042d022aea000` by the source-qualified held `process/qualify` stage. A user-provided full-game run screenshot reached `[4/4] process/qualify` and showed the prior Demo-only identity error; it did not include the process proof JSON or a successful Moader installation. Host tests evaluate the materialized gate for both IDs, an unrelated title and a failed identity query. The same-boot code preflight and remaining native proofs still apply. Full-game Moader installation and app/resident behavior remain unverified on hardware.

## Community mod report

Ben216k reported using extensive RomFS and ExeFS patches and additional subSDKs with Splatoon 3 and Splatoon Raiders. The same report describes a system crash on the stock v0.1.2 setup with Splatoon 3 and a local workaround. The report is a community observation. The controlled Mario Kart test below measured a specific in-game content reload. [Issue #14](https://github.com/H644b/Moader/issues/14)

## Seedsow Lullaby Demo source workflow

One combined installation was observed on a Seedsow Lullaby **Demo** on firmware 23.0.1 using the reviewed `Malev0/seedsow-fs-kaihen` source revision `7d0d8b370a0ae9cf7fb70b4f8d532886d0ec4a17` and Moader's matched local RO/HEN/bootstrap build. Those exact native component bytes are now included in the v0.1.3 backend kit. The earlier v0.1.2 kit and the SmileBASIC and CivSwitch records remain separate.

The same boot's private run receipts establish fresh FS memory read/write/restoration, two diagnostic RO/FS instruction challenges, Loader restoration, Moader resident runtime installation with fresh FS code qualification and metadata readback, matched kaiHEN startup (operation 10/result 0), and a borrowed, same-owner HEN inspection. That inspection reported HEN protocol 7/result 0, one published `moader` plugin, a reachable resident service, `backendConnected: true` and current routing generation. The original RO inspection lease remained owned; newly opened inspector handles closed.

The owner entered hbmenu through the documented two-launch hold-R route, opened Moader, and reported native runtime and FS connected in its debug view. The owner paired the console-hosted browser privately, loaded the library and settings, closed Moader with **+**, and reported that the browser still worked. A Mac HTTP check returned 200 from the console-hosted web page both before and after the app closed. The pairing code was neither collected nor published.

The owner also reported that Pokémon Z-A HD texture mods worked. No mod name, same-boot toggle/apply-state sequence, replacement-read trace or visible before/after capture was supplied for this Seedsow run. Treat that statement as an owner observation rather than a measured Seedsow file-routing test.

The GUI process used for this installation incorrectly compared HEN's own status PID with the bootstrap receipt's last launched plugin PID. The native HEN and resident inspection succeeded, but that old host process did not print its final held verification message. The source now uses a same-owner status gate that binds the retained RO startup file, HEN result, published plugin and live resident summary without equating those distinct PIDs. Focused host tests and the saved real receipt pass the corrected gate; a new console run of that final GUI message has not been recorded.

In a temporary clean source copy on Apple Silicon macOS, pinned SDK setup, an offline source build using supplied checksum-pinned Seedsow/kaiHEN archives, and the read-only backend check passed. The build reproduced the exact patched RO, HEN, bootstrap, app and resident hashes. The Apple command line tools `clang` and pinned SDK `objcopy` reproduced the 72-byte helper digest. Fresh online archive downloads were not repeated in this isolated check; the GUI's pinned source provisioning and matched-bundle host checks passed separately. These checks do not establish other firmware, a successful full-game Moader installation, a Linux/WSL live-console installation, or every game/mod file behavior. [Seedsow installation instructions](SEEDSOW.md).

The Ubuntu 24.04 x86_64 [Seedsow source-build CI job](../.github/workflows/ci.yml) downloaded and checksum-verified the host-specific devkitPro packages, provisioned the ounce SDK, rebuilt the source-qualified RO/HEN/bootstrap and Moader app/resident from the pinned upstream inputs, and passed the read-only backend check with `readyForInstallerCheck: true`. The separate Linux installer job also passed the no-replay transport and 72-byte helper checks with Linux `clang` and AArch64 `objcopy`. These are host results. WSL2 itself, its network path, and a Linux/WSL2 live-console installation were not exercised.

## v0.1.2 source refresh

The source removes Moader's local pending-launch workaround while retaining the existing reload, cartridge and setup features. Official kaiHEN `20261117.1` was checked; this release keeps the qualified `20261114c` / `e897f991524590833b82fbe8d63a3c6775fb8101` base because the newer command-23 interface conflicts with Moader's reload ABI. [Source disclosure](KAIHEN_CHANGES.md).

The HEN, bootstrap, app, resident and diagnostic app compiled from an isolated source snapshot with the selected SDK14 toolchain. The complete patch replay reproduces the current native source. Component hashes, NSO bounds and section hashes, decompressed privacy checks, NACP version metadata and unchanged FS prebuilt qualification passed. The full `make check` suite passed with AddressSanitizer/UndefinedBehaviorSanitizer, alongside installer, resident-control, setup and both host-profile qualification regressions. Independent review found matching source inputs and artifact descriptors. These were the publication-time build and host checks with scripted kernel boundaries. A later independent owner report records the three functional checks below. The earlier historical records retain their original component and workflow boundaries. See [v0.1.2 release notes](releases/v0.1.2.md).

### Independent owner test — 2026-11-17

After publication, the project owner independently tested the release identified as **v0.1.2** and reported **“v0.1.2 — all three passed”**. The three checks were:

- Z-A was allowed with its physical cartridge inserted.
- Z-A was blocked when the physical cartridge was absent.
- **Settings → Advanced → Reload Moader service** succeeded and reconnected.

This is owner-reported functional acceptance of those three checks. No agent-run console or RPC actions accompanied this documentation update. Installed component hashes, diagnostic reports/logs and PID/generation captures were not supplied. The report does not establish an extended reload count, new gameplay validation or a Civ7 result. Release artifacts and publication-time metadata remain unchanged.

## v0.1.1 release

v0.1.1 consolidates all changes since the first `v0.1.0-dev` tag. The app/resident are rebuilt with `0.1.1` labels; their exact bytes have host validation only. The native FS executable hook is unchanged. Installation, reload, browser and in-game checks have distinct scopes; earlier hardware results do not automatically validate a new binary pair. See the [release notes](releases/v0.1.1.md) for cumulative changes and downloads.

The first public prerelease is **v0.1.0-dev**. Its app and resident-service binaries are the same bytes used for the device tests below. This is an early release with a deliberately narrow hardware compatibility claim, rather than a promise that every game or mod updates instantly.

**v0.1.0-dev.1** updates the host setup and documentation. Its app, resident, filesystem runtime and patched HEN bytes are unchanged. The existing hardware results apply to those same binaries; the new direct ns2jb entry path has separate host-only coverage.

**v0.1.0-dev.2** adds the experimental Civ7 host integration and documentation with unchanged app/service/HEN bytes and app display version `0.1.0-dev`. Host qualification is separate from console acceptance; no new Civ7 console validation is claimed.

**v0.1.0-dev.3** fixes the compressed ZIP reader and rebuilds the app/resident. HEN and the native filesystem runtime are unchanged. The new app/resident binaries have **host validation only**. The console results below were obtained with the original binaries used through v0.1.0-dev.2; they have not been repeated on this rebuilt pair.

**v0.1.0-dev.4** rebuilds the app/resident for cached inventory, scan recovery, native error separation and game-card gating. It retains dev.3's ZIP fix. The pair was initially released with host validation only. A later dev.4 console installation reached hbmenu, the resident web interface and accepted runtime receipts with the saved 11-entry library. Z-A was incorrectly blocked after matching-card checks passed but the extra base-Program predicate failed; raw NS/NCM rows were not captured. Insertion/removal transitions were not verified. Original live-mod results below remain evidence for the earlier pair.

## Cartridge correction and plugin reload

The release includes protocol-7 cooperative resident reload and the corrected physical-card checks. The following are bounded records from earlier source candidates, rather than a universal compatibility claim.

A protocol-7 installation and matching-card check passed on device. With the Z-A card inserted, the Unspecified selection changed from the extra base-Program rejection to Routing ready. This establishes the positive card check, not missing-card withdrawal or arbitrary game/mod behavior.

The `9d9ab58` candidate was then installed from a new verified current-boot FS session. Startup, FS diagnostics and browser checks passed. Two controlled reloads completed without another FS acquisition: the saved reports show different old/new PIDs, generations 2 then 3 under the same HEN instance, two matching mounts, completed/joined PM workers, successful Loader receipts, and zero primary/restoration results. The original wait handle was restored with no selected waiter. All 11 mod records, the one saved enabled request and preferences matched across both restarts. Each app return to hbmenu and FTP worked. The previous current-instance reload package was removed, leaving one active package for that HEN instance; complete kernel resource accounting was not measured.

The installer correction retains the existing diagnostic RO as an independent process-memory observer until Loader restoration completes. It checks the bound Loader view before parking, arms a receive-result cookie while parked, then waits for a real receive acknowledgment before clearing the wake event. Retained FS reads cannot serve as that acknowledgment. Both host profiles use a Moader-owned adapter without editing their pinned provider sources. Unproved restoration keeps resources retained and blocks later native calls through a shared transport latch. Host fixtures cover delayed wakes, invalid replies and failure handling; host fixtures do not replace console testing.

The installer at `aad061b` then completed on a new verified FS session. Its saved receipt records the cookie overwritten by a real receive, a successful event clear and acknowledged restoration; the independent observer closed before diagnostic RO retirement. The native HEN bootstrap completed operation 10/result 0, joined its thread, restored gate/channel state and left no pending gate restoration. HEN protocol 7 and the resident were reachable. The FS runtime was active/verified with retained diagnostics, zero diagnostic write errors and all controller handles closed. Authenticated browser reads showed all 11 library entries, one saved requested mod and matching catalogue/FS generation 52. Web start also completed and closed its temporary client. These results validate installation/startup. All four CI jobs passed at this commit.

The native client now monitors cold-open and externally initiated operations as well as its own request. Tests cover capability/query uncertainty, pending cleanup and terminal error state retained for the app lifetime. The app blocks its Plus and missing-framebuffer voluntary return paths during unresolved or quarantined Loader activity, keeps report/screen export available, and displays shutdown recovery instructions. OS-controlled forced termination remains outside this guard; the app cannot repair Loader. The new error-state captures and state-machine tests are host evidence only.

The physical guard accepts a positively matched card only after a second consistent card observation. Production-provider regressions cover Z-A's validated Edition/owner relationship with empty and patch-only installed lists and assert zero installed-content/NCM calls for matching physical copies. Absent/wrong/changing cards, failed required queries, unsupported identities and count bounds still block. Digital retains full installed noncard base-Program checks.

The actual resident entry point runs with real Library, RuntimeBackend, WebServer and InstanceLock code and fake Horizon transport. Checks exercise cooperative reservation, listener shutdown, asynchronous abort/resume, a new explicit request, matching commit and guaranteed post-reply acknowledgement. A contended internal acknowledgement cannot be dropped as Busy. An injected plugin-stop failure retains the live Context and writer lock; the child verifies ownership before an atomic process exit rather than treating the failed join as cleanup.

A separate sanitizer-backed producer/reader integration destroys and reconstructs the actual C++ Library/RuntimeBackend with a new epoch while preserving enabled requests, preferences, pairing, generation and the existing receipt inode. One live instance of the actual C FS reader consumes the emitted catalogues: corrupt replacement publication keeps previous routing and reports an error, the valid new epoch accepts the same generation, retained CodeFS files remain readable, and 1,000 unchanged wakes remain bounded. Exact native-generated receipts are then read by the actual C++ backend. This is host integration using staged catalogue files, not a device process-reload demonstration.

HEN tests run the actual reload orchestration and lifecycle helper/server code with controlled process, transport and clock adapters. They check old command layouts and the complete request envelope, selected identity/nonce guards, idempotency, lifecycle Busy retries, asynchronous abort and post-reply commit notification, failure/restoration outcomes and quarantine. The native client's production state machine checks single submission, lost replies with query-only recovery, identity/generation guards, old-HEN/foreground behavior and reconnect after healthy publication. The production renderer has ready/progress/legacy/failure fixtures. No controller or device performance claim follows from host captures.

Installer tests independently bind both HEN/bootstrap components to source and patch digests, preserve immutable prepared inputs, reject old/mixed pairs and retain the existing no-replay/uncertain-outcome guard. Synthetic descriptor/build/package tests are distinct from rebuilding and inspecting the final release bytes.

Complete device acceptance remains specific to the exact component bytes, title and workflow tested. Cartridge-removal transitions, extended reload/resource stability and arbitrary game/mod combinations are not established by these checks. Install a matching backend once for a fresh boot; an older running HEN cannot acquire new commands from a copied app file.

## Library recovery and game-card checks in v0.1.0-dev.4

Actual production Library/ZIP code runs under AddressSanitizer and UndefinedBehaviorSanitizer with deterministic injected stat, directory-open and directory-read faults. An 800-file synthetic library remains available without payload stat calls during state polling. A decoder pause verifies snapshots, settings and busy-write rejection complete before extraction resumes. An already-enabled cartridge request is withdrawn while the decoder remains paused; the completed import merges the current cache and does not resurrect the withdrawn route. Failed complete scans retain the prior list/generation without publishing partial routing; initial unavailability is distinct from an empty library. Manual external additions invalidate old selections, and stable rescans avoid redundant publication.

Actual native dispatcher and renderer checks cover stale selection errors versus healthy/genuine runtime results, retained and initially unavailable inventory, controller copy selection, persistence and input/generation bounds. Real loopback HTTP checks cover authenticated source selection and rescan, cross-origin rejection, retained state during a failed scan and recovery. ZIP inputs cannot set the title-level copy choice.

The production availability provider uses real SDK declarations with fake NS/NCM calls. Fixtures cover absent/matching/wrong/multi-title cards, key-card data, explicit Digital mode, update-only/missing/partial base content, Program presence, service failures, count limits, changing snapshots, exact application IDs and bounded NX/Ounce/validated Edition identity mapping, including the Z-A relationship. Lifecycle checks balance service/database/storage cleanup. No real service permission, game-card response, digital license, title launch or console test is implied by these fixtures.

Blocked mods are omitted before backend payload validation. Composed regressions cover an SD scan error followed by failed routing withdrawal, retry with unchanged availability, retention of the inventory warning and recovery only after a full successful scan. Withdrawal details remain qualified by publication/receipt state. The supported installer initializes an empty catalogue before attaching the FS runtime and admits no separate legacy mod plan. Already open mounts, preexisting loaded code and a card removed immediately after an observation remain outside a universal instantaneous-revocation guarantee. [Policy and source references](CARTRIDGES.md).

No card contents, game payloads, device captures, pairing values or personal data are published with these tests.

## ZIP import fix in v0.1.0-dev.3

[Issue #4](https://github.com/H644b/Moader/issues/4) identified a valid DEFLATE stream that Moader rejected when compressed input and expanded output exhausted their 32 KiB buffers together. A following empty-input pump returned `Z_BUF_ERROR`. The reader now refills only when that call made no input/output progress and declared compressed input remains. Actual EOF still requires `Z_STREAM_END`; bounds, CRC, length, trailing-data and path checks remain enforced. Decoder failures include the entry name and numeric zlib status.

The synthetic regression failed against the old production reader. After the fix, both AddressSanitizer/UndefinedBehaviorSanitizer core variants pass: valid first/repeated boundaries, an empty final block and exact-boundary completion extract byte-for-byte as disabled imports. Truncated streams, missing final blocks, trailing compressed data in buffered or unread input, invalid blocks, bad CRCs and underdeclared sizes reject without publishing a library entry. Existing core and real HTTP regressions pass as well. Native NRO/NSO builds succeed with the pinned SDK.

No console installation or reproduction of the ZIP boundary fix has been performed for this build.

## On a Switch 2

- kaiHEN launched hbmenu and the graphical Moader app. Native Settings, controller navigation, screenshot/report export, six-digit pairing and browser management were exercised.
- The Switch served its own HTML, CSS, JavaScript and favicon. A separate computer paired, uploaded a ZIP, enabled/disabled a mod and used the normal two-step removal interface.
- The resident service stayed available after Moader exited and after Mario Kart 8 Deluxe was fully closed and reopened. A one-minute monitor recorded 31 successful authenticated reads during the final lifecycle check.
- Native startup, routing and replacement-file read messages reached the browser and native debug screen. Six controlled library changes had both a matching native receipt and a fresh native log entry.
- Actual installed-game names were returned for Mario Kart 8 Deluxe, Pokémon Legends: Z-A, its Switch 2 Edition and Minecraft. The game-name/Title-ID setting synchronized between interfaces. Shared preferences and listener auto-start/port persistence were checked.

## The live-mod test

The original app and resident were tested with one existing Mario Kart 8 Deluxe course-preview texture replaced by a file of the same size. The goal was to observe toggles and removal in one running game.

| Action | Observed preview |
| --- | --- |
| Mod disabled | Original candy-track image |
| Enable | Replacement waterfall/stone-face image |
| Disable | Original image restored |
| Enable again | Replacement image returned |
| Remove while enabled | Original image restored; entry disappeared from the library |

The game stayed open throughout these five states. This preview needed a content reload: **start a race → pause and quit to the main menu → reopen VS Race/course selection**. Returning only to character selection did not visibly reload it.

Native diagnostics confirmed accepted live routing and successful reads from the exact replacement path: 82,024 bytes from offset zero, with a matching sample of the first 64 bytes. The visible transitions were confirmed by the person operating the Switch.

Afterward, Mario Kart was fully closed and reopened normally without a console reboot. The web service remained available, the test entry was removed, and the original preferences were preserved.

## On a development computer

Sanitizer-backed checks cover ZIP/path validation, conflicts, persistence and interrupted-write recovery, API authorization, pairing, metadata parsing, listener shutdown, native catalogue/receipt handling, old/in-flight file ownership, malformed input, 1,000 routing reloads and 1,000 bounded live toggles. The actual native debug producer and C++ reader were tested together against locked, interrupted, stale and corrupt snapshots. Installer tests reject changed prepared files and uncertain transport outcomes.

The interfaces were reviewed with the Impeccable workflow. Source-based captures and Chromium checks cover keyboard/controller flows, shared Settings, Credits/Advanced, long names, narrow layouts, reduced motion and the unified logo. The public illustrations use synthetic sample libraries; they are not game captures.

CI repeats the portable checks listed in [the workflow](../.github/workflows/ci.yml). A successful host check does not replace Switch hardware testing.

### Direct ns2jb host preparation

The direct helper accepts a held ns2jb run directory and a local POSIX Node 24 executable, including Linux/WSL paths with spaces. Tests exercise executable selection, Windows-binary rejection in WSL, conflicting routes, symlink traversal, unique immutable snapshots and the preserved mac2jb route.

The Linux CI integration uses the actual precompiled release bytes and pinned upstream downloads in an isolated kit. It checks preparation and read-only validation, then inspects the printed installation commands. Guards reject Node network/RPC access, companion scripts and native installation during preparation. Static run fixtures test the existing FS proof gate separately; they are not live-device evidence.

No direct Linux/WSL console acquisition or installation, WSL LAN/firewall configuration, or native Windows behavior is claimed as hardware-tested. The real-console results above used macOS/mac2jb. The separate Seedsow source build has a [macOS/Linux SDK path](BUILDING.md#build-from-a-clean-source-checkout); precompiled host-kit setup does not use that SDK.

## Experimental CivSwitch integration

Civilization VII FS access and Civ7 HEN startup are reported by the [reviewed upstream projects](CIV7.md#reviewed-source-revisions). Those are upstream results, distinct from Moader's hardware evidence above.

The initial Moader profile is `civ7-s8n`, pinned to s8n commit `51a804cbce09216e1a42e65929644fd436b4c8bf`. HerXayah and mac2jb CivSwitch runs are not admitted by the initial pin.

Host validation includes:

- Independent checks of the authentic 120-file source closure, upstream source digest, target materialization and unchanged diagnostic RO/process-client ABI.
- 59 setup/wrapper tests, 39 provider/session tests and 39 HEN/package/bootstrap/thread tests, using temporary synthetic data.
- 20 transport tests covering authentication, six-argument calls, bounded replies, truncation, disconnects, timeouts and no replay after failure.
- Actual selected module imports and the checked launch adapter with all console/network/process calls blocked.
- Fresh isolated precompiled-kit preparation for both host profiles using actual public release bytes and pinned upstream downloads, read-only validation and two distinct immutable commands per profile. No printed installation command was executed.
- The existing 38 installer, 19 resident-control and 17 HEN-inspection checks still pass.

These are host results with Node 24. The CI workflow repeats them on Ubuntu. Synthetic records and mocked native replies are never treated as real FS or device proof.

**No Moader acquisition, installation, FS/runtime qualification, HEN transition, resident/browser lifecycle or mod behavior through CivSwitch has been verified on a console.** The same app/HEN binary bytes do not transfer the existing SmileBASIC entrypoint's hardware proof to Civ7. [Experimental workflow and requirements](CIV7.md).

## Limits

- Hardware results apply to the versions, games and workflows named above. The installer rejects unsupported filesystem code or interfaces.
- The Mario Kart check captured a partial replacement read and a visible effect. A whole-file original → replacement → original hash trace remains untested.
- The browser stayed available during a normal game close and relaunch. Recovery after a forced game error remains untested.
- See [How Moader works](RUNTIME.md) for what happens to cached content, open files and the backend after a reboot.
