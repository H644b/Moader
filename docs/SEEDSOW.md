# Install Moader with Seedsow Lullaby (PC-driven)

## Requirements

- A working Seedsow homebrew setup for the **Demo** or **full game**. Use the edition and profile you prepared. The two editions use different saves.
- An Apple Silicon Mac, an x86-64 Linux PC or a Windows PC with x86-64 WSL2.
- Your Switch 2 SD card and the [Moader backend kit](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip).

## Install for your edition

1. On the computer you use with Seedsow, extract the backend kit and open its `dist/` folder. These app files are the same for the Demo and full game.
2. Close Moader if it is running. Copy `Moader.nro` to `/switch/Moader.nro` and `moader.nso` to `/kaihen/plugins/moader.nso` on the SD card. Both files are required.
3. Return the card to the Switch. With the PC-driven backend running for your edition and profile, open **Moader** from **hbmenu**.
4. Press the left stick and open **Settings → Advanced → Runtime details**. **Runtime connection** should say **Connected**.

## After a full reboot

Use your computer with the same Seedsow edition to start the backend again for this boot. Your installed files and mod library stay on SD. To update Moader, replace both files with the pair from the new release.

[Upload your first mod](USAGE.md#upload-your-first-mod) · [Troubleshooting](TROUBLESHOOTING.md#browser-or-app-connection)
