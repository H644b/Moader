# How Moader modifies kaiHEN

Moader publishes its modified kaiHEN source, the exact patch, the upstream revision and the shipped binary hash. The published HEN changes touch **four files**. A comparison with the pinned upstream tree found no additional changed, added or missing kaiHEN source files.

| Record | Value |
| --- | --- |
| Upstream project | [ouncebrew/kaihen](https://github.com/ouncebrew/kaihen) |
| Source base | [`e897f991524590833b82fbe8d63a3c6775fb8101`](https://github.com/ouncebrew/kaihen/tree/e897f991524590833b82fbe8d63a3c6775fb8101) |
| Official release inputs | [`20261114c`](https://github.com/ouncebrew/kaihen/releases/tag/20261114c) |
| Exact changes | [kaihen-mo.patch](../software-builds/kaihen-mo.patch) |
| Full modified source | [runtime/kaihen](../runtime/kaihen/) |
| Build and validation record | [kaihen-mo.json](../software-builds/kaihen-mo.json) |
| Patched `hen/main` SHA-256 | `e6341b1932b450e25f04ccf425c3034bd45dd3cccfadbf5e5fb2778b6a673e5c` |

## 1. Keep a pending native homebrew launch usable

An R-button press can occur while SmileBASIC is still the active application, or before opening a Switch 1 game. The upstream launch state could return `UnexpectedState` for an application that was not a supported native homebrew target. That could leave HEN faulted before the intended native game was launched.

In [game_arm.cpp](../runtime/kaihen/hen/source/game_arm.cpp), the unsupported-application branch now returns success while leaving the native launch pending. HEN can wait for a suitable native application. This preserves the existing target eligibility checks; Switch 1 games do not become native hbmenu hosts through this change.

## 2. Expose existing game-launch trace information

Moader adds the read-only HEN command `HenControlCommand_MoaderGameTrace`, with command ID `0x4d4f0001`. It returns the existing `GameTraceSnapshot` so host diagnostics can inspect launch progress and errors.

The three changes are:

| File | Change |
| --- | --- |
| [common/control.h](../runtime/kaihen/common/control.h) | Defines the additional diagnostic command ID. |
| [hen/source/game_arm.hpp](../runtime/kaihen/hen/source/game_arm.hpp) | Adds a const getter for the trace snapshot. |
| [hen/source/server.cpp](../runtime/kaihen/hen/source/server.cpp) | Handles the query and returns the snapshot for a request with no copied handles. |

The query reads diagnostic state. It does not enable a mod, install a filesystem hook or change a running game. Existing HEN command IDs remain in place.

## Relationship to the rest of Moader

The modified HEN starts the homebrew environment. Moader's **resident plugin** manages the library and browser, and its separate **filesystem runtime** redirects supported game reads. The runtime changes derived from ns2jb-modloader live under [runtime/modloader](../runtime/modloader/); they are separate from the four-file HEN patch above.

The normal release also uses the checksum-qualified official RO image, game loader, bootstrap and hbmenu inputs. Their qualification checks are in [install-kaihen.cjs](../tools/install-kaihen.cjs); the precompiled kit includes the specifically modified `hen/main` identified above.

The experimental [CivSwitch host profile](CIV7.md) has separate host adaptations in [host-kaihen.cjs](../tools/host-kaihen.cjs): building the exact RO package, exposing the checked launch operation, retiring the current diagnostic RO, and validating the tagged-thread bootstrap. Civ7 needs a 24-byte try-wait adapter appended to the in-memory bootstrap image; the original `bootstrap.bin` file and patched `hen/main` remain unchanged. The host also selects authenticated target-specific transports. These adaptations are disclosed separately from the four-file HEN source patch and do not establish console validation through Civ7.

## Verify and rebuild

The [build guide](BUILDING.md) explains the pinned compiler/SDK and isolated build. The build helper requires the modified HEN to reproduce the recorded digest before it can populate installation inputs. The release packager checks the app, resident and HEN against [config/release.json](../config/release.json).

Review the complete patch against the exact upstream revision, rather than comparing with a moving default branch. The [validation summary](VALIDATION.md) describes observed hardware behavior and its limits. Upstream licenses and component notices remain in place; see [credits and third-party code](../THIRD_PARTY.md).
