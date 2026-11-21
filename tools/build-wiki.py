#!/usr/bin/env python3
"""Build GitHub Wiki pages from the same guides shipped in Moader's repository."""
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit
import argparse
import json
import re

ROOT = Path(__file__).resolve().parent.parent
PAGES = {
    'Release-v3.0.0': 'docs/releases/v3.0.0.md',
    'Release-v0.2.5': 'docs/releases/v0.2.5.md',
    'Release-v0.2.0': 'docs/releases/v0.2.0.md',
    'Release-v0.1.3': 'docs/releases/v0.1.3.md',
    'Release-v0.1.2': 'docs/releases/v0.1.2.md',
    'Release-v0.1.1': 'docs/releases/v0.1.1.md',
    'Changelog': 'CHANGELOG.md',
    'Installation': 'docs/GETTING_STARTED.md',
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
    # Keep generic validation limits from shared policy pages while using the
    # same software-error wording as the user guides.
    content = content.replace(
        'Forced game crashes and crash recovery are not hardware-qualified by this prerelease.',
        'Recovery after forced game errors has not been hardware-qualified.')
    content = content.replace(
        'Forced crashes and crash recovery have not been hardware-tested.',
        'Recovery after forced termination has not been hardware-tested.')

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

## Start here

1. [Install Moader](Installation). If it already opens from hbmenu, [check the connection](Installation#check-your-installation).
2. [Pair a browser](Using-Moader#connect-a-browser) on your phone or computer.
3. [Upload your first mod](Using-Moader#upload-your-first-mod) and enable it for the matching game.

## Find what you need

| Guide | Covers |
| --- | --- |
| [Installation](Installation) | App and backend downloads, setup methods, updates and what to repeat after a reboot |
| [Using Moader](Using-Moader) | Controller controls, game selection, browser pairing and mod management |
| [Game-card checks](Game-Card-Checks) | Selecting your digital or physical copy |
| [Service reload](Plugin-Reload) | Updating a compatible running service |
| [Troubleshooting](Troubleshooting) | Help with the app, browser, imports and setup |
| [Frequently asked questions](FAQ) | Common questions about setup and playing with mods |
| [Release notes]({base}/releases/tag/{tag}) · [Changelog](Changelog) | Downloads for {tag} and the history of earlier versions |

## Interface previews

| Console app | Browser |
| --- | --- |
| ![Console workbench](https://raw.githubusercontent.com/{repository}/main/docs/images/native-workbench.png) | ![Browser workbench](https://raw.githubusercontent.com/{repository}/main/docs/images/web-workbench.png) |

The screenshots show sample libraries in the console app and paired browser. [About the screenshots]({base}/blob/main/docs/images/README.md)

## Project links

[Report a problem]({base}/issues/new/choose) · [Ask a question]({base}/discussions) · [Device test results](Validation) · [Source code]({base})
'''
    pages['FAQ'] = '''# Frequently asked questions

## Do I need a Mac?

Linux and Windows with WSL2 are options for several computer-assisted methods. Seedsow on-console (standalone) uses a computer for initial preparation. The [installation guide](Installation#start-the-backend-for-this-boot) lists supported hosts and requirements for each method.

## Do I need FS access every time I toggle a mod?

Start the backend once after a full reboot. During that boot you can upload, enable or remove mods. Your library and preferences stay on the SD card.

## Can I use Civilization VII?

Moader's CivSwitch method is experimental. Its installation on a console still needs verification. [Compare the setup methods](Installation#start-the-backend-for-this-boot)

## Where do mods go on SD?

Use `/moader/library/TITLE_ID/mod-folder/romfs/`, with a lowercase Title ID and lowercase `romfs`. Each mod has its own folder. Browser ZIP uploads create this structure automatically. See [SD card layout](Using-Moader#sd-card-layout).

## How is kaiHEN modified?

Moader publishes the complete kaiHEN source patch, upstream revision and shipped HEN/bootstrap hashes. The [kaiHEN modification disclosure](kaiHEN-Modifications) explains the read-only trace query and cooperative plugin reload, separately from Moader's FS hook. [Reload instructions](Plugin-Reload).

## Can I close Moader while playing?

Yes, with the resident backend running. Press **+** to return to hbmenu; the independent service keeps the browser available. In foreground mode the listener belongs to the app and closes with it.

## Why is the game unchanged after enabling a mod?

Read the apply state. A request may still be Pending, or a game may have cached its content. Same-sized replacements of existing loose files can affect later reads. New/resized files, images and executable changes can need new mounts or a game relaunch. [Apply requirements](How-Moader-Works#what-can-change-while-a-game-runs) explain the differences.

## Is live changing actually tested?

One equal-sized Mario Kart 8 Deluxe course-preview texture changed through enable, disable, re-enable and removal while the same game remained running. Its cached preview refreshed after a race, quitting to the main menu and reopening course selection. This establishes that workflow, rather than every game or mod type. [Validation](Validation).

## Does Remove erase the mod's files immediately?

Removal hides the entry and updates routing. Payloads stay available for older open filesystem objects and recovery. It is recoverable library removal, not an immediate disk purge. Avoid manually deleting retained payloads during the active boot.

## Does a game error require another FS setup?

A game-only error is expected to leave the separate backend available. If the browser still connects, disable/remove the offending mod and relaunch the game. Recovery after a forced game error has not been hardware-tested. An FS/HEN/system failure or console reboot can invalidate the backend. [Software-error recovery](Troubleshooting#a-mod-caused-a-software-error).

## Can I show game names instead of Title IDs?

Open **Settings → Show game names**. Missing names fall back to the real Title ID; details always retain the ID. The display preference does not change which game receives a mod.

## Why does a mod say Game unavailable?

Select the game and open **Settings → Game copy**. Cartridge and game-key copies require their matching inserted card. Digital is your explicit copy selection, not an ownership check. Enabled requests stay saved while new routing omits unavailable games. A failed query is reported separately from a known missing card. See [Game-card checks](Game-Card-Checks) for acknowledgement, cached-content and host-only validation limits.

## Where is the pairing code?

Press **ZR** for **Connection**. If the web listener is off, select **Start listener**. Open the displayed address on your local network and enter all six digits, including leading zeros. [Pair a browser](Using-Moader#connect-a-browser)

## How do I report a problem?

Start with [Troubleshooting](Troubleshooting#reporting-a-problem). Include the release, steps, exact error/apply state, and whether content or the game was reopened. Remove pairing values, session commands, personal paths and network details from excerpts. Do not upload game assets, firmware, keys or saves.
'''
    pages['_Sidebar'] = f'''**Moader**

- [Home](Home)
- [Install Moader](Installation)
- [Check your installation](Installation#check-your-installation)
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

- [Release {tag}]({base}/releases/tag/{tag})
- [Changelog](Changelog)
- [Building](Building)
- [Maintaining](Maintaining)
- [Contributing](Contributing)
- [Credits and licenses](Credits-and-Licenses)
- [kaiHEN modifications](kaiHEN-Modifications)
- [Security](Security)
'''
    pages['_Footer'] = f'[Moader]({base}) · [Download {tag}]({base}/releases/tag/{tag}) · [Issues]({base}/issues) · [Discussions]({base}/discussions)\n'
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
