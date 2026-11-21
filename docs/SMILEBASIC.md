# Installing Moader with SmileBASIC

Use this method if your Switch 2 uses SmileBASIC for homebrew. You need the computer again after each full reboot. Your Moader library and settings stay on the SD card.

## Requirements

- A Switch 2 with SmileBASIC, its SD card and a supported computer.
- The [v3.0.0 backend kit](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip). It includes both the Moader app and the required resident plugin.

## Choose your computer

| Computer | SmileBASIC method in the kit |
| --- | --- |
| macOS | mac2jb or direct ns2jb |
| Linux | direct ns2jb |
| Windows | direct ns2jb from Windows Subsystem for Linux 2 (WSL2) |

## Install and verify

1. Extract the backend kit on your computer and open `START_HERE.txt`. In the published v3.0.0 kit, the SmileBASIC computer notes are in `docs/INSTALLATION.md`. Use the section matching your computer.
2. After setup, confirm the app is at `/switch/Moader.nro` and the plugin at `/kaihen/plugins/moader.nso` on the SD card. Open **Moader** from **hbmenu**.
3. Press the left stick and open **Settings → Advanced → Runtime details**. When **Runtime connection** says **Connected**, you can [upload your first mod](USAGE.md#upload-your-first-mod).

[Installation troubleshooting](TROUBLESHOOTING.md#browser-or-app-connection)
