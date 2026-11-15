# Contributing to Moader

Start with the [README](README.md) and open a [bug, compatibility report or feature request](https://github.com/H644b/Moader/issues/new/choose). For questions and early ideas, use [Discussions](https://github.com/H644b/Moader/discussions). Small fixes can go straight to a pull request; discuss larger runtime or installation changes first.

Moader is a prerelease Switch 2 mod manager. The current release is **v0.1.0-dev.1** (the application displays `0.1.0-dev`). The native app, browser and privileged filesystem runtime have different lifetimes and responsibilities. Keep requested settings, accepted native routing and visible game behavior distinct in reports and changes.

## Make a change

1. Fork the repository and create a branch from `main`.
2. Keep the change focused. Explain the problem and the resulting behavior; include a small reproduction when possible.
3. Run the relevant host checks below. A console is not required for ordinary contributions. Say which checks you ran and which hardware behavior remains untested.
4. Open a pull request and complete its template. Preserve license notices and identify the source and license of new third-party material.

Use synthetic fixtures for tests. Do not add game assets, firmware, keys, account exports, pairing codes, private addresses, device captures or raw session logs. Share only a redacted excerpt needed to explain a result. Follow [SECURITY.md](SECURITY.md) for vulnerabilities and [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) when participating.

## Host checks

CI uses Ubuntu, Clang/C++, Python, Node **24** and system zlib. The native build Makefile also includes a locally installed devkit, so `make check` is not the portable CI entry point.

From a clean public checkout, install Clang and zlib development headers using your system package manager. Then run:

```sh
python3 .github/scripts/prepare-host-deps.py --sdk --installer
python3 tools/setup-backend.py --sources-only
python3 tools/embed-web.py
node -e "require('./runtime/modloader/lib/native/artifacts.cjs').artifacts()"
python3 tools/test-core.py
python3 tools/test-web.py
python3 tools/test-runtime-logs.py
python3 tools/test-metadata-names.py
python3 tools/test-game-names.py
python3 tools/test-web-worker.py
python3 tools/test-resident.py
python3 tools/test-setup-backend.py
node tools/test-installers.cjs
node tools/test-control-resident.cjs
node tools/test-inspect-hen.cjs
```

The dependency helper downloads checksum-pinned public SDK/release inputs for host declarations and installer qualification. It does not install hooks, contact a console or execute the downloaded release binaries. Its generated `vendor/` and `research/artifacts/` inputs are local dependencies, not files to commit. The control-protocol tests also need the pinned local engine source; use `python3 tools/setup-backend.py --sources-only` to fetch it into the ignored local source tree. Host HTTP tests use loopback; `test-web.py` also runs the pairing/FTP publication regressions with mocked FTP, and installer tests mock device transports.

The filesystem log test also exercises the actual native C catalogue reader and C++ publisher, including live view changes and ownership. Its SDK calls are host mocks. Passing host tests does not establish console compatibility. For native runtime changes, rebuild the checked prebuilt components using the project's native build instructions and run their source-hash verification; do not bypass a mismatch. Updating privileged hooks requires a fresh qualified installation in the current boot. Do not replay stale handles or an operation with uncertain completion.

Before publication, scan the **clean export or checkout**, before generating local dependencies:

```sh
python3 tools/check-public-tree.py /path/to/public-checkout
python3 .github/tests/test_public_tree.py
```

The scanner flags likely private material and reports filenames/reasons only. It complements a manual review; it cannot prove that every file is safe to publish. Keep third-party source and license notices intact.

## Describe device observations precisely

Include release version, console/firmware and kaiHEN versions if known, mod layout, actions taken, native result/generation and the observed game effect. State whether content was reopened, the game was closed or the console rebooted. Leave unknown values unknown. A screenshot should show only the relevant UI and have pairing/network/account details removed.

The bounded MK8 course-preview test passed enable, disable, re-enable and recoverable removal while the game stayed running. Its preview required a race → quit to main menu → reopen course selection; returning only to character selection did not refresh it. This does not establish universal cache invalidation or compatibility with arbitrary assets. The privileged runtime is current-boot work and does not survive a console reboot. Forced crashes and crash recovery have not been hardware-tested.

No original-content hash or one-open-file O→M→O hash experiment is required to submit a useful compatibility report. Do not acquire or redistribute original game data for an issue. Record visible behavior and the native messages actually available.

By submitting a contribution, you agree that it may be distributed under the repository's applicable license, including GPL-2.0-only for Moader source. Third-party components retain their own notices and exceptions. No copyright assignment is required.
