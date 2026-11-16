# Maintaining the release inputs

Release preparation must preserve the link between public source, qualified HEN/bootstrap pair, portable runtime manifest, host kit and SD app/plugin ZIP. Use a clean staging directory. Never package the working checkout wholesale: `research/`, `state/`, `evidence/`, mac2jb run folders, captured policies, local journals, developer SDKs and prepared installer snapshots can contain machine or device state.

The public source includes Moader source, tests, scripts, documentation, licensing, `runtime/kaihen` source and the shipped portable runtime assets. Keep the sanitized `software-builds/kaihen-mo.json` and corresponding `.patch` as the public HEN provenance record. Preserve the base revision, release base, patch reference, toolchain and HEN/bootstrap digests with their validation scope; do not publish private evidence paths or session identifiers.

## Release layout

The current release tag is `v0.1.1`. PR #7 has been merged, with the unresolved zero-reload `2011-0102` lifecycle failure explicitly recorded. Publication is not hardware acceptance: keep `console_retest_passed` and `appAndResidentHardwareTested` false unless the exact claimed checks pass. GitHub's `releases/latest` endpoint can skip prereleases, so use explicit tag download links.

| Artifact | Contents |
| --- | --- |
| Public source | Audited source allowlist, tests, documentation, license notices and portable native prebuilt assets |
| `Moader-v0.1.1.zip` | SD layout: `switch/Moader.nro`, `kaihen/plugins/moader.nso`, release docs/licenses/manifest |
| `Moader-backend-v0.1.1.zip` | Public host source plus exactly four binaries: `dist/Moader.nro`, `dist/moader.nso`, `vendor/kaihen-mo/hen/main` and `vendor/kaihen-mo/bootstrap.bin`; outer `Moader-backend-v0.1.1` folder includes `START_HERE.txt` |
| `Moader.nro`, `moader.nso` | Standalone copies of the matched app and resident |
| `SHA256SUMS.txt` | Checksums of the packaged release assets |

**Omit the ns2jb and Civ7 source dependencies from public source and host kits unless explicit redistribution terms are located and reviewed.** The setup helper fetches the exact upstream revision directly for each user. The default profile selects the kurumuz engine; `civ7-s8n` selects its engine, Civ7 target tables and acquisition bridge, as pinned by [config/civ7-provider.json](../config/civ7-provider.json). Do not invent a license or infer redistribution permission from a public repository. Preserve upstream attribution separately from license claims.

The minimal host kit has no `research/` content and does not bundle the full official kaiHEN ZIP, other upstream binaries or OunceShell. First-run setup fetches the pinned archive and extracts only `ro/main`, `game/main` and `hbmenu.nro`, while retaining the kit's patched `hen/main` and `bootstrap.bin`. It creates `vendor/kaihen-mo/programs` empty. Include the complete corresponding patched HEN source, patch and license notices in the public source and kit. Do not include private captures, complete research source trees, `.git`, session files or runtime state. Keep all other third-party notices required by the selected source/assets.

`tools/setup-backend.py` hard-pins the official archive and engine tree, and verifies the reviewed HEN/bootstrap proof. An optional `release/backend-inputs.json` may provide checksum-qualified app ZIP and raw patched HEN/bootstrap download URLs for provisioning a source checkout without compiling:

```json
{
  "schema": 1,
  "programs": {
    "url": "https://github.com/H644b/Moader/releases/download/v0.1.1/Moader-v0.1.1.zip",
    "sha256": "REPLACE_WITH_ACTUAL_RELEASE_ZIP_SHA256"
  },
  "patched_hen": {
    "url": "REPLACE_WITH_EXPLICIT_HTTPS_RAW_HEN_ASSET_URL"
  },
  "patched_bootstrap": {
    "url": "REPLACE_WITH_EXPLICIT_HTTPS_RAW_BOOTSTRAP_ASSET_URL"
  }
}
```

This descriptor is optional for the populated host kit and for `--build`. Do not publish placeholders as a working manifest. Component URLs cannot override the digests in the reviewed proof. Update code, source provenance and docs together when intentionally qualifying another release; never weaken a hash check to accommodate unexplained differences.

## Package the committed public source

`config/release.json` records the version/tag, exact app/resident/HEN/bootstrap sizes and SHA-256 digests, and the validation document. Its `appAndResidentHardwareTested` flag records whether the exact app/resident pair has console validation. The v0.1.1 app/resident are rebuilt with matching version labels; the HEN/bootstrap pair carries the physical-card/reload integration and matches the startup-tested candidate. Read the build proof and validation document for exact host/device scope; do not inherit the old HEN's console-tested claim. A fresh source rebuild can have different app/resident hashes because of debug paths. Record an intentional build and its validation boundary before updating pins; do not replace them merely to make packaging pass.

Finish the intended build and checks, audit the public source, and commit all release source/documentation/config changes before packaging:

```sh
python3 tools/package-release.py
```

The release packager exports committed public source from `HEAD`, adds only the four exact binaries above, and uses `tools/package-homebrew.py` for the SD ZIP. Uncommitted source changes are not included. Outputs go to `dist/releases/`: `Moader-v0.1.1.zip`, `Moader-backend-v0.1.1.zip`, `Moader.nro`, `moader.nso`, and `SHA256SUMS.txt`, with a manifest if emitted by the packager. Inspect and test these final exported bytes before uploading them. Keep the source commit used for packaging in the release record.

## Native changes

The supported SDK bootstrap is Apple Silicon macOS with the pins documented in [BUILDING.md](BUILDING.md). Use a fresh checkout and `python3 tools/setup-backend.py --build` for isolated HEN/bootstrap/app compilation. The HEN/bootstrap pair must reproduce the source-qualified proof before provisioning. The app's debug information can encode its build directory, so distinguish successful builds from byte reproducibility.

Changes to `runtime/modloader/native` or a source listed in its prebuilt manifest require Zig 0.15.2 and a maintainer rebuild:

```sh
node runtime/modloader/tools/build.cjs
node -e 'require("./runtime/modloader/lib/native/artifacts.cjs").artifacts(); console.log("Native assets verified")'
```

Run this in the intended build checkout: it writes `runtime/modloader/state/build-release` and `runtime/modloader/native/prebuilt`. Review the generated manifest and retain the source/hash gate. Do not require end users to compile these templates or fetch Zig.

When changing runtime log layout, keep `include/moader/runtime_log_wire.h`, the native runtime and installer metadata contract aligned. The current preparation contract is a 64-byte catalogue, 64-byte acknowledgement and two 65,616-byte zero-filled log snapshots. Installer preflight enforces this before native work. Existing prepared snapshots remain versioned inputs; create a new snapshot after integration.

## Verify the exported artifacts

1. Run `python3 tools/test-setup-backend.py` and the relevant host tests. Native tests that use SDK headers need the pinned SDK; `make check` itself is not SDK-free.
2. Inspect the public export and both ZIP inventories for forbidden private paths and missing license/source files. Scan text for personal paths, accounts, credentials and captured session identifiers. Keep release checksums for the final bytes.
3. Unpack the host kit into an isolated directory containing spaces. With no copied research directory or upstream binaries except the patched HEN, run `python3 tools/setup-backend.py --node /absolute/path/to/node24`. Confirm it fetches the pinned official kaiHEN and ns2jb archives, the reported tree/hash matches [BUILDING.md](BUILDING.md), the patched HEN/bootstrap pair matches the reviewed proof, and `vendor/kaihen-mo/programs` is empty. It must not extract OunceShell.
4. Run `python3 tools/setup-backend.py --check --node /absolute/path/to/node24` twice. It must not download, compile or change files. Run the local installer `--check` with that same Node executable and confirm it prints a unique `build/install/prepared-*/manifest.json`, validates both programs and performs no console connection.
5. Exercise the direct ns2jb wrapper with `--run-dir PATH --node node` and separately the existing mac2jb status-command route. Both must preserve the selected Node 24, execute local Moader scripts only, create a unique local snapshot and print a matching `--prepared` install command. The wrapper never performs installation itself. `python3 tools/test-ns2jb-host.py --online --node node` checks real precompiled-kit preparation with public inputs and transport calls blocked; it does not acquire FS or install on a console.
6. Exercise `--host-profile civ7-s8n` separately: `python3 tools/test-civ7-host.py --online --node node` checks the upstream source/materialization and native ABI; `node tools/test-civ7-host.cjs` checks synthetic held sessions against the actual provider; `python3 tools/test-ns2jb-host.py --online --node node --host-profile civ7-s8n` checks real precompiled-kit preparation with endpoint/process calls blocked. These establish host behavior only.
7. Record source-build, export checks, published download checks and device acceptance separately. A passed installer `--check` establishes local input qualification; it does not demonstrate hardware installation or game behavior. Publish only the validation scope actually observed.

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
