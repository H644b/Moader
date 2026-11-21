# Install Moader

Choose a download based on what is already running on your Switch. Use the app and resident plugin from the same release.

| What you need | Download |
| --- | --- |
| Install or update Moader on an existing compatible backend | [Moader-v3.0.0.zip](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-v3.0.0.zip) |
| Set up the backend for this boot | [Moader-backend-v3.0.0.zip](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip) |

## Copy or update the console app

Close Moader if it is open. Extract the app ZIP on your computer and copy both files to the SD card. The archive already uses these paths:

| File | Location on the SD card |
| --- | --- |
| Console app | `/switch/Moader.nro` |
| Resident plugin | `/kaihen/plugins/moader.nso` |

Open **Moader** from **hbmenu**, the homebrew launcher. Moader connects to a matching resident service when one is running. You can also manage your library with the app open in foreground mode. The browser stays available after you close the app when the resident service is running.

Copying the plugin puts the new file on SD. If Moader is already running in the background, [reload the service](PLUGIN_RELOAD.md#use-it) to start the replacement.

## Start the backend for this boot

Choose the method you use to start homebrew. Use the v3.0.0 backend ZIP from the download table.

| Method | Computer | Guide |
| --- | --- | --- |
| SmileBASIC with mac2jb | macOS | [mac2jb setup](#route-b-macos-with-mac2jb) |
| SmileBASIC with direct ns2jb | macOS, Linux, or Windows with WSL2 | [Direct ns2jb setup](#route-a-linuxwsl-or-macos-with-ns2jb) |
| Seedsow Lullaby, PC-driven | Apple Silicon macOS, Linux x86_64, or WSL2 x86_64 | [PC-driven Seedsow](SEEDSOW.md) |
| Seedsow Lullaby, on-console (experimental) | Computer for initial setup | [On-console Seedsow requirements](SEEDSOW_STANDALONE.md#requirements-and-boundaries) |
| CivSwitch (experimental) | macOS or Linux | [CivSwitch setup](CIV7.md#experimental-setup) |

The on-console Seedsow kit includes a standalone bootstrap. The Moader route has a separate requirement for a qualified local filesystem capture. See the [v3.0.0 release notes](https://github.com/H644b/Moader/releases/tag/v3.0.0) and [validation results](VALIDATION.md#v300-standalone-release-scope).

After a full reboot, the files and settings stay on SD. Start your backend again for the new boot.

### Before either SmileBASIC method

Download and extract [Moader-backend-v3.0.0.zip](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip). Open the outer folder with `START_HERE.txt` and `tools/`. Install **Python 3.10+**. First-time preparation needs internet access and uses the prebuilt files in the kit, so you can skip installing an SDK. Close the Moader app if it's open.

Use the requirements in the upstream guide for your chosen method. Moader needs a fresh, verified filesystem session from the current boot. Keep the session's host and SmileBASIC open until the Moader installer finishes.

<a name="route-b-macos-with-mac2jb"></a>

## SmileBASIC with mac2jb

### macOS

Use the [official mac2jb requirements and entry instructions](https://github.com/cpypwn/mac2jb#readme) to obtain a **fresh verified current-boot FS session**. Keep **SmileBASIC and mac2jb open**. The helper uses the Node 24 executable from the status command.

Open Terminal in the extracted host-kit folder: type `cd `, drag that folder into Terminal and press Return. Alternatively use `cd "YOUR_EXTRACTED_KIT_FOLDER"`, replacing the placeholder.

Copy the **entire newly generated four-argument mac2jb status command**, preserving quoted paths. In macOS's default zsh, run:

```sh
read -r "MOADER_STATUS_COMMAND?Paste the full mac2jb status command: "
python3 tools/install-from-rpc.py --prepare --status-command "$MOADER_STATUS_COMMAND"
```

Paste at the prompt and press Return. The helper reads the command as text. If using bash, replace the first line with `read -r -p 'Paste the full mac2jb status command: ' MOADER_STATUS_COMMAND`.

Continue to [Finish SmileBASIC setup](#review-and-install-once).

<a name="route-a-linuxwsl-or-macos-with-ns2jb"></a>

## SmileBASIC with direct ns2jb

### macOS and Linux

Install Git and native **Node 24** on the computer. Then follow [the two-terminal steps](#get-the-held-session-in-terminal-1).

### Windows with WSL2

Install Git, Python and Linux **Node 24** inside your WSL distro. Run `node -p 'process.platform'` there. It must print `linux`. Use Linux paths, and open both terminals in the same distro. Set up [WSL2 networking](#wsl2-networking-before-acquisition) before starting [the two-terminal steps](#get-the-held-session-in-terminal-1).

### Get the held session in Terminal 1

In Terminal 1, check `node --version` and `python3 --version`. Node must report v24. Use Git to get the required ns2jb revision:

```sh
git clone https://gitlab.com/kurumuz/ns2jb ns2jb
cd ns2jb
git checkout --detach 28f03b5895e5569d8fbc35cbed84906b8f0a65b2
```

Read that checkout's [pinned README and one-time SmileBASIC setup](https://gitlab.com/kurumuz/ns2jb/-/blob/28f03b5895e5569d8fbc35cbed84906b8f0a65b2/README.md#one-time-setup). Follow its current console/application prerequisites; Moader does not replace the upstream entry setup.

Replace `YOUR_PC_LAN_IPV4` with the computer IPv4 that the Switch can reach **and the host can bind**, then run:

```sh
node run.cjs --rpc YOUR_PC_LAN_IPV4
```

Follow the host tool’s printed steps. Wait for **4/4 verified**, the held RPC/FS-ready message and the new run folder. Keep Terminal 1, SmileBASIC and the computer awake through installation.

Leave HEN startup to Moader’s combined installer. If the run fails, keep the error and follow [setup recovery](TROUBLESHOOTING.md#backend-setup-failed-or-timed-out).

### Prepare Moader in Terminal 2

Open Terminal 2 in the extracted Moader kit. Replace both placeholders with the kit path and the current run folder printed in Terminal 1:

```sh
cd "YOUR_EXTRACTED_KIT_FOLDER"
python3 tools/install-from-rpc.py --prepare \
  --run-dir "/path/to/ns2jb/runs/<actual-run>" \
  --node "$(command -v node)"
```

Use the same Node 24 in both terminals. Continue to [Finish SmileBASIC setup](#review-and-install-once).

<a name="review-and-install-once"></a>

## Finish SmileBASIC setup

1. Wait for preparation to print a command. Preparation checks the kit locally and may download dependencies. The Switch installation starts when you run the printed command.
2. Review the Node, snapshot and run-folder paths. Run that exact command **once**. Keep SmileBASIC and its session host open until the installer returns to the terminal prompt.
3. Look for the final **Moader setup complete** line. If the installer prints it and exits successfully, the startup check has passed. You can close SmileBASIC. There is no later terminal check to wait for.

If the command fails, times out or reports an uncertain result, keep the report and follow [setup recovery](TROUBLESHOOTING.md#backend-setup-failed-or-timed-out). Leave the old command and session alone.

### Open Moader on the Switch

1. Press **HOME**, highlight SmileBASIC and press **X** to close it. Keep the Switch on.
2. Hold **R** while starting a native Switch 2 game. Let it load, close it, then hold **R** while starting it again to enter hbmenu.
3. Open **Moader** and [check the runtime connection](#check-your-setup).

<a name="wsl2-networking-before-acquisition"></a>

## WSL2 networking for direct ns2jb

Microsoft documents mirrored networking on **Windows 11 22H2+**; it enables LAN access to WSL. Before any held run, configure Windows `%UserProfile%\.wslconfig`:

```ini
[wsl2]
networkingMode=mirrored
```

Run `wsl --shutdown` in Windows and reopen your distro. Do this before starting the session because it stops WSL processes. Inside Linux, use `ip -4 addr show` to check for the chosen LAN IPv4; it must be present/bindable there. See [Microsoft WSL networking](https://learn.microsoft.com/en-us/windows/wsl/networking#mirrored-mode-networking).

In elevated Windows PowerShell, add a port-specific Hyper-V rule on your trusted LAN:

```powershell
New-NetFirewallHyperVRule -Name "MoaderNs2jb" -DisplayName "Moader ns2jb" -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol TCP -LocalPorts 39380
```

Allow TCP **39380** through any other applicable host/Linux firewall; do not disable the firewall or set blanket inbound allow. Follow [Microsoft's Hyper-V firewall guidance](https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/hyper-v-firewall) for local policy and rule cleanup.

The [pinned runner](https://gitlab.com/kurumuz/ns2jb/-/blob/28f03b5895e5569d8fbc35cbed84906b8f0a65b2/run.cjs) binds its advertised IPv4 directly. For this runner, Microsoft's [NAT portproxy reference](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/netsh-interface) is **not sufficient by itself**: forwarding a Windows LAN address does not make that address bindable inside NAT-mode Linux. Establish a correct bindable LAN path first or use native Linux. No NAT-only recipe or WSL/device test is claimed here.

**Never forward the separate loopback RPC bridge.**

## Check your setup

Open **Moader**. The **Game library** can be empty until you add a mod. For in-game mods, press the left stick and open **Settings → Advanced → Runtime details**. Look for **Connected** on the runtime connection row. Then [pair a browser and add a mod](USAGE.md#connect-a-browser).

If you can open the app but its runtime is disconnected, check whether setup completed for this boot. [Troubleshoot the connection](TROUBLESHOOTING.md#browser-or-app-connection)

## Updating

Finish any uploads and close Moader. Copy the new app and resident plugin together. Leave the `moader/` folder on SD because it holds your mods and preferences.

- **Compatible resident already running.** Use [Reload Moader service](PLUGIN_RELOAD.md#use-it) to activate the new plugin.
- **After a full reboot or a HEN/bootstrap update.** Follow [the setup method for this boot](#start-the-backend-for-this-boot) with the matching backend kit.
