# Moader

This development branch contains **unreleased 0.1.2-dev.0 host-exit diagnostics**. They add observation, not a verified crash repair. The [diagnostic guide](docs/HOST_EXIT_DIAGNOSTICS.md) explains the capture workflow; published v0.1.1 assets remain unchanged.

> **v0.1.1 is a release with a known system crash.** The game/host-exit lifecycle can produce `2011-0102` before invoking Reload. Installation/startup passed with the corrected installer, but full lifecycle acceptance has failed. Read the [release notes](docs/releases/v0.1.1.md) and [validation record](docs/VALIDATION.md) before testing.

**Manage Switch 2 mods from your controller or a browser.**

Moader gives kaiHEN users a shared mod library, ZIP uploads, enable/disable controls and native debug output. Its resident backend keeps the web interface available while you play or close the Moader app. The browser interface is served by your Switch; no cloud account is needed.

## Interface previews

| Console app | Browser companion |
| --- | --- |
| ![Console workbench with a synthetic sample library](docs/images/native-workbench.png) | ![Browser workbench with synthetic sample mods](docs/images/web-workbench.png) |

*These are synthetic sample libraries in the actual interfaces, not device or game captures. [Image provenance](docs/images/README.md).*

## Download

The current release is **[v0.1.1](https://github.com/H644b/Moader/releases/tag/v0.1.1)**. It includes every change since `v0.1.0-dev`: cross-platform setup, experimental Civ7 integration, ZIP and library fixes, corrected game-card checks, and guarded plugin reload. Use the [matching installation guide](https://github.com/H644b/Moader/blob/v0.1.1/docs/INSTALLATION.md). The reproducible game-exit crash remains unresolved.

| Download | Use it for |
| --- | --- |
| [Moader-v0.1.1.zip](https://github.com/H644b/Moader/releases/download/v0.1.1/Moader-v0.1.1.zip) | The published console app and matching resident plugin |
| [Moader-backend-v0.1.1.zip](https://github.com/H644b/Moader/releases/download/v0.1.1/Moader-backend-v0.1.1.zip) | Published backend setup kit; pinned dependencies are fetched at first use |

Use the matching backend kit for the supported setup path below. The backend kit includes Moader and the matched precompiled patched HEN. Setup fetches the pinned official kaiHEN archive and ns2jb sources at first use, so initial setup needs internet access. End users do not need to install an SDK or compile HEN.

**After updating:** run the matching resident from this release, then select a game and open **Settings → Game copy**. Cartridge/game-key copies need their matching card. For an actual digital copy, choose **My digital copy**. The default checks the inserted card and blocks uncertain availability; it does not infer ownership from downloaded data. [Game-card checks and limits](docs/CARTRIDGES.md).

## Supported setup

Choose either route to obtain the fresh, held SmileBASIC/FS session that Moader needs:

| Host route | Entry tool | Guide |
| --- | --- | --- |
| Linux or WSL2; also optional on macOS | [kurumuz's pinned ns2jb fork](https://gitlab.com/kurumuz/ns2jb) with native Node 24 | [Direct ns2jb setup](docs/INSTALLATION.md#route-a-linuxwsl-or-macos-with-ns2jb) |
| macOS | [mac2jb](https://github.com/cpypwn/mac2jb#readme) and its bundled Node 24 | [mac2jb setup](docs/INSTALLATION.md#route-b-macos-with-mac2jb) |

The precompiled kit needs Python 3.10+ and the route's Node 24; Git is only needed to obtain the direct ns2jb checkout. WSL uses Linux Node and both terminals in the same distro; [configure its LAN access before acquisition](docs/INSTALLATION.md#wsl2-networking-before-acquisition).

Use the chosen upstream tool's current console/application requirements. A stock-console installer is not included; CivSwitch has a separate [experimental s8n host profile](docs/CIV7.md), with no Moader Civ7 hardware validation. HerXayah and mac2jb CivSwitch runs are not admitted by that profile. The direct Linux/WSL instructions follow pinned source and helper contracts; they do not add a new Linux/WSL console-verification claim to the existing macOS/mac2jb hardware results.

**kaiHEN** is the homebrew environment; **hbmenu** is the launcher for homebrew apps; **FS** means the filesystem service, whose verified access is needed to install Moader's backend. Everyday mod management uses the running backend rather than repeating that setup.

## Choose your starting point

**Already running a compatible Moader resident backend this boot?** Copy `Moader.nro` into the SD card's `switch/` folder and open it from hbmenu. It connects to the resident service. See [app installation](docs/INSTALLATION.md#copy-or-update-the-console-app).

**Setting up the backend for this boot?** Use the backend kit with a **fresh, verified held FS session** from either route above. Backend setup starts the resident service and installs filesystem routing once for that boot. Follow [backend setup](docs/INSTALLATION.md#start-the-backend-for-this-boot).

Copying the NRO alone installs the interface. It does not activate the resident service or filesystem runtime. Likewise, copying `moader.nso` to SD stages a plugin; it does not replace a running plugin.

## Everyday use

1. Open Moader. Press **Y** to turn the web interface on.
2. Press **ZR** for its address and six-digit pairing code.
3. On a computer or phone on the same network, open the displayed address, such as `http://YOUR_SWITCH_IP:7712`, and enter the code exactly, including leading zeros.
4. Upload a mod ZIP for the correct game Title ID. New imports start **disabled**.
5. Enable the mod and read its **apply state**. Some changes need content to reopen or the game to relaunch.

**ZL** opens diagnostics. **+** returns to hbmenu. With the resident backend running, the browser stays available after the app exits. [Full controls and ZIP examples →](docs/USAGE.md)

Settings are saved on the Switch and shared by both interfaces: game names/Title IDs, reduced motion, log following, web startup and port. **Advanced** provides diagnostics; **Credits** lists the projects behind Moader.

## What has been tested?

On real hardware with the original app/resident binaries, one equal-sized Mario Kart 8 Deluxe course-preview texture changed from the usual candy image to a distinct replacement preview, back to candy, back to the replacement, then back to candy after removal—all within the same running game, without a console reboot.

The preview needed a scene reload: **start a race → pause/quit to the main menu → VS Race/course selection**. Returning only to character selection did not refresh it. Native debugging worked in the app and browser, and **31/31 authenticated web checks** succeeded across a normal game close/reopen interval.

This is a bounded test of one loose RomFS replacement. Game caches, new/resized files, images and executable mods have different apply requirements. See [apply requirements](docs/RUNTIME.md) and the [sanitized validation summary](docs/VALIDATION.md).

## Learn more

- [Wiki](https://github.com/H644b/Moader/wiki) — browse all user guides
- [Installation](docs/INSTALLATION.md) — app copy, both backend setup routes and updates
- [kaiHEN modifications](docs/KAIHEN_CHANGES.md) — complete source patch, reload protocol, upstream base and HEN/bootstrap hashes
- [CivSwitch / Civilization VII](docs/CIV7.md) — experimental entrypoint, exact source pin and validation limits
- [Usage](docs/USAGE.md) — controller controls, pairing, ZIP layout and settings
- [Troubleshooting](docs/TROUBLESHOOTING.md) — connection, apply-state and recovery help
- [How it works](docs/RUNTIME.md) — the three components and their limits
- [Validation](docs/VALIDATION.md) — bounded hardware results and host-test coverage
- [Building](docs/BUILDING.md) · [Maintaining](docs/MAINTAINING.md) — contributor documentation

Found a problem? Check [troubleshooting](docs/TROUBLESHOOTING.md#reporting-a-problem), then [open an issue](https://github.com/H644b/Moader/issues).

Moader is **GPL-2.0-only**. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY.md). Game files and the validation texture are not included.
