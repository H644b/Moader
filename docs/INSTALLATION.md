# Install Moader

## Copy or update the console app

1. Download and extract [Moader-v0.2.0.zip](https://github.com/H644b/Moader/releases/download/v0.2.0/Moader-v0.2.0.zip).
2. Close Moader if it's open.
3. Copy `switch/Moader.nro` to the SD card's `switch/` folder.
4. Open **Moader** from hbmenu.

If the matching resident service is already running, the app connects to it. Otherwise you can manage the library and use the browser while the app stays open. [Use Moader](USAGE.md)

The ZIP also contains `kaihen/plugins/moader.nso`. Copying it puts the plugin on SD. To replace a running plugin, follow [Updating](#updating).

## Start the backend for this boot

The backend has two parts. The resident service keeps managing the library after you close the app. The filesystem runtime (FS) applies mod files in games. Setup starts both for the current boot. Your mod files and preferences stay on SD after a reboot.

| Method | Computer | Download |
| --- | --- | --- |
| [SmileBASIC with mac2jb](#route-b-macos-with-mac2jb) | macOS | [v0.2.0 backend ZIP](https://github.com/H644b/Moader/releases/download/v0.2.0/Moader-backend-v0.2.0.zip) |
| [SmileBASIC with direct ns2jb](#route-a-linuxwsl-or-macos-with-ns2jb) | macOS, Linux, WSL2 | [v0.2.0 backend ZIP](https://github.com/H644b/Moader/releases/download/v0.2.0/Moader-backend-v0.2.0.zip) |
| [Seedsow Lullaby Demo or full game](SEEDSOW.md) | Apple Silicon macOS, Linux x86_64, WSL2 x86_64 | [v0.2.0 backend ZIP](https://github.com/H644b/Moader/releases/download/v0.2.0/Moader-backend-v0.2.0.zip) |
| [CivSwitch, experimental](CIV7.md#experimental-setup) | macOS or Linux host path | [v0.2.0 backend ZIP](https://github.com/H644b/Moader/releases/download/v0.2.0/Moader-backend-v0.2.0.zip) |

The Seedsow method uses the included matched RO/HEN/bootstrap set. Its guide covers additional SDK and GUI preparation. The updated v0.2.0 app/resident have host validation; the earlier Seedsow **Demo** device installation used the exact same RO/HEN/bootstrap bytes with the previous app/resident build. The full-game process identity has host qualification only. [Validation scope](VALIDATION.md#seedsow-lullaby-demo-source-workflow)

### Before either SmileBASIC method

Download and extract [Moader-backend-v0.2.0.zip](https://github.com/H644b/Moader/releases/download/v0.2.0/Moader-backend-v0.2.0.zip). Open the outer folder with `START_HERE.txt` and `tools/`. Install **Python 3.10+**. First-time preparation needs internet access and uses the prebuilt files in the kit, so you can skip installing an SDK. Close the Moader app if it's open.

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
3. Open **Moader**. In **Settings → Advanced**, check that the filesystem runtime is connected. Then [connect your browser](USAGE.md#connect-a-browser) or start using the app.

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

## Updating

Close Moader before replacing the app file. Keep the app and resident from the same release.

- **App only.** Copy the new NRO to SD and reopen the app.
- **Compatible resident service.** Copy the replacement NSO to `kaihen/plugins/moader.nso`. Use [Reload Moader service](PLUGIN_RELOAD.md) with the matching protocol-7 HEN and bootstrap already running.
- **New HEN or bootstrap, or a full reboot.** Use the matching backend kit and follow the [setup method for this boot](#start-the-backend-for-this-boot).
