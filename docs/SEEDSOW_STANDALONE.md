# Seedsow on-console Moader route (experimental)

The [Seedsow console-only chain](https://github.com/Malev0/seedsow-fs-kaihen/blob/ff14e26cb950db5cff0da911d28774404adb5cd1/console-only/README.md), adapted from [rushinz's on-console Lua port](https://gitlab.com/rushinz/seedsow-semi-untethered), runs FS acquisition and kaiHEN from Seedsow after a one-time save upload. Moader's preparation tool pins that upstream revision and stages the complete matched Moader kaiHEN set on SD. The computer is needed to prepare the SD and upload the save, but not for subsequent on-console runs. This integration has passed host source and SD readback checks; a Moader installation through the console-only chain has **not yet been verified on a Switch**. The existing [PC-driven Seedsow route](SEEDSOW.md) remains separate.

## Requirements and boundaries

- Seedsow Lullaby Demo or full game with a working HTTP launcher save for that edition, a Switch 2 on firmware supported by the [upstream Seedsow project](https://github.com/Malev0/seedsow-fs-kaihen), and an SD card that can be mounted on the computer. Keep the Demo and full-game saves separate.
- The matching Moader source checkout or a backend kit **containing this tool**, Python 3.10+, Git and Node.js 20+. The published v0.2.5 backend ZIP predates this standalone tool. macOS host preparation has been checked; Linux and WSL2 use the same Python and Node paths, but their standalone route has not yet been verified end to end.
- A copy of the launcher save on a **second console profile** before uploading, as the upstream instructions recommend. The uploader writes and commits `save:/` on the selected profile. Keep the original profile for the PC-driven route.

Use the complete kaiHEN archive supplied by this Moader checkout or matching backend kit. The tool verifies the official 20261114c base, Moader's source-qualified protocol-7 RO/HEN/bootstrap, the app and resident, then stages the full release. Do not use upstream **Prepare SD** or **Update kaiHEN** afterward: those buttons stage the regular upstream build over Moader's matched files.

A clean source checkout does not contain the ignored prebuilt files. Before the commands below, follow [Build from a clean source checkout](BUILDING.md#build-from-a-clean-source-checkout) with `--build --host-profile seedsow-malev0`, then run its `--check`. A backend kit packaged from this work will contain those exact files already. Do not copy binaries from a different Moader version into this checkout.

## Prepare the SD card on the computer

From the Moader folder that contains `tools/` and `config/`, run:

```sh
python3 tools/seedsow-standalone.py provision --node "$(command -v node)"
python3 tools/seedsow-standalone.py check --node "$(command -v node)"
python3 tools/seedsow-standalone.py stage-sd --sd-root "/path/to/mounted/SD-root"
python3 tools/seedsow-standalone.py verify-sd --sd-root "/path/to/mounted/SD-root"
```

Replace the SD path with its actual **root**, not a `kaihen` or `switch` subfolder. On macOS it is commonly under `/Volumes/`; on Linux or WSL2 use the mounted card's actual Linux path. The tool refuses a system or project directory and symlinked SD destinations. It creates the upstream documented `kaihen/bin/{bootstrap.bin,ro/main,hen/main,game/main}` layout, mirrors the full patched release under `kaihen/`, and writes `hbmenu.nro`, `switch/ounceshell.nro`, `switch/Moader.nro` and `kaihen/plugins/moader.nso`. It reads back every staged file and reports the exact upstream revision and Moader bundle identity. It does not contact the Switch or run the exploit.

Safely eject the card and put it in the Switch before continuing.

## Upload the standalone save once

On the profile holding the **backup copy** of the launcher save, start Seedsow and press **Y** to enable its HTTP launcher. From the same Moader folder run:

```sh
python3 tools/seedsow-standalone.py upload-save --host SWITCH_LAN_IPV4 --node "$(command -v node)"
```

The tool rechecks all 14 pinned Lua modules and the upstream uploader before calling it. The uploader writes the standalone modules to this profile's Seedsow save and commits the save. Wait for its `commit_rc=0x0` result. Do not use this command against an unrelated or unbacked-up save. This upload is preparation only; it does not acquire FS or start HEN.

## Start Moader on a fresh boot

1. Fully reboot the Switch. Leave other FS and kaiHEN methods unrun on this boot. Open Seedsow using the profile with the standalone save and press **A** at its launcher. The on-console chain now runs without the computer.
2. Wait for `KH: === READY! Close Seedsow Lullaby and hold R while launching a game ===`. That line reports the standalone HEN chain's completion; it does not by itself prove the Moader resident or game entry.
3. Close Seedsow from HOME. Launch a native Switch 2 game normally once and let it run briefly, then close it completely. Hold **R** from before selecting the same game through its next launch to enter hbmenu. The first launch records the game policy when none exists; the second launch is the takeover attempt.
4. Open Moader. Under **Settings → Advanced**, check that runtime and FS are connected. Check the web listener and a mod only after this same-boot status succeeds. Keep pairing codes private.

The HEN and Moader resident last only until reboot. The save and staged SD files remain, so later fresh boots start again at step 1. Do not start the PC-driven Seedsow installer on a boot where the standalone chain has already run.

If the chain stops, freezes, or reports an uncertain result, keep its on-screen log or `/kaihen/seedsow_run.log` and fully reboot before another Moader attempt. If the chain reports ready but R does not open hbmenu, record the game and whether `/kaihen/bin/game/main.npdm` appears after the first launch; do not delete that policy file. [Validation scope](VALIDATION.md#seedsow-standalone-host-preparation).
