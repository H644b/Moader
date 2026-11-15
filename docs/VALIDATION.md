# What has been tested

The first public prerelease is **v0.1.0-dev**. Its app and resident-service binaries are the same bytes used for the device tests below. This is an early release with a deliberately narrow hardware compatibility claim, rather than a promise that every game or mod updates instantly.

**v0.1.0-dev.1** updates the host setup and documentation. Its app, resident, filesystem runtime and patched HEN bytes are unchanged. The existing hardware results apply to those same binaries; the new direct ns2jb entry path has separate host-only coverage.

**v0.1.0-dev.2** adds the experimental Civ7 host integration and documentation with unchanged app/service/HEN bytes and app display version `0.1.0-dev`. Host qualification is separate from console acceptance; no new Civ7 console validation is claimed.

**v0.1.0-dev.3** fixes the compressed ZIP reader and rebuilds the app/resident. HEN and the native filesystem runtime are unchanged. The new app/resident binaries have **host validation only**. The console results below were obtained with the original binaries used through v0.1.0-dev.2; they have not been repeated on this rebuilt pair.

**v0.1.0-dev.4** rebuilds the app/resident for cached inventory, scan recovery, native error separation and game-card gating. It retains dev.3's ZIP fix. The pair was initially released with host validation only. A later dev.4 console installation reached hbmenu, the resident web interface and accepted runtime receipts with the saved 11-entry library. Z-A was incorrectly blocked after matching-card checks passed but the extra base-Program predicate failed; raw NS/NCM rows were not captured. Correct insertion/removal behavior and the original fatal restart were not verified. Original live-mod results below remain evidence for the earlier pair.

## Cartridge correction and plugin reload in v0.1.0-dev.5

This release rebuilds the app, resident service, HEN and flat bootstrap. The native filesystem runtime is unchanged. The new HEN/bootstrap pair and reload command have **host validation only** until an explicit console acceptance record is added. The earlier live MK8 result does not validate this new plugin-reload path.

The physical guard accepts a positively matched card only after a second consistent card observation. Production-provider regressions cover Z-A's validated Edition/owner relationship with empty and patch-only installed lists and assert zero installed-content/NCM calls for matching physical copies. Absent/wrong/changing cards, failed required queries, unsupported identities and count bounds still block. Digital retains full installed noncard base-Program checks.

The actual resident entry point runs with real Library, RuntimeBackend, WebServer and InstanceLock code and fake Horizon transport. Checks exercise cooperative reservation, listener shutdown, asynchronous abort/resume, a new explicit request, matching commit and guaranteed post-reply acknowledgement. A contended internal acknowledgement cannot be dropped as Busy. An injected plugin-stop failure retains the live Context and writer lock; the child verifies ownership before an atomic process exit rather than treating the failed join as cleanup.

A separate sanitizer-backed producer/reader integration destroys and reconstructs the actual C++ Library/RuntimeBackend with a new epoch while preserving enabled requests, preferences, pairing, generation and the existing receipt inode. One live instance of the actual C FS reader consumes the emitted catalogues: corrupt replacement publication keeps previous routing and reports an error, the valid new epoch accepts the same generation, retained CodeFS files remain readable, and 1,000 unchanged wakes remain bounded. Exact native-generated receipts are then read by the actual C++ backend. This is host integration using staged catalogue files, not a device process-reload demonstration.

HEN tests run the actual reload orchestration and lifecycle helper/server code with controlled process, transport and clock adapters. They check old command layouts and the complete request envelope, selected identity/nonce guards, idempotency, lifecycle Busy retries, asynchronous abort and post-reply commit notification, failure/restoration outcomes and quarantine. The native client's production state machine checks single submission, lost replies with query-only recovery, identity/generation guards, old-HEN/foreground behavior and reconnect after healthy publication. The production renderer has ready/progress/legacy/failure fixtures. No controller or device performance claim follows from host captures.

Installer tests independently bind both HEN/bootstrap components to source and patch digests, preserve immutable prepared inputs, reject old/mixed pairs and retain the existing no-replay/uncertain-outcome guard. Synthetic descriptor/build/package tests are distinct from rebuilding and inspecting the final release bytes.

Still required for device acceptance: the new bootstrap's retained PM capability after the acquisition host closes; reload while Moader's hbmenu host remains tracked; confirmed old/new PIDs, fresh health and browser/app reconnection; unchanged HEN/FS lifetimes; repeated reload/resource stability; and subsequent native game launch. A console reboot is required to install this new command once. No old diagnostic RPC session can add it to the installed protocol-6 HEN.

## Library recovery and game-card checks in v0.1.0-dev.4

Actual production Library/ZIP code runs under AddressSanitizer and UndefinedBehaviorSanitizer with deterministic injected stat, directory-open and directory-read faults. An 800-file synthetic library remains available without payload stat calls during state polling. A decoder pause verifies snapshots, settings and busy-write rejection complete before extraction resumes. An already-enabled cartridge request is withdrawn while the decoder remains paused; the completed import merges the current cache and does not resurrect the withdrawn route. Failed complete scans retain the prior list/generation without publishing partial routing; initial unavailability is distinct from an empty library. Manual external additions invalidate old selections, and stable rescans avoid redundant publication.

Actual native dispatcher and renderer checks cover stale selection errors versus healthy/genuine runtime results, retained and initially unavailable inventory, controller copy selection, persistence and input/generation bounds. Real loopback HTTP checks cover authenticated source selection and rescan, cross-origin rejection, retained state during a failed scan and recovery. ZIP inputs cannot set the title-level copy choice.

The production availability provider uses real SDK declarations with fake NS/NCM calls. Fixtures cover absent/matching/wrong/multi-title cards, key-card data, explicit Digital mode, update-only/missing/partial base content, Program presence, service failures, count limits, changing snapshots, exact application IDs and bounded NX/Ounce/validated Edition identity mapping, including the Z-A relationship. Lifecycle checks balance service/database/storage cleanup. No real service permission, game-card response, digital license, title launch or console test is implied by these fixtures.

Blocked mods are omitted before backend payload validation. Composed regressions cover an SD scan error followed by failed routing withdrawal, retry with unchanged availability, retention of the inventory warning and recovery only after a full successful scan. Withdrawal details remain qualified by publication/receipt state. The supported installer initializes an empty catalogue before attaching the FS runtime and admits no separate legacy mod plan. Already open mounts, preexisting loaded code and a card removed immediately after an observation remain outside a universal instantaneous-revocation guarantee. [Policy and source references](CARTRIDGES.md).

The fatal Minecraft/hbmenu system restart remains unconfirmed. Reproducing manager defects and fixing them does not establish that fatal's cause. No card contents, game payloads, device captures, pairing values or personal data are published with these tests.

## ZIP import fix in v0.1.0-dev.3

[Issue #4](https://github.com/H644b/Moader/issues/4) identified a valid DEFLATE stream that Moader rejected when compressed input and expanded output exhausted their 32 KiB buffers together. A following empty-input pump returned `Z_BUF_ERROR`. The reader now refills only when that call made no input/output progress and declared compressed input remains. Actual EOF still requires `Z_STREAM_END`; bounds, CRC, length, trailing-data and path checks remain enforced. Decoder failures include the entry name and numeric zlib status.

The synthetic regression failed against the old production reader. After the fix, both AddressSanitizer/UndefinedBehaviorSanitizer core variants pass: valid first/repeated boundaries, an empty final block and exact-boundary completion extract byte-for-byte as disabled imports. Truncated streams, missing final blocks, trailing compressed data in buffered or unread input, invalid blocks, bad CRCs and underdeclared sizes reject without publishing a library entry. Existing core and real HTTP regressions pass as well. Native NRO/NSO builds succeed with the pinned SDK.

No console installation or reproduction of the ZIP boundary fix has been performed for this build. This import failure is separate from the reported Z-A game/system crash; no fix for that crash is claimed.

## On a Switch 2

- kaiHEN launched hbmenu and the graphical Moader app. Native Settings, controller navigation, screenshot/report export, six-digit pairing and browser management were exercised.
- The Switch served its own HTML, CSS, JavaScript and favicon. A separate computer paired, uploaded a ZIP, enabled/disabled a mod and used the normal two-step removal interface.
- The resident service stayed available after Moader exited and after Mario Kart 8 Deluxe was fully closed and reopened. A one-minute monitor recorded 31 successful authenticated reads during the final lifecycle check.
- Native startup, routing and replacement-file read messages reached the browser and native debug screen. Six controlled library changes had both a matching native receipt and a fresh native log entry.
- Actual installed-game names were returned for Mario Kart 8 Deluxe, Pokémon Legends: Z-A, its Switch 2 Edition and Minecraft. The game-name/Title-ID setting synchronized between interfaces. Shared preferences and listener auto-start/port persistence were checked.

## The live-mod test

One existing, equal-size Mario Kart 8 Deluxe course-preview texture was used. The replacement and game assets are **not distributed**.

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

No direct Linux/WSL console acquisition or installation, WSL LAN/firewall configuration, or native Windows behavior is claimed as hardware-tested. The real-console results above used macOS/mac2jb. Native source SDK provisioning remains the documented Apple Silicon macOS build path; precompiled host-kit setup does not use that SDK.

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

- Hardware proof covers the test above, not every game, file format or firmware. The installer checks the supported FS code/ABI and refuses mismatches; it is not a universal jailbreak installer.
- Equal-size loose RomFS replacements can affect future reads. Games may cache their contents. New/resized files, full images and executable changes can require content to reopen or the game to relaunch.
- There is no automatic game-cache invalidation or in-game overlay.
- No original-game SHA-256 or whole-file original → replacement → original trace through one uniquely identified file handle was obtained. The bounded source sample is not a full-read cryptographic hash.
- Normal game close/reopen was tested. Deliberately crashing a game to test recovery was not. A game-only crash is expected to leave the separate service available; a backend/FS/HEN/system failure may need a fresh bootstrap.
- The backend lasts until the Switch reboots. Removed payload paths are retained for recovery and in-flight readers; removal does not claim immediate physical SD erasure.
