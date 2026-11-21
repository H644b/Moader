# Reload the resident plugin

The resident plugin, `moader.nso`, is a required part of Moader. A compatible running service can reload the updated plugin without a full reboot.

## First install

[Install the app and plugin](INSTALLATION.md#install-the-app-and-plugin) and complete setup for the current boot. Reload is for an existing running service.

## Use it

1. Finish any uploads and close Moader. Copy **both** files from the same release to the SD card. The app goes at `/switch/Moader.nro` and the plugin at `/kaihen/plugins/moader.nso`.
2. Open Moader from **hbmenu**. Press the left stick, then choose **Settings → Advanced → Reload Moader service**.
3. Keep Moader open until it reconnects or displays an error. Once it reconnects, refresh the browser page. If needed, press **ZR** and choose **Start listener**.

## Errors and recovery

| Message or stage | What to do |
| --- | --- |
| New HEN installation needed | Set up the matching backend for this boot. |
| Busy | Let the import, library write or game-launch transition finish, then try once more. |
| Preflight failed | Read the error and check the replacement file. If shutdown hasn't started, the old service is still running. |
| Failed after exit | The replacement failed to start after the old service stopped. Keep the report. |
| Quarantined or outcome unknown | Save diagnostics and keep the app open. Avoid another reload or a competing foreground writer. |

[Detailed reload diagnostics](dev/PLUGIN_RELOAD.md#what-the-command-checks) · [Installation and updates](INSTALLATION.md#updating)
