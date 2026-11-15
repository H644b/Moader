# Install Moader

Download the matched **[v0.1.0-dev prerelease](https://github.com/H644b/Moader/releases/tag/v0.1.0-dev)**. Choose the app ZIP for an ordinary app copy, or the backend kit when you need to start Moader's resident service and filesystem runtime for this boot.

The guided/verified backend path for this release is **macOS/mac2jb with a SmileBASIC/ns2jb verified FS session**. Start with the [official mac2jb requirements and entry instructions](https://github.com/cpypwn/mac2jb#readme); upstream maintains the changing console/application/firmware requirements. Moader supplies no stock-console entry installer, and alternate entrypoints such as CivSwitch are not qualified here. [Short glossary: kaiHEN, hbmenu and FS](../README.md#supported-setup).

| Situation | What to do |
| --- | --- |
| Compatible Moader backend is already running | Copy/update the console app below |
| kaiHEN is available, but Moader's backend is absent | The app can run in foreground mode; use backend setup for resident gameplay management |
| Console has rebooted and you want resident/game mod support | Start the backend with a fresh verified FS session |

The backend kit contains Moader and the matched precompiled patched HEN; its setup helper fetches the pinned official kaiHEN archive and ns2jb sources at first use. Internet access is required for that initial dependency setup. **An SDK and HEN compilation are not required for end-user setup.** See [Building](BUILDING.md) only if you want to build the project yourself.

## Copy or update the console app

1. Download [Moader-v0.1.0-dev.zip](https://github.com/H644b/Moader/releases/download/v0.1.0-dev/Moader-v0.1.0-dev.zip).
2. Close the Moader app before replacing it.
3. Copy its `switch/Moader.nro` to the SD card's `switch/` folder, using your usual SD-card or FTP transfer method.
4. Open **Moader** from hbmenu.

```text
SD card/
└── switch/
    └── Moader.nro
```

If the matching resident backend is running, the app connects to it. Otherwise, Moader can manage its library and host the web interface in **foreground mode**; that listener stops when the app closes.

An app copy does **not** install filesystem hooks or start a resident process. The archive also contains `kaihen/plugins/moader.nso`; copying it only stages that component for a later compatible bootstrap. It does not hot-reload the running service.

## Start the backend for this boot

### 1. Unpack the host kit

Download and unzip [Moader-backend-v0.1.0-dev.zip](https://github.com/H644b/Moader/releases/download/v0.1.0-dev/Moader-backend-v0.1.0-dev.zip). Use the extracted **Moader-backend-v0.1.0-dev** folder containing `tools/` and `START_HERE.txt`.

You need:

- macOS Terminal with its default **zsh** shell.
- **Python 3.10+** installed. Check with `python3 --version`.
- The exact **Node 24** executable supplied by mac2jb. The helper takes it from your status command; it does not substitute another installed Node.
- Internet access for the first-use pinned dependency downloads, plus a fresh verified mac2jb FS session for this boot.

The kit includes Moader's app/plugin and matched precompiled patched HEN. Preparation downloads the pinned official kaiHEN archive and ns2jb sources; you do not need an SDK or HEN compilation.

In Terminal, type `cd ` with a space, drag the extracted kit folder into the window, then press Return. Alternatively, replace the placeholder below with that folder's path:

```sh
cd "YOUR_EXTRACTED_KIT_FOLDER"
```

### 2. Obtain the fresh session

Follow the [official mac2jb entry instructions](https://github.com/cpypwn/mac2jb#readme) to obtain a **new verified FS session for the current boot** through the supported SmileBASIC/ns2jb route. Close any foreground Moader instance. **Keep SmileBASIC and the mac2jb host open until installation reports completion.** Moader's preparation helper does not acquire FS for you.

Copy the complete newly generated mac2jb **four-argument status command**, including its quoted paths and final `status` argument. Use the current verified session, never an old boot's command or a completed installation context.

### 3. Prepare locally

From the extracted kit folder, run these two lines. Paste the complete status command at the prompt and press Return:

```sh
read -r "MOADER_STATUS_COMMAND?Paste the full mac2jb status command: "
python3 tools/install-from-rpc.py --prepare --status-command "$MOADER_STATUS_COMMAND"
```

The prompt syntax above is for zsh. [Building](BUILDING.md#prepare-the-host-kit) also documents bash syntax.

**This step prepares files on your computer; it does not contact the Switch or install anything on it.** It validates the selected Node 24/path format, downloads missing pinned dependencies, checks the matched kit, creates a unique local installer snapshot, then prints the exact installation command. The pasted status script is not executed and no shell `eval` is used.

### 4. Review and install once

Inspect the printed command. It must use the Node executable and fresh session you supplied, this kit's installer, and its newly prepared snapshot. Keep SmileBASIC open.

**Copy and run that exact printed installation command once. This is the privileged Switch installation step.** It verifies the current session, installs the matching filesystem runtime and starts the resident service through the guarded bootstrap. Do not replace its prepared snapshot with another file, edit proof flags or reuse a previous command.

If preparation or installation stops, read the error and preserve its report. If an installation fails, times out or reports an uncertain result, **stop without retrying**: the native operation may already have completed. Follow [setup recovery](TROUBLESHOOTING.md#backend-setup-failed-or-timed-out). A successful setup retires its installation context; the old command is not reusable.

### 5. Verify in Moader

After setup reports success, use your compatible kaiHEN environment's normal **hbmenu** entry procedure, then open **Moader**. In Advanced/Debug, check that the runtime is connected and native startup/routing lines are visible.

If the web listener is off, press **Y**. Press **ZR**, open the displayed URL on your other device and enter the six-digit code. With the resident backend active, **+** returns to hbmenu while the browser stays available. [Full browser instructions](USAGE.md#connect-a-browser).

Backend setup is **once per boot**, not part of ordinary mod upload, enable, disable or removal. Keep the app, plugin and filesystem runtime matched; copying an NRO or NSO alone does not update the running backend.

## After setup

If the web listener is off, press **Y** to start it; **ZR** shows its address/code. The default port is `7712`, but the displayed address is authoritative if you changed it.

In resident mode, you can close the app with **+**, play, and keep managing mods from the browser. The app's **ZL** view and browser debug panel show native diagnostics. A connected runtime means it can acknowledge routing; check each mod's apply requirements before expecting an in-game change.

Preferences and the mod library persist on SD. The running backend lasts **until reboot**. A console reboot requires the appropriate fresh backend setup again; it does not erase the saved library.

## Updating

Keep the app, resident plugin, filesystem runtime and setup components from the same supported release.

An ordinary NRO update does not update the already-running plugin or FS hooks. A backend update uses its own qualified setup/bootstrap; dropping a new NSO onto SD does not replace the live process. Follow the new release's update instructions and use a fresh verified session when backend installation is required.

Preserve your library and preferences. Removed-mod payloads may still be needed by older open game objects; Moader's recoverable removal is not an instruction to delete those files manually during the boot.

For contributor build details or release maintenance, see [Building](BUILDING.md) and [Maintaining](MAINTAINING.md).
