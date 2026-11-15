# What has been tested

The first public prerelease is **v0.1.0-dev**. Its app and resident-service binaries are the same bytes used for the device tests below. This is an early release with a deliberately narrow hardware compatibility claim, rather than a promise that every game or mod updates instantly.

**v0.1.0-dev.1** updates the host setup and documentation. Its app, resident, filesystem runtime and patched HEN bytes are unchanged. The existing hardware results apply to those same binaries; the new direct ns2jb entry path has separate host-only coverage.

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

## Limits

- Hardware proof covers the test above, not every game, file format or firmware. The installer checks the supported FS code/ABI and refuses mismatches; it is not a universal jailbreak installer.
- Equal-size loose RomFS replacements can affect future reads. Games may cache their contents. New/resized files, full images and executable changes can require content to reopen or the game to relaunch.
- There is no automatic game-cache invalidation or in-game overlay.
- No original-game SHA-256 or whole-file original → replacement → original trace through one uniquely identified file handle was obtained. The bounded source sample is not a full-read cryptographic hash.
- Normal game close/reopen was tested. Deliberately crashing a game to test recovery was not. A game-only crash is expected to leave the separate service available; a backend/FS/HEN/system failure may need a fresh bootstrap.
- The backend lasts until the Switch reboots. Removed payload paths are retained for recovery and in-flight readers; removal does not claim immediate physical SD erasure.
