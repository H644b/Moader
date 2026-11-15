# Install Moader

**Unreleased candidate:** v0.1.0-dev.5 has not been published. Its first plugin-reload attempt failed on the console; the offline repair still needs device testing. Do not install this branch or run its device commands. The instructions below describe the intended matched kit. For the published v0.1.0-dev.4 release, use [that tag's installation guide](https://github.com/H644b/Moader/blob/v0.1.0-dev.4/docs/INSTALLATION.md). Read the current [validation scope](VALIDATION.md).

| Situation | What to do |
| --- | --- |
| Matching Moader resident backend is already running this boot | Copy/update the console app |
| You need the resident service and game-file routing | Choose either backend route below |
| The console rebooted | Obtain a new verified held FS session and set up this boot's backend |

The precompiled host kit works through Python/Node; end users do not need an SDK or to compile HEN. Source SDK rebuilds are a separate [Apple Silicon macOS workflow](BUILDING.md#build-from-a-clean-source-checkout). See the [short glossary](../README.md#supported-setup) for kaiHEN, hbmenu and FS.

## Copy or update the console app

1. Once the candidate is accepted and published, download its matching `Moader-v0.1.0-dev.5.zip`.
2. Close the Moader app before replacing it.
3. Copy `switch/Moader.nro` to the SD card's `switch/` folder by your usual SD/FTP method.
4. Open **Moader** from hbmenu.

If the matching resident service is running, the app connects to it. Otherwise it can host a foreground library/web interface, which stops when the app closes. An NRO copy does not install FS hooks or start a resident process. Copying the included `kaihen/plugins/moader.nso` only stages a plugin; it does not hot-reload a running service.

## Start the backend for this boot

Once accepted and published, extract the matching `Moader-backend-v0.1.0-dev.5.zip`. Use the outer folder containing `tools/` and `START_HERE.txt`. The kit includes the matched precompiled patched HEN, Moader binaries and source. First-use preparation downloads the pinned official kaiHEN archive and ns2jb source; internet access is needed then.

Choose **one** route. Both end by printing Moader's privileged installation command. Preparation never acquires FS or contacts the Switch. The final installer qualifies the actual held run and FS code/ABI before native work; a failure is not permission to bypass its guards.

| Route | Host requirements | Session source |
| --- | --- | --- |
| A: direct ns2jb | Native Linux/macOS Node **24**, Python **3.10+**; Git to clone/checkout upstream | kurumuz ns2jb `run.cjs --rpc` |
| B: mac2jb | macOS, Python **3.10+**, exact mac2jb-bundled Node **24** | Fresh mac2jb verified FS session |

WSL2 users must complete [networking preparation](#wsl2-networking-before-acquisition) **before** Route A. Use Linux Node and two terminals in the **same WSL distro**; do not use `node.exe`, Windows run paths or a Windows terminal outside that distro for preparation/install.

The direct route is documented from pinned source/helper contracts. No new Linux/WSL device verification is claimed here; the published real-console validation used macOS/mac2jb. Use upstream requirements for console/application compatibility; a stock-console installer is not included. CivSwitch uses the separate experimental profile below.

## Experimental CivSwitch route

Civilization VII FS access exists upstream. Moader's initial experimental profile is **`civ7-s8n`**, restricted to `s8n/civ7-fs-kaihen` commit `51a804cbce09216e1a42e65929644fd436b4c8bf`. Host tests pass; **no Moader Civ7 console installation is claimed**. This is separate from the two SmileBASIC routes below and from modding Civilization VII itself.

Follow the [CivSwitch guide](CIV7.md#experimental-setup) for prerequisites, source fetch, FS-only acquisition and profile-aware local preparation. If you already hold a current admitted s8n FS-only session, keep it open for qualification rather than acquiring again. HerXayah and mac2jb CivSwitch sessions require their own admitted profile; do not pass them under the s8n pin. The ordinary TCP9026 loader state or already-running upstream HEN is not the required FS-only starting state.

## Route A: Linux/WSL or macOS with ns2jb

### Terminal 1: obtain a held, verified FS session

Install native Node **24**, Python **3.10+**, and Git using your host's normal package tools. Check `node --version` and `python3 --version`; Node must report v24. Git is needed only to obtain this pinned upstream checkout:

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

Follow upstream's printed SmileBASIC steps and the fresh `V=START(...)` line. The bootstrap's expected **Out of memory** message is described upstream. Wait for **4/4 verified**, the held RPC/FS-ready message and the newly printed run folder. **Keep Terminal 1, SmileBASIC and the computer awake/open** through Moader installation.

Use `run.cjs --rpc`, **not `hen.cjs` first**: Moader's combined installer performs the HEN bootstrap later. A normal run without `--rpc` closes its endpoint after checks. Do not automate acquisition retries or repeat uncertain native operations.

### Terminal 2: prepare Moader locally

Open another terminal in the extracted **Moader host-kit folder**, not the ns2jb checkout. On WSL both terminals must be inside the same distro. Replace the run-folder placeholder with Terminal 1's actual newly printed folder; the example is not a reusable session:

```sh
cd "YOUR_EXTRACTED_KIT_FOLDER"
python3 tools/install-from-rpc.py --prepare \
  --run-dir "/path/to/ns2jb/runs/<actual-run>" \
  --node "$(command -v node)"
```

The selected Node must be the same native Node 24 used for Terminal 1. This route does **not** require mac2jb. Continue with [review and install once](#review-and-install-once).

## Route B: macOS with mac2jb

Use the [official mac2jb requirements and entry instructions](https://github.com/cpypwn/mac2jb#readme) to obtain a **fresh verified current-boot FS session**. Close foreground Moader first. Keep **SmileBASIC and the mac2jb host open** through installation. Python 3.10+ is required; the helper preserves the exact bundled Node 24 from the status command.

Open Terminal in the extracted host-kit folder: type `cd `, drag that folder into Terminal and press Return. Alternatively use `cd "YOUR_EXTRACTED_KIT_FOLDER"`, replacing the placeholder.

Copy the **entire newly generated four-argument mac2jb status command**, preserving quoted paths. In macOS's default zsh, run:

```sh
read -r "MOADER_STATUS_COMMAND?Paste the full mac2jb status command: "
python3 tools/install-from-rpc.py --prepare --status-command "$MOADER_STATUS_COMMAND"
```

Paste the full status command at the prompt. The pasted script is not executed and no shell `eval` is used. If using bash, replace the first line with `read -r -p 'Paste the full mac2jb status command: ' MOADER_STATUS_COMMAND`.

## Review and install once

For **either** route, `--prepare` downloads missing pinned inputs, validates the kit and Node, creates a unique local installer snapshot and prints one exact installation command. **It does not contact the Switch or install anything on it.**

Inspect the printed command: it must use the intended Node 24, kit, newly prepared snapshot and the fresh held session. Then **copy and run that exact printed command once**. This is the **privileged installation** step. Keep Terminal 1/mac2jb and SmileBASIC open; do not run a second acquisition, bootstrap HEN separately, substitute a different snapshot or edit proof flags.

If installation fails, times out or reports uncertainty, stop and preserve its report. A native operation may already have completed; do not retry the same command. Follow [setup recovery](TROUBLESHOOTING.md#backend-setup-failed-or-timed-out). A completed bootstrap retires the old diagnostic installation context; its command/handles are not reusable.

After success, use the supported kaiHEN hbmenu entry procedure and open **Moader**. Check the connected runtime and native startup/routing lines in Advanced/Debug. If the web listener is off press **Y**; **ZR** gives its URL/code. Pair the browser, then **+** can close the app while resident web stays available. [Full usage](USAGE.md).

Backend setup is **once per boot**, not part of everyday upload/enable/disable/removal. Saved preferences/library persist on SD; running backend state does not survive a console reboot. NRO/NSO copies alone do not replace running FS hooks or a resident service.

## WSL2 networking before acquisition

Microsoft documents mirrored networking on **Windows 11 22H2+**; it enables LAN access to WSL. Before any held run, configure Windows `%UserProfile%\.wslconfig`:

```ini
[wsl2]
networkingMode=mirrored
```

Apply it with `wsl --shutdown` in Windows, then reopen the distro. This stops WSL processes, so do it **before acquisition**. Inside Linux, use `ip -4 addr show` to check for the chosen LAN IPv4; it must be present/bindable there. See [Microsoft WSL networking](https://learn.microsoft.com/en-us/windows/wsl/networking#mirrored-mode-networking).

In elevated Windows PowerShell, add a port-specific Hyper-V rule on your trusted LAN:

```powershell
New-NetFirewallHyperVRule -Name "MoaderNs2jb" -DisplayName "Moader ns2jb" -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol TCP -LocalPorts 39380
```

Allow TCP **39380** through any other applicable host/Linux firewall; do not disable the firewall or set blanket inbound allow. Follow [Microsoft's Hyper-V firewall guidance](https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/hyper-v-firewall) for local policy and rule cleanup.

The [pinned runner](https://gitlab.com/kurumuz/ns2jb/-/blob/28f03b5895e5569d8fbc35cbed84906b8f0a65b2/run.cjs) binds its advertised IPv4 directly. For this runner, Microsoft's [NAT portproxy reference](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/netsh-interface) is **not sufficient by itself**: forwarding a Windows LAN address does not make that address bindable inside NAT-mode Linux. Establish a correct bindable LAN path first or use native Linux. No NAT-only recipe or WSL/device test is claimed here.

In WSL, `node -p 'process.platform'` must print `linux`. Keep acquisition and installation in the same distro using Linux paths/Node. **Never forward the separate loopback RPC bridge**; only the initial Switch-to-host listener uses TCP **39380**.

## Updating

Keep the app, resident and patched HEN/bootstrap from **v0.1.0-dev.5** matched. Install the updated backend kit through a fresh current-boot FS session once to add protocol-7 reload support. An older running HEN cannot gain the command by copying files. After that, finish imports, copy a compatible replacement `kaihen/plugins/moader.nso` and use **Settings → Advanced → Reload Moader service**. The command checks and restarts that resident; copying an NSO alone still does not reload it. A foreground app uses its own implementation and cannot restart a resident. [Reload guide](PLUGIN_RELOAD.md).

Select each game, then open **Settings → Game copy**. Choose Cartridge/game-key for a physical copy or My digital copy for a copy that runs without the card. Unspecified defaults to checking the inserted card. Existing enable requests are preserved; a missing card or verification failure withholds that game's replacements from newly published routing. Insert the card or correct the copy setting, then inspect the apply details. [Full behavior and limits](CARTRIDGES.md).

HEN/native FS runtime inputs are unchanged. This update does not establish the cause or fix of a fatal game/system restart. The new app/service build has host validation only. The displayed version remains `0.1.0-dev`; identify the update by its release archive or recorded hashes.

Preserve user mods and preferences. Recoverable removal retains payloads for older open game objects; do not manually purge them during the boot. See [Troubleshooting](TROUBLESHOOTING.md), [Building](BUILDING.md) and [Maintaining](MAINTAINING.md).
