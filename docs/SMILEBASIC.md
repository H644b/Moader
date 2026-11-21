# Install Moader with SmileBASIC

## Requirements

Use a Switch 2 with a working SmileBASIC homebrew setup, its SD card and a computer. Use the same computer-side method that your SmileBASIC setup already uses.

| Computer | SmileBASIC method |
| --- | --- |
| Mac | mac2jb or direct ns2jb |
| Linux | direct ns2jb |
| Windows | direct ns2jb in Windows Subsystem for Linux 2 (WSL2) |

## Install the app and plugin

1. Download the [Moader backend kit](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip) to your computer and extract it. Open `dist/` inside the extracted folder.
2. Close Moader if it is open. Copy **both required files** from `dist/` to the SD card. Create the destination folders if needed.

   | File | Destination on the SD card |
   | --- | --- |
   | `Moader.nro` | `/switch/Moader.nro` |
   | `moader.nso` | `/kaihen/plugins/moader.nso` |

3. Return the SD card to the Switch. Once your SmileBASIC homebrew environment and Moader backend are running for this boot, open **hbmenu → Moader**.

## Verify and use Moader

The app opens to **Game library**. Press the left stick and open **Settings → Advanced → Runtime details**. When **Runtime connection** says **Connected**, you can [pair a browser and upload a mod](USAGE.md#connect-a-browser).

After a full reboot, use your computer again to start your SmileBASIC environment for the new boot. Your files stay on SD. Copy both files again when updating to another Moader release.
