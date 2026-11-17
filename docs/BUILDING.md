# Building and preparing the backend

This source targets the **v0.1.2 release**. It retains protocol-7 service reload, corrected cartridge checks and the checked host installer while removing Moader's local pending-launch adjustment from the custom HEN source. The qualified official input remains `20261114c` at source base `e897f991524590833b82fbe8d63a3c6775fb8101`. Fresh native build and host qualification are pending; prior hardware records do not transfer automatically to the rebuilt components. See [Validation](VALIDATION.md) and the [release notes](releases/v0.1.2.md).

The precompiled kit supplies the matching binaries, corresponding source and portable native runtime assets. Preparation downloads required pinned upstream inputs; no compiler or SDK is needed for this host workflow. Source SDK rebuilds remain [Apple Silicon macOS only](#build-from-a-clean-source-checkout).

Unzip the kit, read `START_HERE.txt`, and enter the outer folder containing `tools/`. Install Python 3.10+. Either acquisition route needs its exact Node **24** for preparation and installation; host-only setup checks accept Node 22+.

## Prepare the host kit

The two SmileBASIC routes below require a fresh, verified held FS session for this boot. Leave SmileBASIC and its acquisition host open. Preparation does not acquire a new session. For the experimental Civilization VII entrypoint, use the separate [CivSwitch workflow](CIV7.md), including its explicit `--host-profile civ7-s8n` selection and exact source requirements.

**Direct ns2jb on Linux/WSL or macOS:** follow [Route A](INSTALLATION.md#route-a-linuxwsl-or-macos-with-ns2jb), including its pinned upstream checkout and `node run.cjs --rpc YOUR_PC_LAN_IPV4`. In a second terminal inside the extracted Moader kit, use the newly printed actual run folder and the same native POSIX Node 24:

```sh
python3 tools/install-from-rpc.py --prepare \
  --run-dir "/path/to/ns2jb/runs/<actual-run>" \
  --node "$(command -v node)"
```

Git is only needed to obtain the acquisition checkout. On WSL, both terminals must use Linux Node in the same distro; complete [LAN networking preparation](INSTALLATION.md#wsl2-networking-before-acquisition) first. No new Linux/WSL device validation is claimed by this documentation.

**mac2jb on macOS:** follow [Route B](INSTALLATION.md#route-b-macos-with-mac2jb). Copy its complete four-token status command, including its bundled Node 24, `rpc.cjs`, run folder and `status`. In macOS's default zsh:

```sh
read -r "MOADER_STATUS_COMMAND?Paste the full mac2jb status command: "
python3 tools/install-from-rpc.py --prepare --status-command "$MOADER_STATUS_COMMAND"
```

In bash, use `read -r -p 'Paste the full mac2jb status command: ' MOADER_STATUS_COMMAND` for the first line. Do not combine `--status-command` with `--run-dir` or `--node`; the status command supplies mac2jb's exact executable.

Preparation downloads the checksum-pinned official kaiHEN archive and pinned ns2jb source directly from upstream if missing. It verifies the engine tree, fills the required kaiHEN components while retaining the exact patched HEN/bootstrap pair, checks the kit, and creates a unique local installer snapshot. It creates an empty `vendor/kaihen-mo/programs` directory and does not extract OunceShell or other optional programs. Neither route executes an acquisition/status script. Preparation opens no console connection and prints an installation command that uses the selected Node executable and unique `--prepared` manifest. Review it, then run that exact command once for privileged installation while the original held session stays open. Do not run `hen.cjs` first; the combined installer owns HEN bootstrap. Follow [INSTALLATION.md](INSTALLATION.md) for the device workflow and [TROUBLESHOOTING.md](TROUBLESHOOTING.md) if anything stops.

The ns2jb engine is fetched into `research/sources/ns2jb/engine` on the user's machine. It is omitted from the public repository and kit because an explicit upstream redistribution license has not been identified. Moader does not copy ns2jb/mac2jb runs, credentials or captured device state into the kit.

To validate already prepared inputs without downloading, compiling or creating an installer snapshot:

```sh
python3 tools/setup-backend.py --check --node /absolute/path/to/node24
```

On a kit that still needs its upstream inputs, run `python3 tools/setup-backend.py --node /absolute/path/to/node24` first. The fetched official archive is kept locally at `research/artifacts/kaiHEN-20261114c.zip` for installer qualification; only the pinned engine is retained from the ns2jb source archive. No existing research checkout or copied private state is needed. Setup refuses to replace changed existing inputs. Use a fresh kit directory to change versions.

Existing optional files in `vendor/kaihen-mo/programs`, including Moader diagnostics if supplied there, are preserved and appear as `bundledPrograms` in check output. The installer includes them. A fresh minimal kit has no such programs; setup never adds OunceShell.

## Build from a clean source checkout

The bundled SDK setup currently targets **Apple Silicon macOS only**. It provisions devkitA64 GCC 16.1.0, switch-tools 1.13.1 and ounce-devkit 20261114. Linux and Intel macOS cross-builds are not configured by these scripts. Install Python 3.10+, Node 22+ and `make`; use Node 24 for the later live installation.

From a clean checkout of the release source:

```sh
python3 tools/setup-devkit.py
python3 tools/setup-backend.py --build --node node
python3 tools/setup-backend.py --check --node node
node tools/install-moader.cjs --check
```

The SDK bootstrap downloads the packages pinned in `tools/toolchain-packages.json` and the checksum-pinned ounce SDK. Backend setup downloads the pinned official kaiHEN archive and ns2jb source. It copies a source allowlist to a temporary directory, rebuilds HEN, bootstrap, plugin library and the application there, verifies HEN/bootstrap against the source-qualified component digests, then fills missing local inputs. Existing files must already match; there is no force-overwrite option. Use a separate clean checkout for a rebuild when release binaries already occupy `dist/` or `vendor/kaihen-mo`.

`setup-backend.py --check` is read-only. In contrast, `install-moader.cjs --check` writes a local immutable snapshot under `build/install/prepared-*` and updates its convenience pointer. Both avoid console connections. Use the unique manifest printed by the installer for any later `--prepared` command; `build/install/manifest.json` is a mutable convenience pointer.

HEN and bootstrap rebuilds must reproduce the pair recorded in `software-builds/kaihen-mo.json`. Its source-tree/patch digests bind those binaries to the reviewed source, and its console-validation flag is separate. The app and resident are compiled with debug information, so their hashes may vary with the build directory. A successful source build does not establish device behavior; see the release's validation scope.

## Exact external inputs

| Input | Pin |
| --- | --- |
| ns2jb source | `28f03b5895e5569d8fbc35cbed84906b8f0a65b2` |
| Extracted engine tree SHA-256 | `7003335a2a426a82156c19653be5e61506a05dd02b5d0490c8bcb4516c5c354a` |
| Experimental Civ7 source | `51a804cbce09216e1a42e65929644fd436b4c8bf` |
| Selected Civ7 source tree SHA-256 | `d83cadc568b65a724f32b848f1681540487a798b354ac3286f71e41e94a9ac75` |
| Official kaiHEN release | `20261114c` |
| Official kaiHEN ZIP SHA-256 | `63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967` |
| Patched HEN SHA-256 | `edaef968962a884514d81df8316d8fe225327c2b30e00211806afeb0ef989d86` |
| Matched bootstrap SHA-256 | `df8482f82e1e462206bb9e8128ec0ff2bb53636d8723aa3fdfaa978efd67b801` |
| ounce-devkit archive SHA-256 | `f24d5773b1ee1c2b19317f57e8ae54a60fcdcefe493a0e495297c241c4912f59` |

Source URLs are the [pinned ns2jb archive](https://gitlab.com/kurumuz/ns2jb/-/archive/28f03b5895e5569d8fbc35cbed84906b8f0a65b2/ns2jb-28f03b5895e5569d8fbc35cbed84906b8f0a65b2.tar.gz), [official kaiHEN release](https://github.com/ouncebrew/kaihen/releases/tag/20261114c) and [ounce-devkit release](https://github.com/ouncebrew/ounce-devkit/releases/tag/devkit-20261114). The engine tree digest hashes sorted relative POSIX paths, each followed by NUL, its lowercase file SHA-256, and newline. The engine currently contains 116 files totaling 542,813 bytes. The helper accepts only that content; it never falls back to another revision.

The experimental Civ7 profile fetches the [pinned s8n archive](https://github.com/s8n/civ7-fs-kaihen/archive/51a804cbce09216e1a42e65929644fd436b4c8bf.tar.gz) instead of the kurumuz engine. Its 120 selected files total 720,866 bytes and include `engine/`, `targets/civ7/` and `bridge/civ_bridge.js`. [config/civ7-provider.json](../config/civ7-provider.json) records each source file and its materialized run counterpart. Its tree digest uses the same format. Source identity and host checks do not establish a Civ7 console installation.

For offline preparation, transfer these exact upstream inputs yourself and pass `--offline --engine-archive PATH --kaihen-archive PATH`. A source checkout additionally needs `--build` with its installed SDK, or `--hen-binary PATH` plus `--programs-archive PATH --programs-sha256 SHA256`. The programs ZIP must contain `switch/Moader.nro` and `kaihen/plugins/moader.nso`. The minimal host kit already supplies the patched HEN and Moader binaries; provide both upstream archives for its first offline preparation.

## Host tests and runtime assets

For control-protocol tests or CI that need only the pinned external engine, use:

```sh
python3 tools/setup-backend.py --sources-only
```

This fetches and validates only `research/sources/ns2jb/engine`. It needs no Node, SDK, HEN, release proof, app binaries or prepared snapshots, and does not create those inputs. An existing engine must match the pinned tree; repeat runs validate it without downloads or writes. The source stays in the ignored local research path and is not added to the public release.

For the same source-only operation offline:

```sh
python3 tools/setup-backend.py --sources-only --offline --engine-archive /absolute/path/to/pinned-ns2jb.tar.gz
```

This mode accepts only `--root`, `--host-profile`, `--offline` and `--engine-archive` alongside `--sources-only`. It rejects `--check`, `--build`, `--node`, release manifests and HEN/app archive options before work. It performs no console or installer operations. Add `--host-profile civ7-s8n` to select the Civ7 source closure; an offline archive must match that selected profile.

Run setup/wrapper tests without an SDK, network or device:

```sh
python3 tools/test-setup-backend.py
node tools/test-installers.cjs
```

The main Makefile includes the SDK CPU fragment at parse time; `make check` therefore requires an installed SDK. SDK-free CI should invoke individual compatible test scripts directly. `tools/test-core.py`, `tools/test-library-refresh.py`, `tools/test-native-ui.py` and `tools/test-web.py` require `clang++`, zlib headers/libraries and AddressSanitizer/UBSan; the web-worker test also needs the SDK headers. Do not label the entire native suite SDK-free.

The portable runtime templates and `zbic.wasm` are shipped in `runtime/modloader/native/prebuilt`. Their manifest verifies both source hashes and generated files. End users need Node, not Zig. Maintainers rebuilding these assets use Zig **0.15.2** and `node runtime/modloader/tools/build.cjs`; see [MAINTAINING.md](MAINTAINING.md) before changing them.
