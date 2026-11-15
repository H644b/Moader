# Troubleshooting

Start with the message in Moader's **ZL debug view** or browser debug panel. Note whether the problem is the app connection, web listener, runtime acknowledgement or the game's visible content; those are different stages.

## The app works, but the browser stops when I close it

You may be using **foreground mode**. That listener belongs to the open app. Resident browser access during gameplay requires the matching resident service and filesystem runtime for the current boot. Follow [backend setup](INSTALLATION.md#start-the-backend-for-this-boot).

Copying `Moader.nro` installs the app. Copying `moader.nso` only stages a plugin; neither action replaces a running backend or installs live FS hooks.

## The browser cannot connect

- Press **Y** to make sure the web listener is on, then **ZR** for its current address.
- Open that exact **HTTP** address on a device on the same local network. The default form is `http://YOUR_SWITCH_IP:7712`.
- If you changed the web port, stop/start the listener and use the new displayed URL.
- Check whether a guest network, client isolation or VPN is separating the devices.
- If the console rebooted, its previously running backend is gone; use the supported fresh setup for the new boot.

A connection failure alone does not identify a crash, sleep state or changed address. Keep the last error rather than assuming which component stopped. Use the app's reconnect prompt if available.

## The pairing code is rejected

Enter exactly the six digits displayed on the Switch, including leading zeros. A code copied from another installation/console may not match. After several incorrect attempts, wait **30 seconds** before trying again. Keep the code out of screenshots, issues and shared logs.

## The game name is missing or wrong

Moader reads installed metadata when it can. Unsupported or unavailable metadata falls back to the real **Title ID**. Check the ID in details before uploading or enabling a mod; changing the display setting does not redirect the mod to another title. Use **Settings → Show game names** to compare names and IDs.

## My mod says Pending

The request is saved, but matching runtime acknowledgement has not arrived. Check the runtime connection and native debug output. Confirm that the app/plugin/FS runtime come from the same supported release and that backend setup completed for this boot.

Do not interpret a saved toggle as proof that a game read the replacement. Avoid repeating privileged setup while an earlier attempt is unresolved.

## Routing is accepted, but the game looks unchanged

Read the apply-state details:

- An existing same-sized loose replacement can affect later reads, but an already cached asset needs a real content/scene reload.
- Returning to a shallow menu may not reload an asset. In the tested MK8 preview, **race → pause/quit/main menu → VS Race/course selection** worked; character selection alone did not.
- New paths, changed file sizes and full images can require a new mount or a game relaunch.
- Executable modules and patches require a game relaunch, including after disable/removal.

Check the correct game/version, Title ID and internal file path. Native debug output can show whether the mod source was actually read. The one validated texture workflow does not establish every game's reload behavior.

## My ZIP is rejected

Use the supported [ZIP layout](USAGE.md#upload-your-first-mod). The archive must contain a supported payload, such as `romfs/`, optionally within one enclosing folder. Check the size limits, unsafe/linked paths and duplicate/conflicting files. Text IPSwitch scripts need conversion outside Moader; this release accepts binary IPS patches.

If two enabled mods replace the same path, disable one and review the conflict. A successful import still does not establish game/version compatibility.

## I removed a mod, but its files remain on SD

Removal is recoverable. Moader hides the entry and publishes the changed routing while preserving payload paths for older open filesystem objects. It is **not a physical disk purge**. Do not manually rename/delete retained payloads during the active boot to try to force removal; reopen content or relaunch the game as its apply state requires.

## Native debug lines are missing

Use matching components from the current release. The product reads closed native report/event snapshots; an open writer log is not the file you should try to unlock manually. An app-only update cannot add this feature to an older running FS runtime.

Check whether the runtime is connected, whether backend setup completed, and whether native rows update after a management request. Preserve the displayed error if snapshots are unavailable rather than changing the live writer file or replaying setup.

## A mod caused a software error or game crash

For a **game-only** crash, the separate resident service/FS runtime is expected to remain available. If the browser still connects, disable/remove the offending mod and relaunch the game. A normal game-only error should not itself require reacquiring FS.

Deliberate crash recovery was not tested. FS, HEN or system crashes—and a console reboot—can invalidate the backend context and require fresh qualified setup. If the service is unavailable, preserve the error/report and establish the current console state before choosing recovery.

## Backend setup failed or timed out

Stop and keep the helper's report. A timed-out native operation may already have completed; rerunning the old command can be unsafe. Do not edit proof flags, erase journals, replay old handles or reuse an earlier boot's session to bypass the checks.

Follow the release's supported recovery instructions. Backend installation needs a **fresh verified current-boot mac2jb session** when a new setup is required. Routine mod upload/enable/disable/removal uses the running backend and does not require repeated FS acquisition. See [Installation](INSTALLATION.md).

## Reporting a problem

[Open an issue](https://github.com/H644b/Moader/issues) with:

- Moader release and whether the matched backend was set up this boot.
- What you did, expected result and actual result.
- The exact on-screen error/apply state and whether the browser still connects.
- Whether a scene reload or game relaunch changes the result.
- A small, sanitized diagnostic excerpt or screenshot when useful.

Remove pairing codes, session commands, network addresses, personal paths and credentials before posting. Do not attach game files, proprietary textures or saves. Describe the mod type and relevant behavior instead.
