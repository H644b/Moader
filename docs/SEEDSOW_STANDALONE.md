# Seedsow on-console Moader route (experimental)

This experimental method starts on the Switch after one-time computer preparation. The Moader route requires a qualified local filesystem capture. The v3.0.0 kit includes the standalone bootstrap.

One Demo run recorded a connected filesystem runtime; a later owner test confirmed the v3.0.0 app and service connected on the same console. See the [device test results](VALIDATION.md#v300-standalone-release-scope) for the tested setup and limits.

This method builds on [Malev0's Seedsow console-only chain](https://github.com/Malev0/seedsow-fs-kaihen/blob/ff14e26cb950db5cff0da911d28774404adb5cd1/console-only/README.md) and [rushinz's on-console Lua port](https://gitlab.com/rushinz/seedsow-semi-untethered).

## Requirements and boundaries

- Seedsow Lullaby Demo or full game with a working HTTP launcher save for that edition, a Switch 2 on firmware supported by the [upstream Seedsow project](https://github.com/Malev0/seedsow-fs-kaihen), and an SD card that can be mounted on the computer. Keep the Demo and full-game saves separate.
- The matching [v3.0.0 backend kit](https://github.com/H644b/Moader/releases/download/v3.0.0/Moader-backend-v3.0.0.zip), Python 3.10+, Git, Node.js 20+ and `luac`. The kit includes the verified source-built standalone bootstrap in `prebuilt/standalone-bootstrap/`; no SDK is needed to stage that prebuilt. A clean source checkout instead needs the pinned `vendor/devkitpro` toolchain to reproduce it ([SDK setup](BUILDING.md#build-from-a-clean-source-checkout)). macOS host preparation has been checked; Linux and WSL2 use the same Python and Node paths, but their standalone route has not yet been verified end to end.
- The exact previously qualified sparse FS capture with SHA-256 `0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271`. The generator refuses any other capture. This private console capture and its derived payload are **not in the v3.0.0 kit**. [Obtain and check a local capture](#obtain-and-check-the-fs-capture) before preparing A. Without the exact qualified input, the Moader A route cannot be prepared from the public kit alone.
- A working launcher save on a **second console profile** before uploading, as the upstream instructions recommend. The uploader writes and commits `save:/` for the profile that is actually running Seedsow. Confirm the profile at game launch; the HTTP uploader cannot identify it from the console address. Back up any profile whose save you need to preserve.

### If Checkpoint cannot back up the working Seedsow Demo save

The unofficial [Switch 2 Checkpoint fork](https://github.com/wowilookgood/Checkpoint/releases/tag/v5.3.0-switch2-1) has a [Seedsow Demo report](https://github.com/wowilookgood/Checkpoint/issues/1) where backup error `0x1559` follows an unreadable `NS2_GAP_.../canary` scratch entry. Checkpoint refuses that backup because it would be incomplete. Leave that original profile's save untouched; do not treat the failed backup as a recovery copy.

For the Demo, the reported workaround is to make a normal Seedsow save on a **new profile** and back up that clean save with Checkpoint. In Checkpoint, use the bottom-left user avatar to select that profile. Download [alula's v2 Demo launcher save](https://github.com/alula/switch-artemis-launcher/releases/tag/v2); its `seedsow_save.zip` asset has SHA-256 `dfd58a4e1151d29e9152b3a528568f95a990aa427914bafc21711e9b7fca691e` and contains nine launcher files. Extract those files into a **new sibling folder** beside the clean backup under Checkpoint's Seedsow Demo save directory on SD (normally `switch/Checkpoint/saves/0x010042D022AEA000 Seedsow Lullaby Demo/`). Leave the clean backup folder intact. Reopen Checkpoint on the new profile and restore the new launcher folder. Confirm that **Y** starts its HTTP launcher on that profile before using `upload-save` below. Checkpoint's Switch 2 release reports backup testing but lists restore as untested in general. One Profile B transfer in this Moader investigation reported success and its launcher answered HTTP 200. The Demo archive is not a full-game save.

Use the complete kaiHEN archive supplied by this Moader checkout or matching backend kit. The tool verifies the official 20261114c base, Moader's source-qualified protocol-7 RO/HEN/bootstrap, the app and resident, then stages the full set with the source-built standalone bootstrap. Do not use upstream **Prepare SD** or **Update kaiHEN** afterward: those buttons stage the regular upstream build over Moader's matched files.

A clean source checkout does not contain the ignored prebuilt files. Use the matching v3.0.0 backend kit, or provide the **exact** app, resident and RO/HEN/bootstrap bytes recorded in `config/release.json` and build the standalone bootstrap from the pinned source. A local `--build` can produce different app/resident bytes on another host and does not by itself satisfy this release-identity check. Do not copy binaries from a different Moader version into this checkout.

## Obtain and check the FS capture

The [PC-driven Seedsow installer](SEEDSOW.md) reads FS code from a fresh, qualified session **before** installing Moader's hooks. It leaves a sparse `fs-4.bin` capture and its `manifest.json` on the computer in the extracted backend kit's `state/` directory. If you already completed that installer, use its existing files; otherwise, follow that guide on one fresh boot and wait for **Moader setup complete** before using the capture on a later boot. Keep the kit and its `state/` folder. You do not need to run the PC installer on each later standalone boot once A has been prepared.

Find the `MOADER_RUN .../build/seedsow-runs/switch-<run-id>` line in the successful GUI log. The capture for that run is `state/switch-<run-id>/native-fs/fs-4.bin` **under the same backend kit folder**; its qualification record is `state/switch-<run-id>/native-fs/manifest.json`. From that folder, replace the example run ID below with the one from your log and check the files locally:

```sh
RUN_ID="switch-REPLACE_WITH_YOUR_RUN_ID"
CAPTURE="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve(strict=True))' "state/$RUN_ID/native-fs/fs-4.bin")"
python3 - "$CAPTURE" <<'PY'
import hashlib, json, pathlib, sys

capture = pathlib.Path(sys.argv[1])
manifest = json.loads((capture.parent / "manifest.json").read_text())
digest = hashlib.sha256(capture.read_bytes()).hexdigest()
expected = "0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271"
print(f"capture: {capture}")
print(f"size: {capture.stat().st_size} bytes; SHA-256: {digest}")
if not (manifest.get("session") == capture.parent.parent.name
        and manifest.get("verified") is True
        and manifest.get("validatedRanges") == 36
        and manifest.get("runtimeLoggingCompatible") is True
        and manifest.get("sha256") == digest
        and capture.stat().st_size == 2416640
        and digest == expected):
    raise SystemExit("This capture is not accepted by the v3.0.0 standalone A generator")
print("Capture is eligible for the v3.0.0 standalone A generator")
PY
```

Use `"$CAPTURE"` in the `--capture` command below only if this check passes. A successful PC-driven installation creates and qualifies **its own** capture, but that does not guarantee it has the single hash pinned by the standalone generator. There is currently no standalone-only workflow that turns a new, different capture into an accepted A payload. If the digest differs, A cannot currently be prepared with that capture. Do not rename, edit or substitute bytes to make it pass; use the [PC-driven route](SEEDSOW.md) for that console until a new capture has been independently qualified and supported in a future build. Keep `fs-4.bin`, the manifest and run log private; they are not release assets.

## Prepare the SD card on the computer

From the extracted v3.0.0 backend kit folder containing `tools/` and `config/`, generate the local candidate payload once from the exact qualified capture. The kit supplies the checked standalone bootstrap. For a clean source checkout, first run `python3 tools/seedsow-standalone-bootstrap.py --output build/standalone-bootstrap`; this build reproduces the published regular bootstrap before creating the standalone candidate.

```sh
node tools/seedsow-standalone-fs-payload.cjs --capture "$CAPTURE" --output build/standalone-fs-payload
python3 tools/seedsow-standalone.py provision --node "$(command -v node)"
python3 tools/seedsow-standalone.py check --payload-dir build/standalone-fs-payload --node "$(command -v node)"
python3 tools/seedsow-standalone.py stage-sd --payload-dir build/standalone-fs-payload --sd-root "/path/to/mounted/SD-root"
python3 tools/seedsow-standalone.py verify-sd --payload-dir build/standalone-fs-payload --sd-root "/path/to/mounted/SD-root"
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

On later fresh boots, **A** runs the Moader FS candidate and **B** runs the original upstream Seedsow Lua chain without the Moader FS adapter. Both buttons read the **same staged SD kaiHEN files**; B is a regular upstream Lua route, not a separate unpatched native kaiHEN build. Choose one button per full reboot. The profile selected when Seedsow starts determines which save these entries use. On a separate full reboot with this shared SD set, the owner reported that B reached hbmenu and Moader showed disconnected; B does not install Moader's FS runtime.

## Experimental Moader installation check on a fresh boot

1. Fully reboot the Switch. Leave other FS and kaiHEN methods unrun on this boot. Open Seedsow using the profile with the standalone save and press **A** at its launcher. The on-console chain now runs without the computer.
2. Look for the Moader FS installer lines showing source qualification, 26 mapped executable pages, retained SD/report references and 19 active hooks, then wait for `KH: === READY! Close Seedsow Lullaby and hold R while launching a game ===`. Any uncertain stop requires a full reboot. READY still does not prove the Moader resident or game entry.
3. Close Seedsow from HOME. Launch a native Switch 2 game normally once and let it run briefly, then close it completely. Hold **R** from before selecting the same game through its next launch to enter hbmenu. The first launch records the game policy when none exists; the second launch is the takeover attempt.
4. Open Moader. Under **Settings → Advanced**, check that runtime and FS are connected. Confirm the current catalogue and FS acknowledgement have the same epoch and generation before treating the installation as complete. A working web listener or hbmenu alone does not prove that. Keep pairing codes private.

The HEN and any started Moader resident last only until reboot. The save and staged SD files remain. One A run has an independent same-boot FS acknowledgement, and a later v3 A run has an owner-reported connected result. Repeated v3 cold-boot reliability is not yet established. Do not start the PC-driven Seedsow installer on a boot where the standalone chain has already run.

If the chain stops, freezes, or reports an uncertain result, keep its on-screen log or `/kaihen/seedsow_run.log` and fully reboot before another Moader attempt. If the chain reports ready but R does not open hbmenu, record the game and whether `/kaihen/bin/game/main.npdm` appears after the first launch; do not delete that policy file. [Validation scope](VALIDATION.md#seedsow-standalone-host-preparation).
