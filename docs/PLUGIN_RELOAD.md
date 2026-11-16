# Reload the resident plugin

**Unreleased and not accepted on hardware.** The first candidate failed during reload. The repaired candidate completed two reloads and returned to hbmenu, but a subsequent normal Minecraft launch caused system error `2011-0102`. HOME recovered after restarting. The instructions below describe the implementation under investigation, not an approved update workflow. Keep this branch uninstalled until the complete lifecycle check passes. [Detailed evidence](VALIDATION.md).

The patched kaiHEN provides **ReloadPlugin**, an asynchronous command that restarts one registered, cooperative plugin. Moader exposes it in **Settings → Advanced → Reload Moader service**. It uses ordinary homebrew IPC after installation; a new FS acquisition is not part of each reload.

## First install

This feature needs the matching protocol-7 HEN, patched bootstrap, resident plugin and Moader app. An older running HEN cannot gain a command from a file copied to SD. Install the matching backend kit through the normal fresh, current-boot FS setup once. The filesystem hook itself is unchanged.

After that, an updated `kaihen/plugins/moader.nso` can be loaded by the reload command. Keep the app and resident from the same release. Existing plugins without the lifecycle handshake cannot be reloaded this way; HEN rejects them before stopping them.

## Use it

1. Finish uploads and imports. Save changes before leaving any other tool that writes Moader files.
2. Open Moader and choose **Settings → Advanced → Reload Moader service**.
3. Follow the progress message. Keep Moader open until it reconnects or reports a specific failure. Pressing the action repeatedly does not submit another reload.
4. A successful reload reconnects the app to the replacement service. A browser connection closes during the restart. The replacement uses **Start web automatically**; if that setting is off, press **Y** to start its listener again, then reconnect with the saved pairing code. Saved library requests, game-copy choices and preferences stay on SD.

Reload restarts the manager service, not the running game. It does not clear game caches or apply executable changes that require a game relaunch. FS-owned routing and retained game filesystem objects remain in the FS process while the resident restarts.

## What the command checks

HEN binds the request to the calling homebrew process, its HEN instance, the selected plugin's PID and generation, and a monotonically increasing nonce. It reserves one reload at a time because plugin startup shares Loader resources with game launching. A busy game-launch transition rejects reload; it is not forced aside.

Before requesting shutdown, HEN stages the selected fixed-path NSO and checks its format/ranges, complete reads and staged bytes. It builds a separate launch package. These checks cannot prove that every executable will initialize successfully. The old service must support cooperative shutdown, deny an active import, stop its listener and reserve library writes. Its owner exits only after the shutdown acknowledgement is sent. HEN waits for that exact old PID to disappear before launching one replacement.

The replacement must complete its startup handshake and application health checks. For Moader, health includes the complete inventory and a successful FS receipt matching its new service epoch and catalogue generation. Creating IPC threads alone is insufficient. The new PID and generation are published only after health succeeds.

## Errors and recovery

- **New HEN installation needed:** the running environment has the older command set. Install the matched backend kit; copying the app alone cannot add reload support.
- **Busy:** an import, library write or game-launch transition is using a required resource. Finish that activity and make a new explicit attempt.
- **Preflight failure:** the replacement or required capability failed validation before shutdown. Read the result; the existing plugin remains running when shutdown has not begun.
- **Failed after exit:** the old plugin has stopped, but its replacement did not become healthy. The selected service is unavailable. HEN reports the failed stage and cleanup result; it does not claim rollback.
- **Quarantined / outcome unknown:** cleanup, process exit or Loader restoration could not be proved. Do not keep submitting reloads or start a competing foreground writer. Save the diagnostic report and screen while the app is still open. If Loader restoration is unverified, force the console off by holding its physical POWER button for at least 20 seconds; returning to hbmenu is not a recovery step. Reinstall only a corrected build after a fresh per-boot setup.

If a reply is lost, the app queries HEN's recorded operation rather than replaying shutdown or launch. A client-side timeout does not cancel work or establish that a process exited. HEN keeps authoritative status after the requesting app disconnects.

## Developer command

The additive homebrew commands are `GetPluginInfo` (23), `ReloadPlugin` (24), `GetPluginReloadStatus` (25), `GetReloadCapabilities` (26) and `GetPluginReloadDiagnostics` (27). The C helpers and fixed request/reply layouts are in [kaihen.h](../runtime/kaihen/libkaihen/include/kaihen.h) and [reload.h](../runtime/kaihen/libkaihen/include/kaihen/reload.h). Existing command numbers and scalar reply sizes remain compatible. Operation status remains 56 bytes; the separate diagnostics reply is 64 bytes. Plugins opt in through `pluginServiceStartWithLifecycle`; ordinary plugins keep the existing start API.

Diagnostics query a cached snapshot. They perform no Loader, RO or filesystem operations in HEN's query handler. The saved app report includes launch stage, whether the PM worker was created/started/finished/joined, and separate primary/restoration gate steps and results. A zero replacement PID alone cannot establish that a PM worker never started. Probe fields describe the last cached observation, not an independent restoration check.

The command, installer and health paths have separate validation boundaries. See [Validation](VALIDATION.md) for the current build's host checks and console status, and [kaiHEN modifications](KAIHEN_CHANGES.md) for the complete patch and provenance.
