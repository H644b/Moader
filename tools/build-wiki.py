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
    'Installation': 'docs/INSTALLATION.md',
    'SmileBASIC': 'docs/SMILEBASIC.md',
    'Seedsow-Demo': 'docs/SEEDSOW.md',
    'Seedsow-Standalone': 'docs/SEEDSOW_STANDALONE.md',
    'CivSwitch-Entrypoint': 'docs/CIV7.md',
    'Controls': 'docs/CONTROLS.md',
    'Mod-Files': 'docs/MOD_FILES.md',
    'FAQ': 'docs/FAQ.md',
    'Developer-Docs': 'docs/dev/README.md',
    'Usage-Reference': 'docs/dev/USAGE.md',
    'Troubleshooting-Reference': 'docs/dev/TROUBLESHOOTING.md',
    'Game-Copy-Reference': 'docs/dev/CARTRIDGES.md',
    'Service-Reference': 'docs/dev/PLUGIN_RELOAD.md',
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


def convert(content, source, repository, *, bundled_guides=None):
    """Keep links to available guides local, using the published Wiki for the rest of an app ZIP."""
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
        if bundled_guides is not None and relative in bundled_guides:
            return match.group()
        if image:
            destination = 'https://raw.githubusercontent.com/' + repository + '/main/' + quote(relative, safe='/')
        elif relative in names:
            destination = names[relative] + suffix
            if bundled_guides is not None:
                destination = 'https://github.com/' + repository + '/wiki/' + destination
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
    pages['Home'] = '''# Moader user guide

Moader manages Switch 2 mods from your controller or a paired browser.

## Installation

[Install Moader](Installation) covers requirements, downloads, SD card paths and first launch. Install the console app and resident plugin together. For in-game mods, [choose your setup method](Installation#first-time-setup) or go straight to the [SmileBASIC guide](SmileBASIC).

## Using Moader

- [Pair a browser and upload your first mod](Using-Moader#connect-a-browser).
- [Browse games and manage installed mods](Using-Moader).
- [Install a mod from the SD card](Mod-Files).
- [Troubleshoot problems](Troubleshooting).
'''
    pages['_Sidebar'] = f'''**Moader documentation**

- [Home](Home)
- [Installation](Installation)
  - [App and plugin](Installation#install-the-app-and-plugin)
  - [First-time setup](Installation#first-time-setup)
  - [SmileBASIC](SmileBASIC)
  - [Seedsow on-console (standalone)](Seedsow-Standalone)
  - [Seedsow Lullaby, PC-driven](Seedsow-Demo)
  - [CivSwitch (experimental)](CivSwitch-Entrypoint)
  - [Updates](Installation#updating)
- [Using Moader](Using-Moader)
- [Controls and settings](Controls)
- [Troubleshooting](Troubleshooting)
- [Frequently asked questions](FAQ)

[Developer docs](Developer-Docs) · [Downloads]({base}/releases/tag/{tag})
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
