# Install Moader

<a name="copy-or-update-the-console-app"></a>

## App only

1. Unzip the [app download](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-v3.0.0.zip).
2. Close Moader. Copy `switch/Moader.nro` to `switch/` on your SD card.
3. Open **hbmenu** (homebrew menu), then **Moader**.

<a name="start-the-backend-for-this-boot"></a>

## Play with mods

Pick a method. The [backend kit](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip) includes the app.

- [SmileBASIC](SMILEBASIC.md) — Mac, Linux or Windows with WSL2.
- [Seedsow on-console (standalone)](SEEDSOW_STANDALONE.md) — computer once. Needs your own matching filesystem capture.
- [Seedsow Lullaby, PC-driven](SEEDSOW.md) — computer each full reboot.
- [CivSwitch (experimental)](CIV7.md) — Mac or Linux.

## Check your installation

Press the left stick. Open **Settings → Advanced → Runtime details**. **Runtime connection** should say **Connected**. [Add a mod](USAGE.md#upload-your-first-mod).

## Updating

Replace the app. Keep `moader/`. [Update the resident service](PLUGIN_RELOAD.md#use-it) if needed.
