# Install Moader with Seedsow on-console (standalone)

A computer is needed once to prepare the installation. Later boots use the Switch, its SD card and the same Seedsow profile.

## Requirements

- A Seedsow standalone SD card and a profile save prepared for your edition.
- The [v3.0.0 backend kit](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip) and a computer that can write to the card.
- For in-game mods, a matching filesystem capture from your own setup. The on-console check used the Demo. The full game still needs verification.

## One-time app and plugin installation

1. On your computer, extract the backend kit and open `dist/`.
2. With Moader closed, put `Moader.nro` at `/switch/Moader.nro` and `moader.nso` at `/kaihen/plugins/moader.nso` on the prepared card. Both are required. If your setup already placed this release's pair there, keep those files.
3. Return the card to the Switch. With the standalone backend running on the profile you prepared, open **hbmenu → Moader**.
4. Press the left stick. Under **Settings → Advanced → Runtime details**, check that **Runtime connection** says **Connected**.

## Later boots and updates

After a full reboot, use the same SD card and prepared profile to start your standalone environment on the Switch. Open Moader and check the connection again. For an update, replace the app and plugin together and keep the `moader/` folder with your library.

[Upload your first mod](USAGE.md#upload-your-first-mod) · [Troubleshooting](TROUBLESHOOTING.md#browser-or-app-connection)
