# Troubleshooting

Start with the message in Moader's **ZL debug view** or browser debug panel. Note whether the problem is the app connection, web listener, runtime acknowledgement or the game's visible content; those are different stages.

## The library lags or temporarily disappears

The v0.1.2 release retains the inventory fixes introduced in v0.1.0-dev.4. Keep app and resident versions matched using the [update instructions](INSTALLATION.md#updating). These fixes keep cached complete inventories available during import and remove recursive payload scans from ordinary state polling. A busy import rejects another library change with an explanation.

If an SD scan fails, the last complete list remains visible with a separate inventory warning. Check SD access, then use **Refresh** in the browser. The resident also retries in its background loop. A successful empty list and an unreadable library are different states; do not erase library folders or saved preferences to troubleshoot an unreadable scan.

## Game unavailable or a missing cartridge

If dev.4 reports **“The base game Program is unavailable”** with the correct Z-A cartridge inserted, that version adds an installed-content requirement after its card check. The dev.5 correction uses repeated matching-card observations for physical copies. Install the matched updated app/resident; selecting Digital just to suppress the error defeats the physical-presence policy. The card still needs to match, and failed or changing card queries remain blocked.

Select the game and open **Settings → Game copy**. Cartridge and game-key copies require a positively matching inserted card. Downloaded update or key-card data alone does not establish that the game is available without its card. Choose Digital only for a real card-independent copy; this is your selection, not Moader verifying ownership.

The requested toggle stays saved while new routing omits the unavailable game's mods. Details explain a missing card, wrong card, missing base Program, unavailable service or uncertain identity. A query failure is not reported as proof that the card is absent. Insert the correct card or resolve the precise error and let availability refresh. Do not guess a different Title ID or change digital selection just to suppress an error. [Game-card checks](CARTRIDGES.md).

If routing withdrawal failed or is awaiting acknowledgement, the detail says so; older routing/open objects can remain in use. Card insertion/removal, service privileges and the new gate have host fixtures only, pending console qualification.

## A red runtime indicator after another operation failed

In v0.1.0-dev.4, runtime status uses the filesystem's result, and a stale selection or failed interface operation has its own error message. A failed scan has a separate inventory warning. A genuine nonzero runtime result remains visible. Read the result and per-mod details instead of treating all three as a backend failure.

## The app works, but the browser stops when I close it

You may be using **foreground mode**. That listener belongs to the open app. Resident browser access during gameplay requires the matching resident service and filesystem runtime for the current boot. Follow [backend setup](INSTALLATION.md#start-the-backend-for-this-boot).

Copying `Moader.nro` installs the app. Copying `moader.nso` only stages a plugin; neither action replaces a running backend or installs live FS hooks.

## The browser cannot connect

- Press **Y** to make sure the web listener is on, then **ZR** for its current address.
- Open that exact **HTTP** address on a device on the same local network. The default form is `http://YOUR_SWITCH_IP:7712`.
- If you changed the web port, stop/start the listener and use the new displayed URL.
- Check whether a guest network, client isolation or VPN is separating the devices.
- If the console rebooted, its previously running backend is gone; use the supported fresh setup for the new boot.

A connection failure alone does not identify a stopped process, sleep state or changed address. Keep the last error rather than assuming which component stopped. Use the app's reconnect prompt if available.

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

If a valid ZIP fails with `ZIP decompression failed.` on v0.1.0-dev through v0.1.0-dev.2, it may hit the decoder's 32 KiB boundary bug. [v0.1.0-dev.3](https://github.com/H644b/Moader/releases/tag/v0.1.0-dev.3) fixes that case and adds the entry name/zlib status to decoder errors. Update both app and service as described in [Updating](INSTALLATION.md#updating); the old resident service continues using its old ZIP reader until it is replaced through supported backend setup. A stored/uncompressed ZIP is a workaround within the normal size limits.

Use the supported [ZIP layout](USAGE.md#upload-your-first-mod). The archive must contain a supported payload, such as `romfs/`, optionally within one enclosing folder. Check the size limits, unsafe/linked paths and duplicate/conflicting files. Text IPSwitch scripts need conversion outside Moader; this release accepts binary IPS patches.

If two enabled mods replace the same path, disable one and review the conflict. A successful import still does not establish game/version compatibility.

## I removed a mod, but its files remain on SD

Removal is recoverable. Moader hides the entry and publishes the changed routing while preserving payload paths for older open filesystem objects. It is **not a physical disk purge**. Do not manually rename/delete retained payloads during the active boot to try to force removal; reopen content or relaunch the game as its apply state requires.

## Native debug lines are missing

Use matching components from the current release. The product reads closed native report/event snapshots; an open writer log is not the file you should try to unlock manually. An app-only update cannot add this feature to an older running FS runtime.

Check whether the runtime is connected, whether backend setup completed, and whether native rows update after a management request. Preserve the displayed error if snapshots are unavailable rather than changing the live writer file or replaying setup.

## A mod caused a software error

For a **game-only** error, the separate resident service/FS runtime is expected to remain available. If the browser still connects, disable/remove the offending mod and relaunch the game. A normal game-only error should not itself require reacquiring FS.

Recovery after a forced game error was not tested. FS, HEN or system failures—and a console reboot—can invalidate the backend context and require fresh qualified setup. If the service is unavailable, preserve the error/report and establish the current console state before choosing recovery.

## Direct ns2jb never reaches the held FS-ready state

Check the [pinned upstream setup](https://gitlab.com/kurumuz/ns2jb/-/blob/28f03b5895e5569d8fbc35cbed84906b8f0a65b2/README.md) and [Route A](INSTALLATION.md#route-a-linuxwsl-or-macos-with-ns2jb). Use the fresh printed SmileBASIC `V=START(...)` and run folder, not an earlier run. The expected bootstrap **Out of memory** step is described upstream. Moader requires **4/4 verified** plus the held RPC/FS-ready state; an earlier progress message is insufficient.

Acquisition must use `node run.cjs --rpc YOUR_PC_LAN_IPV4`. A normal run without `--rpc` closes its endpoint after checks. Keep that terminal, SmileBASIC and the host awake/open through installation. Do not run `hen.cjs` first: Moader's combined installer owns HEN bootstrap. Preserve an unexpected failure rather than scripting repeated acquisition attempts.

## WSL cannot bind or the Switch cannot reach the host

Complete [WSL2 networking preparation](INSTALLATION.md#wsl2-networking-before-acquisition) before acquiring a held session. The pinned runner uses the same IPv4 for its local bind and the address printed for SmileBASIC. An `EADDRNOTAVAIL` error can mean the selected Windows LAN IPv4 is absent inside the Linux distro. A NAT port proxy alone does not fix that bind; use a confirmed bindable/reachable mirrored LAN path or native Linux.

Check initial inbound TCP **39380**, host/Linux firewall rules, VPN and LAN isolation. The separate generated kurumuz RPC bridge is loopback-only and unauthenticated: **never forward it**. Both acquisition and Moader preparation/install must stay inside the same WSL distro.

## The helper rejects Node or the run folder

For direct ns2jb, choose native Linux/macOS **Node 24**, using `--node "$(command -v node)"` or its explicit executable path. In WSL, `node -p 'process.platform'` must print `linux`; Windows `node.exe` is unsupported. For mac2jb, paste the full newly generated four-token status command so the helper retains its exact bundled Node. Do not combine the two helper routes.

Use the actual existing run folder printed by the held runner. The helper accepts relative or absolute POSIX paths, rejects symlinked run paths and expects the upstream `switch2-…` folder name. Use the real nonsymlink path, keep spaces quoted, and avoid Windows drive/UNC paths. A local preparation error does not justify changing native proof flags.

## CivSwitch shows Awaiting payload, but Moader is not ready

**Awaiting payload** means the JavaScript loader is listening on the console's TCP9026. Moader's experimental `civ7-s8n` profile requires a verified, current held **FS-only** run from its exact admitted s8n revision. App-only, completed/released, uncertain or already-HEN/RO-frontend runs are different states. HerXayah and mac2jb CivSwitch runs are not admitted by that source pin. See the [CivSwitch guide](CIV7.md).

If the correct FS-only session is already held, preserve it and its original host for qualification; do not reacquire merely because the entrypoint is Civ7. Use `--host-profile civ7-s8n` for both local setup and `install-from-rpc.py --prepare`, with the real run directory and native Node24. A legacy/SmileBASIC prepared snapshot cannot substitute for this profile. Never edit proof or source hashes to override rejection.

The s8n runner takes the **console** IP and sends the bridge to TCP9026; the console then connects back to the computer on TCP39380 by default. Verify both directions. Successful outgoing loader access alone does not establish WSL return-path reachability. Its separate localhost RPC bridge requires a per-run bearer token: keep it local and out of shared logs. Do not run upstream `hen.cjs` first; Moader owns its combined HEN handoff. No Moader Civ7 hardware test is claimed by these troubleshooting steps.

## Backend setup failed or timed out

Stop and keep the helper's report. A timed-out native operation may already have completed; rerunning the old command can be unsafe. Do not edit proof flags, erase journals, replay old handles or reuse an earlier boot's session to bypass the checks.

Follow the release's supported recovery instructions. Backend installation needs a **verified current-boot held FS session from the selected admitted profile**. The normal routes use SmileBASIC through direct ns2jb or mac2jb; experimental Civ7 uses only the pinned s8n FS-only route. If the correct session is still held, preserve it for qualification rather than acquiring again merely to change host setup. A rejected, completed or uncertain session must not be relabeled or replayed. Routine mod upload/enable/disable/removal uses the running backend and does not require repeated FS acquisition. See [Installation](INSTALLATION.md).

## Reporting a problem

[Open an issue](https://github.com/H644b/Moader/issues) with:

- Moader release and whether the matched backend was set up this boot.
- What you did, expected result and actual result.
- The exact on-screen error/apply state and whether the browser still connects.
- Whether a scene reload or game relaunch changes the result.
- A small, sanitized diagnostic excerpt or screenshot when useful.

Remove pairing codes, session commands, network addresses, personal paths and credentials before posting. Do not attach game files, proprietary textures or saves. Describe the mod type and relevant behavior instead.

## Reload progress or a lost connection during reload

The protocol-7 patch adds **Settings → Advanced → Reload Moader service**. The listener closes while its process restarts. Wait for the app to reconnect, then reconnect your browser. A timeout is an unknown outcome, not proof of cancellation. Use the displayed status and save diagnostics; do not start a second writer or repeatedly submit a command after a quarantined result. [Reload guide](PLUGIN_RELOAD.md).

## Installation stops during Loader restoration

Keep the terminal's saved installation journal. A memory timeout does not prove the native request stopped. Do not repeat the installation, run cleanup commands, or reuse that RPC session. If the console is frozen, hold POWER for 12 seconds to turn it off, then power on normally. A new installation requires a newly acquired FS session after recovery. See [Validation](VALIDATION.md) for the Loader-restoration evidence and its limits.
