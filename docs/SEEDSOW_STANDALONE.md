# Seedsow on-console Moader route (experimental)

The [Seedsow console-only chain](https://github.com/Malev0/seedsow-fs-kaihen/blob/ff14e26cb950db5cff0da911d28774404adb5cd1/console-only/README.md), adapted from [rushinz's on-console Lua port](https://gitlab.com/rushinz/seedsow-semi-untethered), runs FS acquisition and kaiHEN from Seedsow after a one-time save upload. Moader's preparation tool pins that upstream revision and stages the complete matched Moader kaiHEN set on SD. The computer is needed to prepare the SD and upload the save; later runs of this *kaiHEN chain* do not need it.

**Moader installation is incomplete on this route.** One Profile B Demo run reached FS promotion, kaiHEN READY, hbmenu and the Moader app, but the app reported its native filesystem runtime disconnected. The standalone chain does not yet install Moader's separate FS hook. Its READY line proves kaiHEN startup, not a working Moader backend. Use the [PC-driven Seedsow route](SEEDSOW.md) for a complete Moader installation. The steps below record the experimental preparation and diagnostic run; do not present them as a working Moader installer. [Device evidence](VALIDATION.md#seedsow-standalone-host-preparation).

## Requirements and boundaries

- Seedsow Lullaby Demo or full game with a working HTTP launcher save for that edition, a Switch 2 on firmware supported by the [upstream Seedsow project](https://github.com/Malev0/seedsow-fs-kaihen), and an SD card that can be mounted on the computer. Keep the Demo and full-game saves separate.
- The matching Moader source checkout or a backend kit **containing this tool**, Python 3.10+, Git and Node.js 20+. The published v0.2.5 backend ZIP predates this standalone tool. macOS host preparation has been checked; Linux and WSL2 use the same Python and Node paths, but their standalone route has not yet been verified end to end.
- A working launcher save on a **second console profile** before uploading, as the upstream instructions recommend. The uploader writes and commits `save:/` on the selected profile. Keep the original profile for the PC-driven route.

### If Checkpoint cannot back up the working Seedsow Demo save

The unofficial [Switch 2 Checkpoint fork](https://github.com/wowilookgood/Checkpoint/releases/tag/v5.3.0-switch2-1) has a [Seedsow Demo report](https://github.com/wowilookgood/Checkpoint/issues/1) where backup error `0x1559` follows an unreadable `NS2_GAP_.../canary` scratch entry. Checkpoint refuses that backup because it would be incomplete. Leave that original profile's save untouched; do not treat the failed backup as a recovery copy.

For the Demo, the reported workaround is to make a normal Seedsow save on a **new profile** and back up that clean save with Checkpoint. In Checkpoint, use the bottom-left user avatar to select that profile. Download [alula's v2 Demo launcher save](https://github.com/alula/switch-artemis-launcher/releases/tag/v2); its `seedsow_save.zip` asset has SHA-256 `dfd58a4e1151d29e9152b3a528568f95a990aa427914bafc21711e9b7fca691e` and contains nine launcher files. Extract those files into a **new sibling folder** beside the clean backup under Checkpoint's Seedsow Demo save directory on SD (normally `switch/Checkpoint/saves/0x010042D022AEA000 Seedsow Lullaby Demo/`). Leave the clean backup folder intact. Reopen Checkpoint on the new profile and restore the new launcher folder. Confirm that **Y** starts its HTTP launcher on that profile before using `upload-save` below. Checkpoint's Switch 2 release reports backup testing but lists restore as untested in general. One Profile B transfer in this Moader investigation reported success and its launcher answered HTTP 200. The Demo archive is not a full-game save.

Use the complete kaiHEN archive supplied by this Moader checkout or matching backend kit. The tool verifies the official 20261114c base, Moader's source-qualified protocol-7 RO/HEN/bootstrap, the app and resident, then stages the full release. Do not use upstream **Prepare SD** or **Update kaiHEN** afterward: those buttons stage the regular upstream build over Moader's matched files.

A clean source checkout does not contain the ignored prebuilt files. Use a matching backend kit containing this tool, or provide the **exact** app, resident and RO/HEN/bootstrap bytes recorded in `config/release.json`. A local `--build` can produce different app/resident bytes on another host and does not by itself satisfy this release-identity check. Do not copy binaries from a different Moader version into this checkout.

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

## Experimental kaiHEN and Moader diagnostic on a fresh boot

1. Fully reboot the Switch. Leave other FS and kaiHEN methods unrun on this boot. Open Seedsow using the profile with the standalone save and press **A** at its launcher. The on-console chain now runs without the computer.
2. Wait for `KH: === READY! Close Seedsow Lullaby and hold R while launching a game ===`. That line reports the standalone HEN chain's completion; it does not by itself prove the Moader resident or game entry.
3. Close Seedsow from HOME. Launch a native Switch 2 game normally once and let it run briefly, then close it completely. Hold **R** from before selecting the same game through its next launch to enter hbmenu. The first launch records the game policy when none exists; the second launch is the takeover attempt.
4. Open Moader. Under **Settings → Advanced**, check that runtime and FS are connected. The first device run reported both disconnected because this route has no Moader FS hook installation step. A working web listener or hbmenu does not change that result. Do not treat the route as a completed Moader install or test mods until a same-boot runtime and FS acknowledgement is verified. Keep pairing codes private.

The HEN and any started Moader resident last only until reboot. The save and staged SD files remain, but repeating the same chain will not add the missing Moader FS hook. Do not start the PC-driven Seedsow installer on a boot where the standalone chain has already run.

If the chain stops, freezes, or reports an uncertain result, keep its on-screen log or `/kaihen/seedsow_run.log` and fully reboot before another Moader attempt. If the chain reports ready but R does not open hbmenu, record the game and whether `/kaihen/bin/game/main.npdm` appears after the first launch; do not delete that policy file. [Validation scope](VALIDATION.md#seedsow-standalone-host-preparation).
