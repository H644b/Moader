# Game-card checks

Moader v0.1.0-dev.5 checks game availability before admitting requested replacements to a newly published runtime catalogue. The checks cover the Moader-managed library. They do not launch the game, mount its executable as a probe, change a license, or contact an online ownership service.

## Choose the game's copy

Select a game in the left rail, then open **Settings → Game copy**. The choice is saved immediately for that game and shared between the controller app and browser.

| Choice | Behavior |
| --- | --- |
| Unspecified / Check inserted card | Require a positively matching current card and a second consistent card observation. Uncertain identity remains blocked. |
| Cartridge / Cartridge or game-key card | Require this game's matching card, including when the game data was downloaded. |
| Digital / My digital copy | Check installed noncard base Program data based on your explicit declaration that you use a card-independent copy. This is not automatic ownership or license verification. |

Game-key cards still need their physical card after their game data is downloaded, as [Nintendo explains](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68415/p/172). Do not choose Digital for a game-key copy. If you own both types, choose the copy you intend to use. A mistaken Digital selection can defeat the intended physical-presence guard; Moader cannot establish ownership from the selected setting.

The safe default can block legitimate digital copies until you select the correct type. Existing enabled requests are preserved when updating; they are not silently reclassified as digital.

## When the card is absent

The mod shows **Game unavailable** and explains whether the card is missing, another card is inserted, the base Program is unavailable, or a query could not verify availability. It stays in the library, and its requested toggle stays saved. Its replacement files are excluded from newly published routing before the backend opens or validates those payloads.

An unsuccessful inventory scan also withholds all requested replacements using the retained list; it does not declare that list current. Availability ticks retry failed catalogue publication, while only a complete successful SD scan clears the inventory warning.

The resident rechecks availability in its owner loop, normally once a second plus query/work time. Foreground mode rechecks while the app is open. Insertion or correcting the selected copy can make the saved request eligible again. Read the detail afterward: a publication failure, pending FS acknowledgement and acknowledged routing without the mod are different outcomes.

This does not eject or reload a game, undo code already loaded into its process, clear its asset cache, or revoke older pinned filesystem objects instantly. Already loaded changes can require a scene/content reload or game relaunch. A failed withdrawal can leave previous routing active and is reported explicitly. Checks are bounded observations, not an atomic lease preventing a card from being removed immediately afterward.

## What the implementation checks

For Unspecified and Cartridge, the provider checks NS insertion and the complete bounded list of applications on the card, resolves the routed title to its verified owner, requires a match, then repeats the card observation. It does not require a cartridge to appear in NS's installed-content list or perform NCM content probes on this path. This checks physical presence; the platform still enforces the game's executable, update and download dependencies.

For explicit Digital, the provider checks complete NS content-status observations and NCM metadata/storage. It distinguishes Application bases from patches/DLC and verifies the full base-owner Program content ID at offset zero is present on built-in or SD storage. It repeats the content observation before allowing. Opaque rights flags are not interpreted as proof of digital ownership.

Names and availability share a mutex around NS lifecycle/query sequences. Both foreground and resident publishers use the provider; a UI-only toggle cannot assert a card is present. Query failures and unsupported or ambiguous identities fail closed with their stage/result in the detail.

Exact application records take precedence. The existing bounded record-backed NX/Ounce relationship supports delta 0 and the demonstrated Edition index 1; Edition control fields are checked before using its base owner. The Digital path checks the base owner's Program index zero. An ACD index is never treated as an executable Program index. These checks establish the selected presence condition, not complete launch readiness or compatibility of every update, Edition dependency, game-key download or mod executable.

The supported combined installer starts with an empty checked catalogue before attaching the native filesystem runtime, then starts the publisher. It does not admit the separate upstream loader's legacy `contents/` or global-patch collections. Those standalone upstream workflows do not gain Moader's gate by copying these application files.

Relevant upstream interfaces: [NS declarations](https://switchbrew.github.io/libnx/ns_8h.html), [NS implementation](https://github.com/switchbrew/libnx/blob/master/nx/source/services/ns.c), and [NCM implementation](https://github.com/switchbrew/libnx/blob/master/nx/source/services/ncm.c). The pinned libounce SDK used for Moader remains the compile-time authority.

## Validation and crash limits

The production provider and publisher have sanitizer-backed host fixtures using real SDK declarations and fake service replies. They cover card/source choices, Program presence, errors, identity mapping, changing observations and cleanup. The dev.4 resident was installed and reached the web interface on a console. With the Z-A card inserted, its gate passed the card/owner checks but then reported the base Program unavailable. This exposed the extra installed-content requirement on the physical path. Raw installed-content rows were not captured, so the exact NS/NCM response behind that failure is unknown.

The dev.5 correction has host regressions for matching cards with empty or patch-only installed lists, plus wrong/absent/changing cards and required-query failures. They assert that the physical path makes no installed-content or NCM calls. The first candidate accepted the matching Z-A cartridge on device. Missing-card withdrawal and insertion/removal transitions remain untested there. That candidate's separate plugin reload failed; the subsequent rebuilt repair has offline validation only and remains unreleased. A service denied on a particular policy stays blocked rather than guessing availability. See [Validation](VALIDATION.md).

A missing cartridge has not been established as the cause of the reported fatal Minecraft/hbmenu system restart. This guard is preventive behavior; this release does not claim to diagnose or repair that fatal.
