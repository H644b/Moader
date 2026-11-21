# Install Moader with CivSwitch (experimental)

CivSwitch uses Civilization VII and a Mac or Linux computer. An end-to-end Moader installation through CivSwitch still needs verification on a console. [CivSwitch test results](VALIDATION.md#experimental-civswitch-integration)

## Install on an existing CivSwitch setup

1. Download the [v3.0.0 backend kit](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip) to your Mac or Linux PC. Extract it and open the `dist/` folder.
2. Close Moader. Copy the app to `/switch/Moader.nro` and the required resident plugin to `/kaihen/plugins/moader.nso` on the SD card. Use the pair from the same release.
3. Return the card to the Switch. With your CivSwitch homebrew environment and backend running, open **Moader** from **hbmenu**.

## Check the result

Press the left stick and open **Settings → Advanced → Runtime details**. Record what **Runtime connection** shows. **Game library** confirms the app opened. **Connected** is the separate check for in-game support. If you report a result, include your Moader version and the displayed status.

After a full reboot, the files stay on the SD card. Check the runtime again with your CivSwitch environment running for the new boot. When updating Moader, replace the app and plugin together.

[Installation troubleshooting](TROUBLESHOOTING.md#browser-or-app-connection)
