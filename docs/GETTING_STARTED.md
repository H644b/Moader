# Install Moader

Moader opens from **hbmenu**, the homebrew menu on your Switch 2. If Moader already appears there, open it and [check your installation](#check-your-installation). If you're setting up Moader for use in games for the first time, [choose your backend method](#start-the-backend-for-this-boot).

## Copy or update the console app

1. Download and extract the [Moader v3.0.0 console ZIP](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-v3.0.0.zip) on your computer.
2. Close Moader if it is running. Copy `switch/Moader.nro` from the ZIP to the `switch/` folder at the top of your SD card. If you use the resident service, also copy `kaihen/plugins/moader.nso` to the matching folder on the card.
3. Open **Moader** from hbmenu. You should see **Game library**. An empty library is normal before you add your first mod.

The app can manage your library and serve the browser while it stays open. The resident service keeps the browser available after you close the app. To use mods in a game, the filesystem runtime must also be connected.

## Start the backend for this boot

The **backend** runs on the Switch and makes mod files available to games. Start it once after each full reboot. Your library and preferences stay on the SD card.

For first-time setup, download **Moader-backend-v3.0.0.zip** on your computer. The kit also includes the console app and resident service. If your method has already copied them to SD, [check your installation](#check-your-installation). [v3.0.0 release downloads](https://github.com/H644b/Moader/releases/tag/v3.0.0)

| Setup method | When you need a computer |
| --- | --- |
| [Seedsow on-console (standalone)](SEEDSOW_STANDALONE.md) | Once to prepare the SD card and profile save. Later boots run on the Switch. For in-game mods, v3.0.0 accepts one exact filesystem capture. You supply that file from your own setup. |
| [Seedsow Lullaby, PC-driven](SEEDSOW.md) | At each full boot, using an Apple Silicon Mac, x86-64 Linux or Windows with x86-64 WSL2. The Demo and full game use different saves. |
| SmileBASIC | At each full boot. The mac2jb option uses macOS. Direct ns2jb supports macOS, Linux and Windows with WSL2. Computer-specific notes are in the installation reference that comes with the kit. |
| [CivSwitch (experimental)](CIV7.md) | A Mac or Linux computer is needed. Moader installation through Civilization VII still awaits a console check. |

## Check your installation

Open **Moader** from hbmenu. **Game library** means the app is ready to manage mods. To check support for mods in games, press the left stick and choose **Settings → Advanced → Runtime details**. Look for **Runtime connection** set to **Connected**. If it says **Disconnected**, your library is still available while the app is open.

To add a mod, press **ZR** for **Connection**. Follow the [browser pairing steps](USAGE.md#connect-a-browser), then [upload your first mod](USAGE.md#upload-your-first-mod).

## Updating

- **App update.** Close Moader, replace the app file and reopen it from hbmenu. Keep your existing `moader/` folder on SD.
- **Resident service update.** Use the app and plugin from the same release. After copying the plugin, open **Settings → Advanced → Reload Moader service** if your running backend supports it. [Service reload](PLUGIN_RELOAD.md#use-it)
- **New backend version or a full reboot.** Use your chosen [setup method](#start-the-backend-for-this-boot). The standalone method uses the prepared SD card and save on later boots.

[Installation troubleshooting](TROUBLESHOOTING.md#browser-or-app-connection) · [Device test results](VALIDATION.md)
