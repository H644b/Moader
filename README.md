# Moader

Moader manages Switch 2 mods from a controller or a web browser. Import mods, turn them on or off, and see what the console has applied. The Switch serves the browser interface directly.

The **Moader app** opens from hbmenu, the homebrew launcher. It can manage the library while open. The **backend** loads mods into games and keeps browser access running after you close the app. Set up the backend once after each full reboot.

## Prebuilt downloads for v0.2.5

| I want to… | Download | Start here |
| --- | --- | --- |
| Open or update the console app | [Moader-v0.2.5.zip](https://github.com/H644b/Moader/releases/download/v0.2.5/Moader-v0.2.5.zip) | [Copy the app](docs/INSTALLATION.md#copy-or-update-the-console-app) |
| Set up the backend for this boot | [Moader-backend-v0.2.5.zip](https://github.com/H644b/Moader/releases/download/v0.2.5/Moader-backend-v0.2.5.zip) | [Choose a setup method](docs/INSTALLATION.md#start-the-backend-for-this-boot) |
| Update a compatible running service | [Moader-v0.2.5.zip](https://github.com/H644b/Moader/releases/download/v0.2.5/Moader-v0.2.5.zip) | [Reload the resident service](docs/PLUGIN_RELOAD.md#use-it) |

The backend kit includes the matched Seedsow RO/HEN/bootstrap set and the helper that provisions the pinned patched GUI. Its current-process gate accepts Seedsow Lullaby Demo and full game IDs; only the Demo has a completed Moader installation record. v0.2.5 reduces the resident's BSD session use during game launch. SmileBASIC and experimental CivSwitch use the same kit with their own source profiles. [Seedsow guide](docs/SEEDSOW.md) · [Release notes and checksums](https://github.com/H644b/Moader/releases/tag/v0.2.5) · [Validation scope](docs/VALIDATION.md)

## Use Moader

[Connect a browser and import a mod](docs/USAGE.md). Moader accepts RomFS files and images, ExeFS modules and images, and binary IPS patches. Close and relaunch the game to apply executable changes. Some loose RomFS replacements can take effect the next time a running game reads the file.

| Console app | Browser |
| --- | --- |
| ![Moader mod library on the console](docs/images/native-workbench.png) | ![Moader mod library in a browser](docs/images/web-workbench.png) |

[About the example libraries in these images](docs/images/README.md)

[Find a fix](docs/TROUBLESHOOTING.md) · [How it works](docs/RUNTIME.md) · [Test results](docs/VALIDATION.md) · [Build from source](docs/BUILDING.md) · [Contribute](CONTRIBUTING.md)

Moader is licensed under [GPL-2.0-only](LICENSE). See [third-party notices](THIRD_PARTY.md).
