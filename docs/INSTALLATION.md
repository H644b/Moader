# Installing Moader

Moader has two required files. `Moader.nro` is the app you open from the homebrew menu. `moader.nso` is the resident plugin that manages your mods while the app is closed. Install both from the same release.

## Requirements

- A Switch 2 and its SD card.
- A computer that can extract ZIP files and copy files to the card.
- Access to **hbmenu** (the homebrew menu), or one of the [first-time setup methods](#first-time-setup) below.

## 1. Choose a download

| Your setup | Download |
| --- | --- |
| You already have a homebrew setup and want to install or update Moader. | [Moader-v3.0.0.zip](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-v3.0.0.zip) contains the app and plugin. |
| You're setting up Moader for in-game mods for the first time. | [Moader-backend-v3.0.0.zip](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip) includes the same app and plugin plus the files for your [setup method](#first-time-setup). |

## 2. Install Moader

If you downloaded the backend kit for a new setup, go straight to [your method](#first-time-setup). Each method guide includes its own installation steps.

<a name="app-only"></a>
<a name="copy-or-update-the-console-app"></a>

### Install the app and plugin

1. Extract the ZIP you chose on your computer. Close Moader if it is running.
2. Copy **both** files to the SD card. The app ZIP already uses the layout shown below. In the backend kit, find both files inside the extracted `dist/` folder. Create the destination folders on the card if needed.

   | Required file | Destination on the SD card |
   | --- | --- |
   | Console app (`Moader.nro`) | `/switch/Moader.nro` |
   | Resident plugin (`moader.nso`) | `/kaihen/plugins/moader.nso` |

3. Return the card to the Switch. If your backend is running, open **Moader** from **hbmenu** and [verify the installation](#check-your-installation). For a new setup, continue below.

<a name="play-with-mods"></a>
<a name="start-the-backend-for-this-boot"></a>

### First-time setup

The backend makes your enabled mods available to games. Choose the guide for the game you use to start homebrew. Each guide lists its prerequisites, SD files and connection check. Computer-side reference material is in the [developer docs](dev/README.md).

| Setup method | What you need |
| --- | --- |
| [SmileBASIC](SMILEBASIC.md) | A Mac, Linux PC or Windows PC with WSL2 after each full reboot. |
| [Seedsow on-console (standalone)](SEEDSOW_STANDALONE.md) | A computer once to prepare the SD card and profile save. Later boots run on the Switch. In-game mods require a matching filesystem capture from your own setup. |
| [Seedsow Lullaby, PC-driven](SEEDSOW.md) | An Apple Silicon Mac, x86-64 Linux PC or Windows PC with x86-64 WSL2 after each full reboot. |
| [CivSwitch (experimental)](CIV7.md) | A Mac or Linux PC and Civilization VII. This route still needs console verification. |

After a full reboot, start your chosen backend again. The app, plugin and installed mods stay on the SD card.

<a name="check-your-installation"></a>

## 3. Verify the installation

Moader opens to **Game library**. An empty library is normal before you add a mod.

To check in-game support, press the left stick and open **Settings → Advanced → Runtime details**. **Runtime connection** should say **Connected**. If it says **Disconnected** after a full reboot, start your setup method for this boot and check again.

To install your first mod, [pair a browser](USAGE.md#connect-a-browser), then [upload the mod ZIP](USAGE.md#upload-your-first-mod).

## Updating

Finish any uploads, close Moader and replace **both** files with the pair from the new release. Keep the `moader/` folder on the SD card. It holds your library and settings. If the resident service is already running and supports reload, [reload it from Settings](PLUGIN_RELOAD.md#use-it) to activate the new plugin.

[Troubleshoot installation or connection problems](TROUBLESHOOTING.md#browser-or-app-connection)
