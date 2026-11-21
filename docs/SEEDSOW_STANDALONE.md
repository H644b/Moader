# Seedsow on-console Moader route (experimental)

The [Seedsow console-only chain](https://github.com/Malev0/seedsow-fs-kaihen/blob/ff14e26cb950db5cff0da911d28774404adb5cd1/console-only/README.md), adapted from [rushinz's on-console Lua port](https://gitlab.com/rushinz/seedsow-semi-untethered), runs FS acquisition and kaiHEN from Seedsow after a one-time save upload. Moader's preparation tool pins that upstream revision and stages the complete matched Moader kaiHEN set on SD. The computer is needed to prepare the SD and upload the save; later runs of this *kaiHEN chain* do not need it.

**Profile B's A route reached `KH READY` and a connected Moader runtime and FS on one fresh Demo boot.** The saved run records 19 active FS hooks, HEN result `0x0`, both Loader restoration results `0x0`, and the Moader plugin listed by HEN. A same-boot read-only check found the current catalogue and CRC-valid FS acknowledgement at the same epoch and generation with result `0x0`. This verifies that exact staged pair on that console. A second cold boot, other consoles and games, and physical mod routing have not been checked. The isolated upstream **B** path previously reached `KH READY` and hbmenu with earlier shared SD files; it does not install Moader FS and has not been retested with the standalone bootstrap. The published release bootstrap is unchanged. [Device evidence](VALIDATION.md#seedsow-standalone-host-preparation).

## Requirements and boundaries

- Seedsow Lullaby Demo or full game with a working HTTP launcher save for that edition, a Switch 2 on firmware supported by the [upstream Seedsow project](https://github.com/Malev0/seedsow-fs-kaihen), and an SD card that can be mounted on the computer. Keep the Demo and full-game saves separate.
- The matching Moader source checkout, Python 3.10+, Git, Node.js 20+, `luac`, and the pinned `vendor/devkitpro` toolchain to build the standalone bootstrap. The published v0.2.5 backend ZIP predates this standalone tool. macOS host preparation has been checked; Linux and WSL2 use the same Python and Node paths, but their standalone route has not yet been verified end to end.
- For this development candidate, the exact previously qualified sparse FS capture with SHA-256 `0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271`. The generator refuses any other capture. This private capture and generated adapter payload are not in the public checkout or a release yet. A future tested kit can carry the generated files so its users do not need to capture FS themselves.
- A working launcher save on a **second console profile** before uploading, as the upstream instructions recommend. The uploader writes and commits `save:/` for the profile that is actually running Seedsow. Confirm the profile at game launch; the HTTP uploader cannot identify it from the console address. Back up any profile whose save you need to preserve.

### If Checkpoint cannot back up the working Seedsow Demo save

The unofficial [Switch 2 Checkpoint fork](https://github.com/wowilookgood/Checkpoint/releases/tag/v5.3.0-switch2-1) has a [Seedsow Demo report](https://github.com/wowilookgood/Checkpoint/issues/1) where backup error `0x1559` follows an unreadable `NS2_GAP_.../canary` scratch entry. Checkpoint refuses that backup because it would be incomplete. Leave that original profile's save untouched; do not treat the failed backup as a recovery copy.

For the Demo, the reported workaround is to make a normal Seedsow save on a **new profile** and back up that clean save with Checkpoint. In Checkpoint, use the bottom-left user avatar to select that profile. Download [alula's v2 Demo launcher save](https://github.com/alula/switch-artemis-launcher/releases/tag/v2); its `seedsow_save.zip` asset has SHA-256 `dfd58a4e1151d29e9152b3a528568f95a990aa427914bafc21711e9b7fca691e` and contains nine launcher files. Extract those files into a **new sibling folder** beside the clean backup under Checkpoint's Seedsow Demo save directory on SD (normally `switch/Checkpoint/saves/0x010042D022AEA000 Seedsow Lullaby Demo/`). Leave the clean backup folder intact. Reopen Checkpoint on the new profile and restore the new launcher folder. Confirm that **Y** starts its HTTP launcher on that profile before using `upload-save` below. Checkpoint's Switch 2 release reports backup testing but lists restore as untested in general. One Profile B transfer in this Moader investigation reported success and its launcher answered HTTP 200. The Demo archive is not a full-game save.

Use the complete kaiHEN archive supplied by this Moader checkout or matching backend kit. The tool verifies the official 20261114c base, Moader's source-qualified protocol-7 RO/HEN/bootstrap, the app and resident, then stages the full set with the source-built standalone bootstrap. Do not use upstream **Prepare SD** or **Update kaiHEN** afterward: those buttons stage the regular upstream build over Moader's matched files.

A clean source checkout does not contain the ignored prebuilt files. Use a matching backend kit containing this tool, or provide the **exact** app, resident and RO/HEN/bootstrap bytes recorded in `config/release.json`. A local `--build` can produce different app/resident bytes on another host and does not by itself satisfy this release-identity check. Do not copy binaries from a different Moader version into this checkout.

## Prepare the SD card on the computer

From the Moader folder that contains `tools/` and `config/`, generate the local candidate payload once from the exact qualified capture and build the standalone bootstrap. The bootstrap build first reproduces the published release bytes from the pinned source, then applies the standalone PM completion wait and checks the resulting SHA-256:

```sh
node tools/seedsow-standalone-fs-payload.cjs --capture "/path/to/qualified/fs-4.bin" --output build/standalone-fs-payload
python3 tools/seedsow-standalone-bootstrap.py --output build/standalone-bootstrap
python3 tools/seedsow-standalone.py provision --node "$(command -v node)"
python3 tools/seedsow-standalone.py check --payload-dir build/standalone-fs-payload --bootstrap-output build/standalone-bootstrap --node "$(command -v node)"
python3 tools/seedsow-standalone.py stage-sd --payload-dir build/standalone-fs-payload --bootstrap-output build/standalone-bootstrap --sd-root "/path/to/mounted/SD-root"
python3 tools/seedsow-standalone.py verify-sd --payload-dir build/standalone-fs-payload --bootstrap-output build/standalone-bootstrap --sd-root "/path/to/mounted/SD-root"
```

Replace the SD path with its actual **root**, not a `kaihen` or `switch` subfolder. On macOS it is commonly under `/Volumes/`; on Linux or WSL2 use the mounted card's actual Linux path. The tool refuses a system or project directory and symlinked SD destinations. It creates the upstream documented `kaihen/bin/{bootstrap.bin,ro/main,hen/main,game/main}` layout, mirrors the full patched release under `kaihen/`, and replaces both bootstrap copies with the checked standalone build. It writes `hbmenu.nro`, `switch/ounceshell.nro`, `switch/Moader.nro`, `kaihen/plugins/moader.nso` and eight checked files under `kaihen/moader-fs/`. It reads back every staged file and reports the exact upstream revision, Moader bundle, standalone bootstrap and FS payload identities. It does not contact the Switch or run the exploit.

Safely eject the card and put it in the Switch before continuing.

## Upload the standalone save once

Launch Seedsow explicitly with the prepared **Profile B** launcher save and press **Y** to enable its HTTP launcher. From the same Moader folder run:

```sh
python3 tools/seedsow-standalone.py upload-save --host SWITCH_LAN_IPV4 --payload-dir build/standalone-fs-payload --node "$(command -v node)"
```

The tool rechecks all 14 pinned upstream Lua modules and the uploader, then makes a temporary copy with the three Moader overlay modules for **A**, plus an isolated copy of the original upstream main/kaiHEN/RO modules for **B**. The uploader writes 18 modules and the launcher aliases to the currently running profile's Seedsow save and commits it. Wait for its `commit_rc=0x0` result. That receipt confirms a save commit, but does not identify the console profile. Do not use this command against an unrelated or unbacked-up save. This upload is preparation only; it does not acquire FS or start HEN.

Fully reboot, launch Seedsow explicitly with Profile B and press Y. Check that the required A/B save files persisted on **that running profile** before a native run:

```sh
python3 tools/seedsow-standalone.py verify-save --host SWITCH_LAN_IPV4 --node "$(command -v node)"
```

This makes a read-only request for nine A/B file presences and sizes; it does not identify the profile by itself or compare full file contents. After Y, close Seedsow and relaunch it with Profile B before pressing A or B, because the launcher accepts one choice per process.

On later fresh boots, **A** runs the Moader FS candidate and **B** runs the original upstream Seedsow Lua chain without the Moader FS adapter. Both buttons read the **same staged SD kaiHEN files**; B is a regular upstream Lua route, not a separate unpatched native kaiHEN build. Choose one button per full reboot. The profile selected when Seedsow starts determines which save these entries use. On a separate full reboot, B reached `KH READY` and hbmenu with the shared SD files; B does not install Moader's FS runtime.

## Experimental Moader installation check on a fresh boot

1. Fully reboot the Switch. Leave other FS and kaiHEN methods unrun on this boot. Open Seedsow using the profile with the standalone save and press **A** at its launcher. The on-console chain now runs without the computer.
2. Look for the Moader FS installer lines showing source qualification, 26 mapped executable pages, retained SD/report references and 19 active hooks, then wait for `KH: === READY! Close Seedsow Lullaby and hold R while launching a game ===`. Any uncertain stop requires a full reboot. READY still does not prove the Moader resident or game entry.
3. Close Seedsow from HOME. Launch a native Switch 2 game normally once and let it run briefly, then close it completely. Hold **R** from before selecting the same game through its next launch to enter hbmenu. The first launch records the game policy when none exists; the second launch is the takeover attempt.
4. Open Moader. Under **Settings → Advanced**, check that runtime and FS are connected. Confirm the current catalogue and FS acknowledgement have the same epoch and generation before treating the installation as complete. A working web listener or hbmenu alone does not prove that. Keep pairing codes private.

The HEN and any started Moader resident last only until reboot. The save and staged SD files remain. One computer-free Profile B A run after preparation succeeded; repeatability across cold boots is not yet established. Do not start the PC-driven Seedsow installer on a boot where the standalone chain has already run.

If the chain stops, freezes, or reports an uncertain result, keep its on-screen log or `/kaihen/seedsow_run.log` and fully reboot before another Moader attempt. If the chain reports ready but R does not open hbmenu, record the game and whether `/kaihen/bin/game/main.npdm` appears after the first launch; do not delete that policy file. [Validation scope](VALIDATION.md#seedsow-standalone-host-preparation).
