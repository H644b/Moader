# Reload the resident service

Reload replaces the running `moader.nso` plugin. The game and the filesystem runtime keep running. Any game cache or loaded code stays in place, so follow the mod's apply state if its content still needs to reload.

## First install

Use a Moader app and resident built for the matching patched **protocol-7 HEN and bootstrap**. If your running HEN doesn't support reload, [set up the matching backend for this boot](GETTING_STARTED.md#start-the-backend-for-this-boot).

## Use it

1. Finish mod uploads and imports. Copy the compatible replacement to `kaihen/plugins/moader.nso` on the SD card. If you also updated the app, close it before [copying the NRO](GETTING_STARTED.md#copy-or-update-the-console-app).
2. Open Moader. Choose **Settings → Advanced → Reload Moader service**.
3. Keep the app open until it reconnects or displays a failure. A successful reconnect means the new service passed startup and filesystem checks.
4. Reopen the browser page. If **Start web automatically** is off, press **Y** in Moader to start the listener first. Your saved library, game-copy settings and pairing code stay on SD.

## Errors and recovery

| Message or stage | What to do |
| --- | --- |
| New HEN installation needed | Set up the matching backend for this boot. |
| Busy | Let the import, library write or game-launch transition finish, then try once more. |
| Preflight failed | Read the error and check the replacement file. If shutdown hasn't started, the old service is still running. |
| Failed after exit | The replacement failed to start after the old service stopped. Keep the report. |
| Quarantined or outcome unknown | Save diagnostics and keep the app open. Avoid another reload or a competing foreground writer. |

After a lost reply, the app asks HEN for the existing operation's status. A client timeout leaves the operation's outcome unknown. If Loader restoration is unverified, hold the console's physical **POWER** button for at least 20 seconds to turn it off. Use a corrected build and fresh setup after reboot.

## What the command checks

HEN links the request to the caller, HEN instance, plugin PID and generation, and nonce. It reserves the Loader resources and checks the staged replacement before asking the old plugin to shut down. The old plugin finishes its cooperative shutdown and HEN confirms that its exact PID has exited. HEN publishes the new service only after it sees a complete inventory and a filesystem receipt matching the replacement's new service identity and routing generation.

## Developer command

The homebrew commands are `GetPluginInfo` (23), `ReloadPlugin` (24), `GetPluginReloadStatus` (25), `GetReloadCapabilities` (26) and `GetPluginReloadDiagnostics` (27). See [kaihen.h](../runtime/kaihen/libkaihen/include/kaihen.h) and [reload.h](../runtime/kaihen/libkaihen/include/kaihen/reload.h). Operation status is 56 bytes and the separate diagnostics reply is 64 bytes. Plugins opt in through `pluginServiceStartWithLifecycle`.

The diagnostics query reads a cached snapshot. It reports the recorded PM worker and Loader restoration stages. It performs no Loader, RO or filesystem operations. A zero replacement PID can accompany a run that started a PM worker. See [source changes](KAIHEN_CHANGES.md) and [test results](VALIDATION.md#cartridge-correction-and-plugin-reload).
