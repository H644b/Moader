# Moader

Moader manages your Switch 2 mod library from the console or a browser on the same network. Add mods, choose which ones to enable, and check what the game needs to reload.

The console app opens from **hbmenu**. The resident plugin keeps the browser and library available when you close the app. The filesystem runtime makes your enabled mods available to games for the current boot.

## Get started

1. [Download Moader and choose a setup method](docs/INSTALLATION.md).
2. [Pair a browser with your Switch](docs/USAGE.md#connect-a-browser).
3. [Find a mod in the Store](docs/USAGE.md#browse-and-install-from-the-store) or [upload a ZIP](docs/USAGE.md#upload-your-first-mod), then enable it in your Library.

| v4.0.0 download | What's inside |
| --- | --- |
| [Moader app and plugin](https://github.com/H644b/Moader/releases/download/v4.0.0/Moader-v4.0.0.zip) | The two files to copy to SD when you already have a compatible backend. |
| [Moader Setup apps](https://github.com/H644b/Moader/releases/tag/v4.0.0) | Choose the prebuilt macOS, Linux or Windows download for your computer. Each app contains its matching backend files. |
| [Moader backend kit](https://github.com/H644b/Moader/releases/download/v4.0.0/Moader-backend-v4.0.0.zip) | Smaller command-line kit with the app, plugin and backend files. |

[Release notes and checksums](https://github.com/H644b/Moader/releases/tag/v4.0.0)

Choose [Moader Setup](docs/SETUP_GUI.md) or the command-line backend kit; each ZIP works on its own. The console app also has manual and optional automatic release checks under **Settings → Updates**.

| Console app | Browser |
| --- | --- |
| ![Moader game library on the Switch](docs/images/native-workbench.png) | ![Moader game library in a browser](docs/images/web-workbench.png) |

[About the example libraries](docs/images/README.md)

[Supported mod formats](docs/USAGE.md#upload-your-first-mod) · [Troubleshooting](docs/TROUBLESHOOTING.md) · [Test results](docs/VALIDATION.md) · [Build from source](docs/BUILDING.md) · [Contribute](CONTRIBUTING.md)

Moader is licensed under [GPL-2.0-only](LICENSE). See [third-party notices](THIRD_PARTY.md).
