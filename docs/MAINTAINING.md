# Maintaining the release inputs

Package a clean, committed source tree with the exact binaries recorded in `config/release.json`. Keep research, device logs, SDKs, captured policies and installer snapshots in the local checkout. Include the corresponding source, patches and license notices.

## Release layout

**v3.5.0** adds the active community Store to the existing Seedsow standalone backend kit. The v3.0.0 release introduced the source-qualified standalone implementation and prebuilt bootstrap. Use `sourceCommit` in the v3.5.0 [release manifest](https://github.com/H644b/Moader/releases/download/v3.5.0/release-manifest.json) to identify their exact source. The protected v0.2.0 tag predates the installed-icon asset refresh; its GitHub source archive is the original v0.2.0 source. The v0.1.3 tag also predates its final refreshed assets; that older release manifest identifies their later source commit.

| Artifact | Contents |
| --- | --- |
| Public source | Audited source allowlist, tests, documentation, license notices and portable native prebuilt assets |
| `Moader-v3.5.0.zip` | SD layout: `switch/Moader.nro`, `kaihen/plugins/moader.nso`, release docs/licenses/manifest |
| `Moader-backend-v3.5.0.zip` | Public host source, the matched Moader/kaiHEN binaries, pinned official kaiHEN archive, complete regular bundle, and `prebuilt/standalone-bootstrap/{bootstrap.bin,proof.json}`; outer folder includes `START_HERE.txt` |
| `Moader.nro`, `moader.nso` | Standalone copies of the matched app and resident |
| `SHA256SUMS.txt` | Checksums of the packaged release assets |

**Omit the ns2jb, Civ7 and Seedsow upstream source dependencies from public source and host kits unless explicit redistribution terms are located and reviewed.** The setup helper fetches the exact upstream revision directly for each user. The default profile selects the kurumuz engine; `civ7-s8n` selects its engine, Civ7 target tables and acquisition bridge, as pinned by [config/civ7-provider.json](../config/civ7-provider.json). `seedsow-malev0` fetches its reviewed source closure; the GUI helper obtains the pinned upstream GUI checkout. The user-authorized kaiHEN release binaries are included with the upstream archive's license files and the patched kaiHEN source and patch already in this repository. Preserve upstream attribution separately from license claims.

The host kit contains `research/artifacts/kaiHEN-20261114c.zip` and the full patched kaiHEN bundle. First-run setup validates that pinned base and extracts `game/main`, `hbmenu.nro` and `programs/ounceshell.nro`, while retaining the kit's patched `ro/main`, `hen/main` and regular `bootstrap.bin`. The separate standalone bootstrap and receipt are source-built and hash-pinned. Include the complete corresponding patched HEN source, patch and license notices in the public source and kit. Do not include private FS captures or derived payloads, complete research source trees, `.git`, session files or runtime state. Keep all other third-party notices required by the selected source/assets.

`tools/setup-backend.py` hard-pins the official archive and engine tree, and verifies the reviewed RO/HEN/bootstrap proof. An optional `release/backend-inputs.json` may provide checksum-qualified app ZIP and raw patched component download URLs for provisioning a source checkout without compiling:

```json
{
  "schema": 1,
  "programs": {
    "url": "https://github.com/H644b/Moader/releases/download/v3.5.0/Moader-v3.5.0.zip",
    "sha256": "REPLACE_WITH_ACTUAL_RELEASE_ZIP_SHA256"
  },
  "patched_hen": {
    "url": "REPLACE_WITH_EXPLICIT_HTTPS_RAW_HEN_ASSET_URL"
  },
  "patched_bootstrap": {
    "url": "REPLACE_WITH_EXPLICIT_HTTPS_RAW_BOOTSTRAP_ASSET_URL"
  },
  "patched_ro": {
    "url": "REPLACE_WITH_EXPLICIT_HTTPS_RAW_RO_ASSET_URL"
  }
}
```

This descriptor is optional for the populated host kit and for `--build`. Do not publish placeholders as a working manifest. Component URLs cannot override the digests in the reviewed proof. Update code, source provenance and docs together when intentionally qualifying another release; never weaken a hash check to accommodate unexplained differences.

## Package the committed public source

The descriptor records the tag, exact binary sizes and SHA-256 digests. Set hardware-validation flags only for checks that passed on the exact components named by the descriptor. Build paths can change the app or resident hashes. Keep the checked source and output digests connected when they change.

For a matching release checkout, audit and commit the source before packaging:

```sh
python3 tools/package-release.py
```

The packager reads the source from `HEAD`, verifies the component proof and checks the exported public tree. It writes both ZIPs, the standalone programs, `release-manifest.json` and `SHA256SUMS.txt` under `dist/releases/`. Use an empty output directory. Review the final bytes before release and record the source commit with the downloads. When refreshing assets on an existing tag, retain the tag and disclose that the ZIP source commit differs from GitHub's automatic source archives.

## Native changes

The supported SDK bootstrap covers Apple Silicon macOS and Linux x86_64 with the pins documented in [BUILDING.md](BUILDING.md). Build in a fresh checkout with the method and profile for the source revision you selected. The backend components must reproduce the source-qualified proof before setup accepts them.

Changes to `runtime/modloader/native` or a source listed in its prebuilt manifest require Zig 0.15.2 and a maintainer rebuild:

```sh
node runtime/modloader/tools/build.cjs
node -e 'require("./runtime/modloader/lib/native/artifacts.cjs").artifacts(); console.log("Native assets verified")'
```

Run this in the intended build checkout: it writes `runtime/modloader/state/build-release` and `runtime/modloader/native/prebuilt`. Review the generated manifest and retain the source/hash gate. Do not require end users to compile these templates or fetch Zig.

When changing runtime log layout, keep `include/moader/runtime_log_wire.h`, the native runtime and installer metadata contract aligned. The current preparation contract is a 64-byte catalogue, 64-byte acknowledgement and two 65,616-byte zero-filled log snapshots. Installer preflight enforces this before native work. Existing prepared snapshots remain versioned inputs; create a new snapshot after integration.

## Verify the exported artifacts

The in-review Moader Setup source adds a local controller, a dedicated Electron desktop window, graphical launchers, pinned Node 24 executables for macOS/Linux arm64/x86_64, and uv for kit-scoped Python. `tools/package-release.py` verifies the official Node, uv and Electron runtime archives and includes their license notices. It lists the desktop app in `START_HERE.txt`. Keep `setup/desktop-app/`, `setup/host-policy.cjs`, `setup/host-tools.cjs`, `setup/launch.sh`, `setup/windows-launch.ps1`, `setup/wsl-entry.sh`, the `Moader Setup` launchers, `setup/licenses/` and `docs/SETUP_GUI.md` in public source. Do not commit `host-runtimes/` binaries to Git; the packager inserts them into the backend ZIP. A public macOS release still needs project signing and notarization. Test the extracted ZIP by opening its app from a moved folder, including a path with spaces or non-ASCII characters, and by inspecting its support export. Preserve the CLI fallback and the existing native proof gates.

Run `node --test setup/test-controller.cjs` against matched kit inputs for host-only token, restart, uncertainty, SD-export, hash and redaction checks. Use `tools/test-ns2jb-host.py --online --node /absolute/path/to/node24` for the existing source-qualified SmileBASIC path. Keep host-only and actual-console results in separate validation headings.

1. Run `python3 tools/test-setup-backend.py` and the relevant host tests. Native tests that use SDK headers need the pinned SDK; `make check` itself is not SDK-free.
2. Inspect the public export and both ZIP inventories for forbidden private paths and missing license/source files. Scan text for personal paths, accounts, credentials and captured session identifiers. Keep release checksums for the final bytes.
3. Unpack the host kit into an isolated directory containing spaces. With no copied research directory beyond the kit's pinned official kaiHEN archive and patched RO/HEN/bootstrap set, run `python3 tools/setup-backend.py --node /absolute/path/to/node24`. Confirm it fetches the pinned ns2jb source, the reported tree/hash matches [BUILDING.md](BUILDING.md), and the native set matches the reviewed proof. For the Seedsow profile, confirm the complete included bundle supplies the game loader, hbmenu and OunceShell.
4. Run `python3 tools/setup-backend.py --check --node /absolute/path/to/node24` twice. It must not download, compile or change files. Run the local installer `--check` with that same Node executable and confirm it prints a unique `build/install/prepared-*/manifest.json`, validates both programs and performs no console connection.
5. Exercise the direct ns2jb wrapper with `--run-dir PATH --node node` and separately the existing mac2jb status-command route. Both must preserve the selected Node 24, execute local Moader scripts only, create a unique local snapshot and print a matching `--prepared` install command. The wrapper never performs installation itself. `python3 tools/test-ns2jb-host.py --online --node node` checks real precompiled-kit preparation with public inputs and transport calls blocked; it does not acquire FS or install on a console.
6. Exercise `--host-profile civ7-s8n` separately: `python3 tools/test-civ7-host.py --online --node node` checks the upstream source/materialization and native ABI; `node tools/test-civ7-host.cjs` checks synthetic held sessions against the actual provider; `python3 tools/test-ns2jb-host.py --online --node node --host-profile civ7-s8n` checks real precompiled-kit preparation with endpoint/process calls blocked. These establish host behavior only.
7. Exercise the Seedsow kit separately: run `setup-backend.py --host-profile seedsow-malev0` using the pinned upstream archives, then its read-only `--check`, and provision/check the GUI from the reviewed upstream revision. These are host checks; do not acquire FS or start HEN in a packaging test.
8. Record source-build, export checks, published download checks and device acceptance separately. A passed installer `--check` establishes local input qualification; it does not demonstrate hardware installation or game behavior. Publish only the validation scope actually observed.

Use [INSTALLATION.md](INSTALLATION.md), [USAGE.md](USAGE.md) and [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for user workflows. After any transport outcome becomes uncertain, inspect the run's installation journal and current state before another native operation; do not automate retries.


## Update the official GitHub Wiki

The [GitHub Wiki](https://github.com/H644b/Moader/wiki) uses the same guides as this repository. Edit the canonical Markdown under `docs/` first, then regenerate the linked wiki pages:

```sh
python3 tools/build-wiki.py
```

This writes the linked Markdown guides under `build/wiki/`, including Home, navigation and FAQ. The command reports the current page count. It rewrites repository-relative links for the wiki and keeps interface images in the main public repository. It never publishes by itself. CI checks generation alongside the public source guard.

For a repository maintainer, clone the official wiki, copy the generated pages, review the diff, then commit and push:

```sh
git clone https://github.com/H644b/Moader.wiki.git build/wiki-repo
cp build/wiki/*.md build/wiki-repo/
git -C build/wiki-repo diff
git -C build/wiki-repo add '*.md'
git -C build/wiki-repo commit -m "Update Moader usage guides"
git -C build/wiki-repo push
```

GitHub initializes a new wiki's Git repository only after its first page is created in the website editor. For a new wiki, enable it and create Home on GitHub before cloning it. Wiki edits are maintained separately from application tags; keep the current setup guide and release links aligned.
