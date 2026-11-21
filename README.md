# Moader

Moader manages Switch 2 mods from a controller or a web browser. Import mods, turn them on or off, and see what the console has applied. The Switch serves the browser interface directly.

The **Moader app** opens from hbmenu, the homebrew launcher. It can manage the library while open. The **backend** loads mods into games and keeps browser access running after you close the app. Set up the backend once after each full reboot.

## Prebuilt downloads for v3.0.0

| I want to… | Download | Start here |
| --- | --- | --- |
| Open or update the console app | [Moader-v3.0.0.zip](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-v3.0.0.zip) | [Copy the app](docs/INSTALLATION.md#copy-or-update-the-console-app) |
| Set up the backend for this boot | [Moader-backend-v3.0.0.zip](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip) | [Choose a setup method](docs/INSTALLATION.md#start-the-backend-for-this-boot) |
| Update a compatible running service | [Moader-v3.0.0.zip](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-v3.0.0.zip) | [Reload the resident service](docs/PLUGIN_RELOAD.md#use-it) |

The backend kit includes the matched Seedsow RO/HEN/bootstrap set, the pinned GUI helper, and the [experimental on-console A/B implementation](docs/SEEDSOW_STANDALONE.md) with its verified standalone bootstrap prebuilt. One Profile B Demo A boot reached `KH READY` with an independently read, connected FS acknowledgement; after publication the owner reported another A boot opening Moader v3.0.0 with runtime and FS connected. B reached hbmenu on a separate boot without Moader FS. Preparing A still requires a locally qualified FS capture: the kit cannot bundle console-derived FS bytes. SmileBASIC and experimental CivSwitch use the same kit with their own source profiles. [PC-driven Seedsow guide](docs/SEEDSOW.md) · [Release notes and checksums](https://github.com/H644b/Moader/releases/tag/v3.0.0) · [Validation scope](docs/VALIDATION.md)

## Use Moader

[Connect a browser and import a mod](docs/USAGE.md). Moader accepts RomFS files and images, ExeFS modules and images, and binary IPS patches. Close and relaunch the game to apply executable changes. Some loose RomFS replacements can take effect the next time a running game reads the file.

| Console app | Browser |
| --- | --- |
| ![Moader mod library on the console](docs/images/native-workbench.png) | ![Moader mod library in a browser](docs/images/web-workbench.png) |

[About the example libraries in these images](docs/images/README.md)

[Find a fix](docs/TROUBLESHOOTING.md) · [How it works](docs/RUNTIME.md) · [Test results](docs/VALIDATION.md) · [Build from source](docs/BUILDING.md) · [Contribute](CONTRIBUTING.md)

Moader is licensed under [GPL-2.0-only](LICENSE). See [third-party notices](THIRD_PARTY.md).
