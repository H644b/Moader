# Install Moader through Seedsow Lullaby Demo

This is the **Apple Silicon macOS source workflow** for the Seedsow Lullaby Demo entrypoint. It builds Moader and its matched kaiHEN components, then uses Moader's patch of the [Seedsow FS GUI](https://github.com/Malev0/seedsow-fs-kaihen). The published v0.1.2 backend ZIP uses other entrypoints and does not contain this workflow.

One Demo installation on firmware 23.0.1 passed current-boot FS memory and RO checks, started the matched HEN and Moader resident, opened the console app, and kept the paired browser available after the app closed. See the [exact validation scope](VALIDATION.md#seedsow-lullaby-demo-source-workflow). Other firmware, Seedsow Full Game, and other host platforms have not been tested with this Moader path.

## Before you begin

- A Switch 2 with Seedsow Lullaby Demo and its working launcher save. Follow the [upstream Seedsow setup](https://github.com/Malev0/seedsow-fs-kaihen#setup-once) if you do not already have that entrypoint. This guide does not change or replace your save.
- An Apple Silicon Mac and Switch on the same local network. The Demo's HTTP launcher must be reachable from the Mac after you press **Y**.
- Python 3.10 or newer, POSIX Node.js **24**, Git, `make`, Apple command line tools with AArch64 capable `clang`, and room for the local SDK and build. Homebrew can provide Python and Node 24; `tools/setup-devkit.py` installs the pinned native SDK inside this checkout.
- A native Switch 2 game you can use for the documented hold-R hbmenu entry after installation.

Use the source revision that contains this guide. These commands select the tested development branch. Install [Homebrew](https://brew.sh/) first if you do not already have it; finish any Apple command line tools prompt before continuing:

```sh
xcode-select --install  # only if Apple command line tools are missing
brew install python node@24 git
git clone --branch codex/seedsow-local-integration --single-branch https://github.com/H644b/Moader.git
cd Moader
export MOADER_NODE24="$(brew --prefix node@24)/bin/node"
"$MOADER_NODE24" --version
python3 --version
```

Node must report `v24` and Python at least `3.10`. `xcode-select --install` may report that the tools are already installed. If you obtained the checkout another way, run the remaining commands from its root, where `tools/` and `config/` are present.

## Build and check the matched inputs

Run these before contacting the Switch:

```sh
python3 tools/setup-devkit.py
python3 tools/setup-backend.py --build --host-profile seedsow-malev0 --node "$MOADER_NODE24"
python3 tools/setup-backend.py --check --host-profile seedsow-malev0 --node "$MOADER_NODE24"
export MOADER_AARCH64_CLANG="$(xcrun --find clang)"
export MOADER_AARCH64_OBJCOPY="$PWD/vendor/devkitpro/devkitA64/bin/aarch64-none-elf-objcopy"
```

The first command verifies checksum-pinned SDK packages. The second downloads the pinned Seedsow source closure and official kaiHEN base, builds Moader's app, resident, RO, HEN and bootstrap in an isolated source copy, and rejects mismatched output. The third is a read-only local check; it should report `hostProfile: "seedsow-malev0"` and `readyForInstallerCheck: true`. The `clang` and `objcopy` variables select the compilers used to assemble one hash-checked helper during a live attempt. Apple command line tools and the pinned SDK reproduced that helper's 72-byte digest on the tested Mac.

The upstream Seedsow source and downloaded archives stay under ignored `research/`; the SDK stays under ignored `vendor/`. None is a console session. If you have already supplied the exact pinned archives, the build command can use `--offline --engine-archive PATH --kaihen-archive PATH`; otherwise leave `--offline` out.

## Prepare and open the GUI

Provision a **new** GUI folder from the exact reviewed upstream revision, apply Moader's pinned patch, and start it:

```sh
export MOADER_SEEDSOW_GUI="$PWD/../seedsow-fs-kaihen-moader"
"$MOADER_NODE24" tools/prepare-seedsow-gui.cjs --provision "$MOADER_SEEDSOW_GUI"
"$MOADER_NODE24" tools/prepare-seedsow-gui.cjs --check "$MOADER_SEEDSOW_GUI"
tools/start-seedsow-gui.sh "$MOADER_SEEDSOW_GUI"
```

Keep this Terminal open and open the local GUI address it prints. In the GUI, set the Switch's current LAN address and confirm **Console Online** after the Demo launcher starts. Select **Auto-Download Moader kaiHEN**. This button downloads the checksum-pinned official base and stages the exact locally built RO, HEN, bootstrap, `Moader.nro` and resident plugin. Wait for its matched-bundle result. **Start regular kaiHEN** is a separate upstream action and does not install the Moader backend.

The GUI source update button is disabled in this local integration so it cannot silently replace the pinned patch. `--provision` refuses an existing destination; reuse an already checked folder with `--check` and `start-seedsow-gui.sh`.

## Install on one fresh boot

1. Fully reboot the Switch. Open Seedsow Lullaby Demo with the working launcher save, then press **Y**. Wait for **Console Online** in the GUI. Do not run FS acquisition or regular kaiHEN separately on this boot.
2. Select **Install Moader on this boot** **once**. Leave the Mac, GUI and Demo running. The controller checks fresh Demo code signatures, owns one bounded acquisition, proves FS memory read/write/restoration, runs RO checks, installs the Moader runtime and matched HEN, then inspects the running HEN and resident service.
3. Continue when the GUI reports the held **Moader device-verification gate**. A prepared bundle, launcher connection, or copied NRO alone is not this result. If the GUI stops or reports an uncertain native result, follow [Recovery](#recovery) before another attempt.
4. Press **HOME** and close Seedsow. Hold **R** while starting a native Switch 2 game. Let it load, close the game, then hold **R** while starting it a second time to enter hbmenu. This is the upstream kaiHEN route used in the device test.
5. Open **Moader** from hbmenu. Press **ZL** and check that the native runtime and FS/backend report connected. Press **Y** if the web listener is off; **ZR** displays its current URL and private six-digit pairing code. Open that URL from a browser on your local network, enter the code there, and check the library and settings. Keep the code private.
6. Press **+** to return to hbmenu. The paired browser should still load because the resident service runs separately. Enable a mod only when you intend to test that game's file routing. Read its apply state; cached content may need to reopen and executable changes may need a game relaunch. [Mod use and limits](USAGE.md).

The backend runs for this boot. The library and settings remain on SD, but a full Switch reboot ends the running HEN, FS routing and resident service. Repeat this workflow from a new Demo launcher session when you need the backend on a later boot. Do not replay a saved run, PID, handle, address or installer command.

## Recovery

- **Console Offline:** check the IP shown by the Demo, the Mac and Switch network, and that you pressed **Y**. Reaching the Switch with ping alone does not prove its HTTP launcher is listening.
- **Build or source check failed:** stop before the device attempt. Use the pinned checkout and source profile; do not substitute a different Seedsow source or mix RO, HEN and bootstrap versions. `--check` is safe to repeat because it does not contact the console.
- **Acquisition missed, worker timed out, GUI stopped, or the game froze:** preserve the run journal. An uncertain native operation may have completed even when the host lost its reply. Do not press Install, Start regular kaiHEN, Stop run, or Attempt failed again on that boot. Fully reboot, reopen Demo and press **Y** before a new attempt. The GUI's **Full reboot completed** button archives only its host marker; it does not clean up the prior boot.
- **Moader app opens without connected FS:** check the run's completed installation and HEN status, and whether a full reboot occurred. Copying an NRO or NSO alone does not activate the resident or FS routing.
- **Browser does not load:** press **Y** in Moader and use the URL currently shown by **ZR**. The console-hosted listener normally uses port 7712; the computer and Switch must share a reachable local network.

For a failed attempt, collect the GUI log and its new `build/seedsow-runs/` folder privately. Do not post the launcher save, live run directory, pairing code or captured device memory in an issue. The [validation record](VALIDATION.md#seedsow-lullaby-demo-source-workflow) explains what this path has and has not proved.
