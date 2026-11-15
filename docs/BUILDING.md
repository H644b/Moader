# Building and preparing the backend

For installation, use the [v0.1.0-dev release](https://github.com/H644b/Moader/releases/tag/v0.1.0-dev) and its `Moader-backend-v0.1.0-dev.zip` host kit. The kit supplies the app, resident, patched HEN, corresponding source and portable native runtime assets. Preparation downloads the other required upstream components; no compiler is needed. The separate `Moader-v0.1.0-dev.zip` contains the SD app/plugin files; it is not a complete host kit.

Unzip the host kit, open its `START_HERE.txt`, and enter the outer `Moader-backend-v0.1.0-dev` folder. The commands below run from that directory containing `tools/`. Python 3.10+ is required. Reuse the exact Node 24 executable supplied by mac2jb for installation; a different Node executable can behave differently even when both are installed on the same Mac. Host-only setup checks accept Node 22+.

## Prepare the host kit

Obtain a fresh, held mac2jb session using mac2jb's own instructions. Copy its complete status command, which has four tokens: Node executable, `rpc.cjs`, run folder, and `status`. Do not run a new acquisition through Moader.

Run this and paste that complete status command at the prompt:

```sh
read -r "MOADER_STATUS_COMMAND?Paste the full mac2jb status command: "
python3 tools/install-from-rpc.py --prepare --status-command "$MOADER_STATUS_COMMAND"
```

The prompt syntax above is for macOS's default zsh. In bash, use `read -r -p 'Paste the full mac2jb status command: ' MOADER_STATUS_COMMAND` instead.

Preparation downloads the checksum-pinned official kaiHEN archive and pinned ns2jb source directly from upstream if missing. It verifies the engine tree, fills the required kaiHEN components while retaining the exact patched HEN, checks the kit, and creates a unique local installer snapshot. It creates an empty `vendor/kaihen-mo/programs` directory and does not extract OunceShell or other optional programs. The pasted `rpc.cjs` is never executed. Preparation opens no console connection and prints an installation command that uses the same Node executable and the unique `--prepared` manifest. Review it and run it once when ready to install. Keep the mac2jb session open. Follow [INSTALLATION.md](INSTALLATION.md) for the device workflow and [TROUBLESHOOTING.md](TROUBLESHOOTING.md) if anything stops.

The ns2jb engine is fetched into `research/sources/ns2jb/engine` on the user's machine. It is omitted from the public repository and kit because an explicit upstream redistribution license has not been identified. Moader does not copy mac2jb runs, credentials or captured device state into the kit.

To validate already prepared inputs without downloading, compiling or creating an installer snapshot:

```sh
python3 tools/setup-backend.py --check --node /absolute/path/to/mac2jb/node
```

On a kit that still needs its upstream inputs, run `python3 tools/setup-backend.py --node /absolute/path/to/mac2jb/node` first. The fetched official archive is kept locally at `research/artifacts/kaiHEN-20261114c.zip` for installer qualification; only the pinned engine is retained from the ns2jb source archive. No existing research checkout or copied private state is needed. Setup refuses to replace changed existing inputs. Use a fresh kit directory to change versions.

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

The SDK bootstrap downloads the packages pinned in `tools/toolchain-packages.json` and the checksum-pinned ounce SDK. Backend setup downloads the pinned official kaiHEN archive and ns2jb source. It copies a source allowlist to a temporary directory, rebuilds HEN and the application there, verifies HEN against the tested digest, then fills missing local inputs. Existing files must already match; there is no force-overwrite option. Use a separate clean checkout for a rebuild when release binaries already occupy `dist/` or `vendor/kaihen-mo`.

`setup-backend.py --check` is read-only. In contrast, `install-moader.cjs --check` writes a local immutable snapshot under `build/install/prepared-*` and updates its convenience pointer. Both avoid console connections. Use the unique manifest printed by the installer for any later `--prepared` command; `build/install/manifest.json` is a mutable convenience pointer.

The HEN rebuild is required to be byte-identical to the tested HEN. The app and resident are compiled with debug information, so their hashes may vary with the build directory. A successful source build does not establish device behavior; see the release's validation scope.

## Exact external inputs

| Input | Pin |
| --- | --- |
| ns2jb source | `28f03b5895e5569d8fbc35cbed84906b8f0a65b2` |
| Extracted engine tree SHA-256 | `7003335a2a426a82156c19653be5e61506a05dd02b5d0490c8bcb4516c5c354a` |
| Official kaiHEN release | `20261114c` |
| Official kaiHEN ZIP SHA-256 | `63b91e5e7c6f411aad758758e5a1a3cbe1f855b4304d87916afe5319b9dfd967` |
| Patched HEN SHA-256 | `e6341b1932b450e25f04ccf425c3034bd45dd3cccfadbf5e5fb2778b6a673e5c` |
| ounce-devkit archive SHA-256 | `f24d5773b1ee1c2b19317f57e8ae54a60fcdcefe493a0e495297c241c4912f59` |

Source URLs are the [pinned ns2jb archive](https://gitlab.com/kurumuz/ns2jb/-/archive/28f03b5895e5569d8fbc35cbed84906b8f0a65b2/ns2jb-28f03b5895e5569d8fbc35cbed84906b8f0a65b2.tar.gz), [official kaiHEN release](https://github.com/ouncebrew/kaihen/releases/tag/20261114c) and [ounce-devkit release](https://github.com/ouncebrew/ounce-devkit/releases/tag/devkit-20261114). The engine tree digest hashes sorted relative POSIX paths, each followed by NUL, its lowercase file SHA-256, and newline. The engine currently contains 116 files totaling 542,813 bytes. The helper accepts only that content; it never falls back to another revision.

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

This mode accepts only `--root`, `--offline` and `--engine-archive` alongside `--sources-only`. It rejects `--check`, `--build`, `--node`, release manifests and HEN/app archive options before work. It performs no console or installer operations.

Run setup/wrapper tests without an SDK, network or device:

```sh
python3 tools/test-setup-backend.py
node tools/test-installers.cjs
```

The main Makefile includes the SDK CPU fragment at parse time; `make check` therefore requires an installed SDK. SDK-free CI should invoke individual compatible test scripts directly. `tools/test-core.py` and `tools/test-web.py` require `clang++`, zlib headers/libraries and AddressSanitizer/UBSan; the web-worker test also needs the SDK headers. Do not label the entire native suite SDK-free.

The portable runtime templates and `zbic.wasm` are shipped in `runtime/modloader/native/prebuilt`. Their manifest verifies both source hashes and generated files. End users need Node, not Zig. Maintainers rebuilding these assets use Zig **0.15.2** and `node runtime/modloader/tools/build.cjs`; see [MAINTAINING.md](MAINTAINING.md) before changing them.
