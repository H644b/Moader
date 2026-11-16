#!/usr/bin/env python3
"""Build GitHub Wiki pages from the same guides shipped in Moader's repository."""
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit
import argparse
import json
import re

ROOT = Path(__file__).resolve().parent.parent
PAGES = {
    'Release-v0.1.1': 'docs/releases/v0.1.1.md',
    'Changelog': 'CHANGELOG.md',
    'Installation': 'docs/INSTALLATION.md',
    'CivSwitch-Entrypoint': 'docs/CIV7.md',
    'Using-Moader': 'docs/USAGE.md',
    'Game-Card-Checks': 'docs/CARTRIDGES.md',
    'Plugin-Reload': 'docs/PLUGIN_RELOAD.md',
    'Troubleshooting': 'docs/TROUBLESHOOTING.md',
    'How-Moader-Works': 'docs/RUNTIME.md',
    'Validation': 'docs/VALIDATION.md',
    'Building': 'docs/BUILDING.md',
    'Maintaining': 'docs/MAINTAINING.md',
    'Credits-and-Licenses': 'THIRD_PARTY.md',
    'kaiHEN-Modifications': 'docs/KAIHEN_CHANGES.md',
    'Contributing': 'CONTRIBUTING.md',
    'Security': 'SECURITY.md',
}
LINK = re.compile(r'(!?)\[([^\]\n]*)\]\(([^()\s]+)\)')


def convert(content, source, repository):
    """Keep wiki navigation local and point source/assets to the main repository."""
    names = {value: key for key, value in PAGES.items()}

    def replace(match):
        image, label, raw = match.groups()
        url = urlsplit(raw)
        if url.scheme or url.netloc or raw.startswith('#'):
            return match.group()
        target = (source.parent / unquote(url.path)).resolve()
        if not target.is_relative_to(ROOT) or not target.exists():
            raise ValueError('Missing or external local guide target in ' + str(source.relative_to(ROOT)))
        relative = target.relative_to(ROOT).as_posix()
        suffix = ('#' + url.fragment) if url.fragment else ''
        if image:
            destination = 'https://raw.githubusercontent.com/' + repository + '/main/' + quote(relative, safe='/')
        elif relative in names:
            destination = names[relative] + suffix
        else:
            kind = 'tree' if target.is_dir() else 'blob'
            destination = 'https://github.com/' + repository + '/' + kind + '/main/' + quote(relative, safe='/') + suffix
        return image + '[' + label + '](' + destination + ')'

    return LINK.sub(replace, content)


def build():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/wiki')
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or output in ROOT.parents:
        raise ValueError('Use a separate wiki output directory')
    release = json.loads((ROOT / 'config/release.json').read_text())
    repository, tag = release['repository'], release['tag']
    base = 'https://github.com/' + repository
    pages = {name: convert((ROOT / path).read_text(), ROOT / path, repository)
             for name, path in PAGES.items()}
    pages['Home'] = f'''# Moader user guide

Manage Switch 2 mods from your controller or a browser. Moader serves its web interface directly from the Switch, with a six-digit pairing code and no cloud account.

**Known issue in {tag}:** system error `2011-0102` has been reproduced after closing a homebrew host and launching Minecraft, before invoking Reload. Read the [release notes](Release-v0.1.1) and [validation record](Validation); publication does not establish a fix.

## Start here

1. **[Install Moader](Installation)** — choose the kurumuz ns2jb route on Linux/WSL, or ns2jb/mac2jb on macOS. The precompiled backend kit needs no compiler.
2. **[Learn the controls](Using-Moader#controller-controls)** — navigate the app, open Settings and turn the web interface on.
3. **[Connect a browser](Using-Moader#connect-a-browser)** — open the displayed Switch address and pair.
4. **[Import your first mod](Using-Moader#upload-your-first-mod)** — select the correct game Title ID, upload a ZIP, then enable it and read its apply state.

## Find what you need

| Guide | Covers |
| --- | --- |
| [v0.1.1 release notes](Release-v0.1.1) | Download choices, every change since v0.1.0-dev and the unresolved host-exit failure |
| [Changelog](Changelog) | Release history and the unpublished dev.5 candidate |
| [Installation](Installation) | Downloads, Linux/WSL and macOS setup, once-per-boot backend installation and updates |
| [CivSwitch entrypoint](CivSwitch-Entrypoint) | Existing Civ7 FS access, exact source profile, prerequisites and Moader validation status |
| [Using Moader](Using-Moader) | Every controller shortcut, browser pairing, ZIP formats/limits, removal, Settings, Advanced and Credits |
| [Troubleshooting](Troubleshooting) | Network/pairing problems, game names, pending changes, cache reloads, crashes and setup recovery |
| [Game-card checks](Game-Card-Checks) | Per-game copy selection, missing cards, game-key downloads, digital choices and verification limits |
| [Plugin reload](Plugin-Reload) | Reload the resident service, read progress, reconnect and recover from a failed replacement |
| [How Moader works](How-Moader-Works) | Console app, resident service, filesystem runtime, apply states and component lifetimes |
| [Frequently asked questions](FAQ) | Quick answers about reboot, live changes, removal and game errors |
| [Validation](Validation) | What was tested on hardware and what the host checks establish |
| [kaiHEN modifications](kaiHEN-Modifications) | The complete patch, plugin reload protocol, upstream base and shipped HEN/bootstrap hashes |

## Interface previews

| Console app | Browser |
| --- | --- |
| ![Console workbench](https://raw.githubusercontent.com/{repository}/main/docs/images/native-workbench.png) | ![Browser workbench](https://raw.githubusercontent.com/{repository}/main/docs/images/web-workbench.png) |

These are synthetic sample libraries rendered by the actual interfaces, rather than game or device captures. [Image provenance]({base}/blob/main/docs/images/README.md).

## Before expecting an in-game change

An enabled request and accepted routing are different from a visible game effect. Supported later file reads can change live, but a game may keep an asset in memory until a scene reload. Executable changes require a game relaunch. The running backend lasts until the console reboots; the saved library and preferences remain on SD.

## Project links

[Download {tag}]({base}/releases/tag/{tag}) · [Report a problem]({base}/issues/new/choose) · [Ask a question]({base}/discussions) · [Source code]({base})

Contributors: [Building](Building), [Maintaining](Maintaining), [Contributing](Contributing), [Security](Security), [Credits and licenses](Credits-and-Licenses).
'''
    pages['FAQ'] = '''# Frequently asked questions

## Do I need a Mac?

Use the direct kurumuz ns2jb route on Linux or WSL. macOS users can choose ns2jb or mac2jb. Both routes use the same precompiled Moader backend kit. [Installation](Installation) covers the host prerequisites and WSL networking.

## Do I need FS access every time I toggle a mod?

Backend installation uses a fresh verified held FS session once per boot. After installation, upload, enable, disable and removal use the resident service. A console reboot needs fresh backend setup; it does not erase your library.

## Can Civilization VII provide FS access?

Yes. CivSwitch has an upstream FS route. Moader's separate integration has an exact source profile and validation limits; see [CivSwitch entrypoint](CivSwitch-Entrypoint). Loader readiness on port 9026 alone is not FS readiness, and another fork's session is not interchangeable.

## Where do mods go on SD?

Use `/moader/library/TITLE_ID/mod-folder/romfs/`, with a lowercase Title ID and lowercase `romfs`. Each mod has its own folder. Browser ZIP uploads create this structure automatically. See [SD card layout](Using-Moader#sd-card-layout).

## How is kaiHEN modified?

Moader publishes the complete kaiHEN source patch, upstream revision and shipped HEN/bootstrap hashes. The [kaiHEN modification disclosure](kaiHEN-Modifications) explains the pending-launch fix, read-only trace query and cooperative plugin reload, separately from Moader's FS hook. [Reload instructions](Plugin-Reload).

## Can I close Moader while playing?

Yes, with the resident backend running. Press **+** to return to hbmenu; the independent service keeps the browser available. In foreground mode the listener belongs to the app and closes with it.

## Why is the game unchanged after enabling a mod?

Read the apply state. A request may still be Pending, or a game may have cached its content. Same-sized replacements of existing loose files can affect later reads. New/resized files, images and executable changes can need new mounts or a game relaunch. [Apply requirements](How-Moader-Works#what-can-change-while-a-game-runs) explain the differences.

## Is live changing actually tested?

One equal-sized Mario Kart 8 Deluxe course-preview texture changed through enable, disable, re-enable and removal while the same game remained running. Its cached preview refreshed after a race, quitting to the main menu and reopening course selection. This establishes that workflow, rather than every game or mod type. [Validation](Validation).

## Does Remove erase the mod's files immediately?

Removal hides the entry and updates routing. Payloads stay available for older open filesystem objects and recovery. It is recoverable library removal, not an immediate disk purge. Avoid manually deleting retained payloads during the active boot.

## Does a game crash require another FS setup?

A game-only error is expected to leave the separate backend available. If the browser still connects, disable/remove the offending mod and relaunch the game. Deliberate crash recovery has not been hardware-tested. An FS/HEN/system failure or console reboot can invalidate the backend. [Crash recovery](Troubleshooting#a-mod-caused-a-software-error-or-game-crash).

The v0.1.1 host-exit/next-game issue has produced a system fatal before invoking Reload. It remains unresolved; see [release notes](Release-v0.1.1) and [Validation](Validation).

## Can I show game names instead of Title IDs?

Open **Settings → Show game names**. Missing names fall back to the real Title ID; details always retain the ID. The display preference does not change which game receives a mod.

## Why does a mod say Game unavailable?

Select the game and open **Settings → Game copy**. Cartridge and game-key copies require their matching inserted card. Digital is your explicit copy selection, not an ownership check. Enabled requests stay saved while new routing omits unavailable games. A failed query is reported separately from a known missing card. See [Game-card checks](Game-Card-Checks) for acknowledgement, cached-content and host-only validation limits.

## Where is the pairing code?

Turn Web ON with **Y**, then press **ZR**. Enter all six digits, including leading zeros, at the displayed address. Keep the code private. Use the listener on a trusted local network.

## How do I report a problem?

Start with [Troubleshooting](Troubleshooting#reporting-a-problem). Include the release, steps, exact error/apply state, and whether content or the game was reopened. Remove pairing values, session commands, personal paths and network details from excerpts. Do not upload game assets, firmware, keys or saves.
'''
    pages['_Sidebar'] = '''**Moader**

- [Home](Home)
- [v0.1.1 release notes](Release-v0.1.1)
- [Changelog](Changelog)
- [Install / Linux / WSL / macOS](Installation)
- [CivSwitch entrypoint](CivSwitch-Entrypoint)
- [App and browser usage](Using-Moader)
- [SD card layout](Using-Moader#sd-card-layout)
- [Settings and Advanced](Using-Moader#settings)
- [Game-card checks](Game-Card-Checks)
- [Debugging and screenshots](Using-Moader#debugging-and-screenshots)
- [Troubleshooting](Troubleshooting)
- [How it works](How-Moader-Works)
- [FAQ](FAQ)
- [Validation](Validation)

**Project**

- [Building](Building)
- [Maintaining](Maintaining)
- [Contributing](Contributing)
- [Credits and licenses](Credits-and-Licenses)
- [kaiHEN modifications](kaiHEN-Modifications)
- [Security](Security)
'''
    pages['_Footer'] = f'[Moader]({base}) · [Downloads]({base}/releases/tag/{tag}) · [Issues]({base}/issues) · [Discussions]({base}/discussions)\n\nDocs for {tag}; app version `{release["version"]}`.\n'
    for name, content in pages.items():
        for _, _, target in LINK.findall(content):
            url = urlsplit(target)
            if not url.scheme and not url.netloc and url.path and url.path not in pages:
                raise ValueError('Broken internal wiki link in ' + name + ': ' + url.path)
    output.mkdir(parents=True, exist_ok=True)
    for name, content in pages.items():
        target = output / (name + '.md')
        if target.is_symlink():
            raise ValueError('Wiki output contains a symlink')
        target.write_text(content.rstrip() + '\n')
    print('Built ' + str(len(pages)) + ' linked GitHub Wiki pages in ' + str(output))


if __name__ == '__main__':
    try:
        build()
    except (OSError, ValueError) as error:
        raise SystemExit('Wiki generation stopped: ' + str(error))
