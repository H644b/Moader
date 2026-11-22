# Seedsow on-console Moader route

This experimental route needs a computer for initial setup. Later boots run on the Switch. [Test results](VALIDATION.md#v300-standalone-release-scope)

## Requirements and boundaries

- A Switch 2 on [supported firmware](https://github.com/Malev0/seedsow-fs-kaihen), a mountable SD card, and a working Seedsow launcher save for your edition. Keep Demo and full-game saves separate.
- The [v4.0.0 backend kit](https://github.com/H644b/Moader/releases/download/v4.0.0/Moader-backend-v4.0.0.zip), Python 3.10+, Git, Node.js 20+, and `luac`. The kit includes the standalone bootstrap. Host preparation was checked on macOS.
- The [qualified local filesystem capture](#obtain-and-check-the-fs-capture) for A. The kit omits the capture and its derived payload.
- A working launcher save on a **second console profile** and backups for any saves you want to keep. The uploader changes the save of the profile running Seedsow. Confirm that profile on the Switch.

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
    raise SystemExit("This capture is not accepted by the v4.0.0 standalone A generator")
print("Capture is eligible for the v4.0.0 standalone A generator")
PY
```

Continue only if the check passes. The generator accepts a single pinned digest. A successful PC-driven setup can produce a different capture. If yours differs, use the PC-driven method. Keep the capture, manifest, and log private.

## Prepare the SD card on the computer

Use the extracted backend kit folder containing `tools/` and `config/`.

```sh
node tools/seedsow-standalone-fs-payload.cjs --capture "$CAPTURE" --output build/standalone-fs-payload
python3 tools/seedsow-standalone.py provision --node "$(command -v node)"
python3 tools/seedsow-standalone.py check --payload-dir build/standalone-fs-payload --node "$(command -v node)"
python3 tools/seedsow-standalone.py stage-sd --payload-dir build/standalone-fs-payload --sd-root "/path/to/mounted/SD-root"
python3 tools/seedsow-standalone.py verify-sd --payload-dir build/standalone-fs-payload --sd-root "/path/to/mounted/SD-root"
```

Use the root of the mounted SD card. After verification passes, eject the card and put it in the Switch.

## Upload the standalone save once

Launch Seedsow explicitly with the prepared **Profile B** launcher save and press **Y** to enable its HTTP launcher. From the same Moader folder run:

```sh
python3 tools/seedsow-standalone.py upload-save --host SWITCH_LAN_IPV4 --payload-dir build/standalone-fs-payload --node "$(command -v node)"
```

Wait for `commit_rc=0x0`. It confirms the save was written. Confirm the profile on the Switch and use a backed-up save.

Fully reboot, launch Seedsow explicitly with Profile B and press Y. Check that the required A/B save files persisted on **that running profile** before a native run:

```sh
python3 tools/seedsow-standalone.py verify-save --host SWITCH_LAN_IPV4 --node "$(command -v node)"
```

This check confirms file presence and size. Confirm the profile on the Switch. After Y, close and reopen Seedsow with Profile B before choosing A or B.

Choose one per full reboot. **A** starts the Moader filesystem runtime. **B** uses the regular upstream Lua chain. Both use the same SD files.

## Experimental Moader installation check on a fresh boot

1. Fully reboot the Switch. Leave other FS and kaiHEN methods unrun on this boot. Open Seedsow using the profile with the standalone save and press **A** at its launcher. The on-console chain now runs without the computer.
2. Look for the Moader FS installer lines showing source qualification, 26 mapped executable pages, retained SD/report references and 19 active hooks, then wait for `KH: === READY! Close Seedsow Lullaby and hold R while launching a game ===`. Any uncertain stop requires a full reboot. READY still does not prove the Moader resident or game entry.
3. Close Seedsow from HOME. Launch a native Switch 2 game normally once and let it run briefly, then close it completely. Hold **R** from before selecting the same game through its next launch to enter hbmenu. The first launch records the game policy when none exists; the second launch is the takeover attempt.
4. Open Moader. Under **Settings → Advanced**, check that runtime and FS are connected. Confirm the current catalogue and FS acknowledgement have the same epoch and generation before treating the installation as complete. A working web listener or hbmenu alone does not prove that. Keep pairing codes private.

The backend stops at reboot. The save and SD files remain. Avoid running the PC installer on a boot where standalone setup already ran.

If setup stops, freezes, or gives an uncertain result, keep the on-screen log or `/kaihen/seedsow_run.log` and fully reboot. If setup reports ready but **R** fails to open hbmenu, record the game and whether `/kaihen/bin/game/main.npdm` appeared. Leave the policy file in place.

Upstream work by [Malev0](https://github.com/Malev0/seedsow-fs-kaihen/blob/ff14e26cb950db5cff0da911d28774404adb5cd1/console-only/README.md) and [rushinz](https://gitlab.com/rushinz/seedsow-semi-untethered).
