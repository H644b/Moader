# Install Moader through Seedsow Lullaby

Download the [v0.2.5 backend ZIP](https://github.com/H644b/Moader/releases/download/v0.2.5/Moader-backend-v0.2.5.zip) for Seedsow Lullaby Demo or the full game. If you were given a `v0.2.6-rc` hold-R candidate, use that extracted kit throughout this guide. The rc.1 candidate passed one existing-policy console check but an affected tester still reported no hbmenu. The rc.2 first-launch fallback passed a controlled replacement-card Demo check, including normal first game launch, second-launch R entry and a saved game policy; that tester has not retested it. It is not yet a verified general fix. For a controlled rc.2 check, follow [its first-launch steps](releases/v0.2.6-rc.2.md) after installation. The kit contains the prebuilt Moader app, resident, the pinned official kaiHEN base, and `bundles/kaiHEN-Moader-20261114c.zip`: the complete regular release with only the matched RO/HEN/bootstrap replaced and Moader added. This includes the game loader, hbmenu, OunceShell and licenses. Its helper provisions Moader's patched [Seedsow FS GUI](https://github.com/Malev0/seedsow-fs-kaihen) from the exact upstream revision. The upstream Seedsow GUI and source are fetched during preparation.

The full game's program ID is admitted by the same current-process qualification that accepts the Demo, while unrelated program IDs still stop. The code profile is checked anew on each boot. The **Demo** route was verified from macOS with the full bundle, successful FS retirement and two-launch hold-R entry. The v0.2.5 resident was then verified through a cooperative reload and a Minecraft world run on a Demo-installed boot. A fresh complete v0.2.5 kit installation and an installation from the **full game** have not yet been observed. Linux x86_64 passed host build and transport checks. WSL2 follows the Linux build path. [Seedsow test results](VALIDATION.md#v025-resident-bsd-session-budget)

## Before you begin

- A Switch 2 with Seedsow Lullaby Demo or the full game and a working launcher save for **that edition**. The full game's save format differs from the Demo's; do not restore a Demo launcher save over a full-game save. Follow the [upstream Seedsow setup](https://github.com/Malev0/seedsow-fs-kaihen#setup-once) if you do not already have that entrypoint. This guide does not change or replace your save.
- An Apple Silicon Mac or Linux x86_64 host, including WSL2, able to reach the Switch on the local network. The game's HTTP launcher must be reachable from that host after you press **Y**.
- Python 3.10 or newer, native POSIX Node.js **24**, Git, AArch64 capable `clang`, and room for the local SDK. Linux also needs `zstd` for the pinned packages. `tools/setup-devkit.py` installs the native SDK inside the kit without changing system packages. A source rebuild additionally needs `make`.
- A native Switch 2 game you can use for the documented hold-R hbmenu entry after installation.

Extract the backend ZIP and run the remaining commands from its outer folder, the one containing `START_HERE.txt` and `tools/`. Developers rebuilding from source can use the [repository](https://github.com/H644b/Moader) and [build guide](BUILDING.md).

### Apple Silicon macOS

Install [Homebrew](https://brew.sh/) if needed, finish any Apple command line tools prompt, then run:

```sh
xcode-select --install  # only if Apple command line tools are missing
brew install python node@24 git
export MOADER_NODE24="$(brew --prefix node@24)/bin/node"
"$MOADER_NODE24" --version
python3 --version
```

### Linux x86_64 or Windows with WSL2

On Ubuntu or Debian, keep the extracted kit in the Linux filesystem (for example under `~/Moader-backend-<version>`). Install [Linux Node.js 24](https://nodejs.org/en/download/) inside Linux, then run:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends -y python3 git clang zstd curl ca-certificates
export MOADER_NODE24="$(command -v node)"
"$MOADER_NODE24" --version
"$MOADER_NODE24" -p 'process.platform'
python3 --version
```

Node must report `v24`, and Python must be at least `3.10`. On WSL, Node must report `linux`; Windows `node.exe` and Windows paths are not supported. Other Linux distributions can supply the same tools through their package manager. `xcode-select --install` on macOS may report that the tools are already installed.

#### WSL2 network check

Seedsow hosts both the HTTP launcher on TCP **8080** and the owned binary listener on TCP **39380**. The Linux process connects **outbound** to the Switch on both ports. Once the game is open and **Y** has started its launcher, confirm that WSL can reach `http://SWITCH_LAN_IP:8080/` before beginning a run. The listener on 39380 starts only during the single owned attempt; do not start or probe it separately. The GUI binds only to `127.0.0.1:31338` inside WSL, and [Windows browsers normally reach WSL services through localhost](https://learn.microsoft.com/en-us/windows/wsl/networking#accessing-linux-networking-apps-from-windows-localhost). Open the printed GUI URL in your Windows browser if the Linux launcher does not open one. Keep the build, GUI and installation in the same WSL distro with Linux Node and paths. This route does not use the inbound host listener or port-forwarding instructions for the separate direct ns2jb route.

## Prepare and check the matched inputs

Run these before contacting the Switch:

```sh
python3 tools/setup-devkit.py
python3 tools/setup-backend.py --host-profile seedsow-malev0 --node "$MOADER_NODE24"
python3 tools/setup-backend.py --check --host-profile seedsow-malev0 --node "$MOADER_NODE24"
export MOADER_AARCH64_OBJCOPY="$PWD/vendor/devkitpro/devkitA64/bin/aarch64-none-elf-objcopy"
```
### On macOS use `export MOADER_AARCH64_CLANG="$(xcrun --find clang)"` and on Linux or WSL2 use `export MOADER_AARCH64_CLANG="$(command -v clang)"`

The first command installs the local SDK used by the live helper. The second validates the ZIP's prebuilt matched components, extracts the complete official kaiHEN programs and fetches the pinned Seedsow source closure. It does not compile Moader. The third checks the prepared inputs and should report `hostProfile: "seedsow-malev0"` and `readyForInstallerCheck: true`. Keep the two tool paths set in the terminal that starts the GUI.

For offline preparation with the exact pinned archives, add `--offline --engine-archive PATH --kaihen-archive PATH` to the second command. To build from source instead, follow [Building](BUILDING.md#build-from-a-clean-source-checkout).

## Prepare and open the GUI

Provision a **new** GUI folder from the exact reviewed upstream revision, apply Moader's pinned patch, and start it:

```sh
export MOADER_SEEDSOW_GUI="$PWD/../seedsow-fs-kaihen-moader-main"
"$MOADER_NODE24" tools/prepare-seedsow-gui.cjs --provision "$MOADER_SEEDSOW_GUI"
"$MOADER_NODE24" tools/prepare-seedsow-gui.cjs --check "$MOADER_SEEDSOW_GUI"
tools/start-seedsow-gui.sh "$MOADER_SEEDSOW_GUI"
```

Keep this Terminal open and open the local GUI address it prints. In the GUI, set the Switch's current LAN address and confirm **Console Online** after the Seedsow launcher starts. Select **Auto-Download Moader kaiHEN**. The current kit uses its included complete patched release and checks every unchanged file against the pinned official base, plus the matched RO/HEN/bootstrap and Moader bytes. Wait for the GUI to confirm the matching bundle. Use the Moader install button in the next step.

The GUI source update button is disabled in this integration so it cannot silently replace the pinned patch. `--provision` refuses an existing destination; reuse a folder only if the current kit's `--check` accepts it. Provision a new folder if an existing checkout fails that check.

## Install on one fresh boot

1. Fully reboot the Switch. Open Seedsow Lullaby Demo or the full game with its matching working launcher save, then press **Y**. Wait for **Console Online** in the GUI. Do not run FS acquisition or regular kaiHEN separately on this boot.
2. Select **Install Moader on this boot** **once**. Leave the host computer, GUI and Seedsow running. Wait while the installer sets up the backend and checks the running service.
3. Wait for **Moader setup complete; FS cache and listener closed** in the GUI. The service and game-arm check is an intermediate step; keep Seedsow open until the final message and successful host exit. The final receipt confirms one FS retirement, restored cache, and closure of the owned TCP/HTTP listener. Seedsow's remaining native handles and scratch release when you close Seedsow normally. If setup stops or the result is uncertain, follow [Recovery](#recovery).
4. Press **HOME** and close Seedsow. Hold **R** while starting a native Switch 2 game. Let it load, close it, then hold **R** while starting it again to enter hbmenu.
5. Open **Moader**. Check the runtime connection under **Settings → Advanced**, then [pair your browser](USAGE.md#connect-a-browser).
6. Press **+** to return to hbmenu. Reload the page to confirm the resident is still serving it.

The backend runs until you reboot. Repeat setup with a fresh Seedsow session for that boot. Your library and settings stay on SD.

## Recovery

- **Console Offline:** check the IP shown by Seedsow, host-to-Switch network access, and that you pressed **Y**. Reaching the Switch with ping alone does not prove its HTTP launcher is listening. On WSL, test from the same Linux distro running the GUI.
- **Build or source check failed:** stop before the device attempt. Use the pinned checkout and source profile; do not substitute a different Seedsow source or mix RO, HEN and bootstrap versions. `--check` is safe to repeat because it does not contact the console.
- **Acquisition missed, worker timed out, GUI stopped, or the game froze:** preserve the run journal. An uncertain native operation may have completed even when the host lost its reply. Do not press Install, Start regular kaiHEN, Stop run, or Attempt failed again on that boot. Fully reboot, reopen the same Seedsow edition and press **Y** before a new attempt. The GUI's **Full reboot completed** button archives only its host marker; it does not clean up the prior boot.
- **Moader app opens without connected FS:** check the run's completed installation and HEN status, and whether a full reboot occurred. Copying an NRO or NSO alone does not activate the resident or FS routing.
- **Setup completed but R does not open hbmenu:** use a native Switch 2 game, and fully close it from HOME before the second launch. Record the game, whether the Switch was docked or slept, and whether `/kaihen/bin/game/main.npdm` exists on SD after the first launch if you can inspect the card. Leave that file untouched. The installation log ends before game entry and cannot show which later step failed. An [upstream kaiHEN report](https://github.com/ouncebrew/kaihen/issues/3) found handheld success after docked failures, but that observation does not identify the cause on another console. Preserve the GUI run log; do not rerun FS or HEN on the same boot.
- **Browser does not load:** press **Y** in Moader and use the URL currently shown by **ZR**. The console-hosted listener normally uses port 7712; the computer and Switch must share a reachable local network.

For a failed attempt, collect the GUI log and its new `build/seedsow-runs/` folder privately. Do not post the launcher save, live run directory, pairing code or captured device memory in an issue. The [validation record](VALIDATION.md#seedsow-lullaby-demo-source-workflow) explains what this path has and has not proved.
