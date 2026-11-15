# CivSwitch as a Moader entrypoint

Research snapshot: **15 November 2026**. Civilization VII already has upstream filesystem (FS) access on Switch 2. Moader includes a host-tested experimental integration for the single `civ7-s8n` source profile. **Moader has not been hardware-tested through CivSwitch**. This page concerns Civilization VII as the entrypoint for the backend, not whether a mod for Civilization VII itself works.

## What is available?

| Layer | Evidence at the reviewed revision | Moader verdict |
| --- | --- | --- |
| CivSwitch JavaScript loader | Gezine documents the chat/save entrypoint and a console listener on TCP9026 | Entry state only; no FS-ready claim |
| Civilization VII FS access | HerXayah publishes an FS engine; current mac2jb reports a Switch reaching 4/4 with held RPC | Exists upstream; a live session still needs target-specific qualification |
| kaiHEN through Civilization VII | s8n publishes a Civ7 kaiHEN launcher and reports success on firmware 23.0.1 | Demonstrates an upstream HEN route; does not install Moader's matched backend |
| Passing that held session to Moader | Profile-aware preparation and installer adapters pass host tests; console handoff is unverified | Only the exact s8n pin below is eligible for qualification |
| Moader resident service, routing and browser through CivSwitch | No Moader console test performed | Hardware untested |

Sources: [Gezine loader](https://github.com/Gezine/CivSwitch/blob/f6b0f2bd285e7b32c0a1968181046f18edd79708/README.md), [HerXayah FS port](https://github.com/HerXayah/civ7-fs/blob/5e5665b92661f205d853ea4351a9edace7c9c6b5/README.md), [mac2jb FS path](https://github.com/cpypwn/mac2jb/blob/717022b8a91a713be41a02b5e131d3229dd8b00a/README.md#fs-jailbreak), [s8n HEN route](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/README.md).

## Recognize the three different states

1. **Awaiting payload:** CivSwitch's JavaScript loader is waiting on the console's TCP9026. This does not mean FS memory was acquired or HEN started.
2. **Verified held FS session:** the FS engine completed qualification and keeps its endpoint available. s8n's runner checks verified/restored FS state, retained-endpoint readiness and deferred cleanup before publishing its held result. Keep the game and acquisition host open. [Runner checks](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/engine/run.cjs#L324-L342).
3. **HEN running:** the separate bootstrap has actually started HEN. The upstream HEN launcher checks a held RO frontend first, then stages and starts HEN. That frontend or an earlier 4/4 message alone is not completed HEN installation. [Launcher](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/engine/hen.cjs).

Current mac2jb distinguishes **Userland** from **FS jailbreak**. Its README reports hardware 4/4 verification for the FS path; the ordinary JavaScript loader does not provide that session. This supersedes older descriptions of mac2jb's CivSwitch support as loader-only. [Reviewed mac2jb README](https://github.com/cpypwn/mac2jb/blob/717022b8a91a713be41a02b5e131d3229dd8b00a/README.md#jailbreak-with-civswitch).

## Requirements and version boundaries

The entrypoint requires the **physical Switch 1 edition Civilization VII cartridge, game version 1.0.0**. Gezine reports tests on Switch 1 firmware 22.5.0 and Switch 2 firmware 20.4.0/23.0.1; its broader firmware expectation is not a Moader test. The Switch 2 edition, digital edition and updated game builds are not qualified by that requirement. [CivSwitch requirements](https://github.com/Gezine/CivSwitch/blob/f6b0f2bd285e7b32c0a1968181046f18edd79708/README.md).

The HEN fork has a narrower limit: despite a “≤23.0.1” heading, its README says **only 23.0.1 was tested and lower firmware currently does not work**. It targets kaiHEN **20261114c**, needs an inserted SD card and a same-network host, and reports Node24 testing with Linux/macOS preferred. It also notes acquisition timing depends on the exFAT update. These statements do not establish support for another system revision or update combination. [HEN prerequisites](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/README.md#what-you-need).

The FS target contains fixed SDK/layout values, including an expected live SDK CRC. Treat an SDK/layout mismatch as unsupported; do not remove the check to accommodate another build. [Target profile](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/targets/civ7/target.json), [connection validation](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/engine/lib/connection.cjs).

Upstream warns that receiving a blacklist update can prevent Civilization VII from launching without a reboot. Follow the linked upstream network/update guidance before proceeding; this is an upstream warning, not a Moader-verified description of every firmware or a guarantee against updates, blocking or account action. Existing save-trigger setup and recovery can replace game saves; this page supplies no save-deletion or restoration command. [Upstream caveats](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/README.md#booting-kaihen).

## HEN changes and Civ7 entrypoint adaptations

Moader's [kaiHEN modification disclosure](KAIHEN_CHANGES.md) identifies exactly four changed source files against upstream `e897f991524590833b82fbe8d63a3c6775fb8101`: `common/control.h`, `hen/source/game_arm.cpp`, `hen/source/game_arm.hpp` and `hen/source/server.cpp`. That disclosure provides the complete patch, source and shipped HEN hash.

The upstream Civ7 port also describes a target-specific thread-handle adaptation: Civ7 uses tagged thread handles and needs a try-wait adapter. That entrypoint adaptation has its own scope and validation requirements, separate from Moader's four-file HEN patch. [Upstream porting notes](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/README.md#porting-notes).

A target shim can change the bootstrap image loaded into memory while preserving the original `bootstrap.bin` file and HEN binary hash. File identity alone therefore does not describe every runtime adaptation. The experimental host profile has separate implementation checks and hardware acceptance; neither is replaced by the HEN file hashes.

## Initial admitted host profile

The experimental selector is **`civ7-s8n`**, bound to `s8n/civ7-fs-kaihen` commit **`51a804cbce09216e1a42e65929644fd436b4c8bf`**. Only that source profile is admitted initially. **HerXayah FS runs and mac2jb CivSwitch runs are not admitted by this pin**, even if their console output also says 4/4.

An already-current **FS-only held session** from the exact s8n revision is a candidate for qualification. Keep its original host and Civilization VII open; having a working session does not itself require reacquisition. Userland-only, app-only, released/completed, uncertain and already-HEN/RO-frontend sessions do not meet this starting state. Moader must still qualify source/profile identity, current FS evidence and live runtime compatibility before privileged installation.

## Prepare the local dependencies

Use Python **3.10+** and native POSIX **Node 24**. Run these commands from the **v0.1.0-dev.2** backend kit or a matching source checkout. The app still displays `0.1.0-dev`.

To fetch or verify only the pinned host source, without Node/SDK setup, acquisition or console contact:

```sh
python3 tools/setup-backend.py --host-profile civ7-s8n --sources-only
```

For the full set of local backend inputs in a precompiled kit:

```sh
python3 tools/setup-backend.py --host-profile civ7-s8n --node node
```

After those inputs are present, validate them locally without downloads or installation:

```sh
python3 tools/setup-backend.py --host-profile civ7-s8n --check --node node
```

These commands fetch/check the pinned source and inputs locally; they do not launch the FS exploit or contact the Switch. The [selected source manifest](../config/civ7-provider.json) pins 120 files, including the Civ7 bridge needed by the FS-only runner. A full backend check also needs the matching app/HEN/release inputs: `--sources-only` alone does not prepare those. Upstream source is downloaded on your computer, not redistributed in the Moader kit.

## Experimental setup

This experimental workflow has passed host checks using pinned upstream sources and synthetic sessions. Its privileged installation and console handoff remain unverified on hardware. Use the v0.1.0-dev.2 backend kit or a matching checkout.

### 1. Keep an existing session, or acquire FS separately

**Already holding a current FS-only session from the exact admitted s8n revision?** Keep Civilization VII and its original host open. Use its actual run directory in step 2; do not reacquire solely to follow this guide.

Otherwise, confirm the version/network/save caveats above and establish the CivSwitch loader using [upstream entry instructions](https://github.com/Gezine/CivSwitch/blob/f6b0f2bd285e7b32c0a1968181046f18edd79708/README.md#workflow). From the Moader checkout or kit, fetch the selected sources locally:

```sh
python3 tools/setup-backend.py --host-profile civ7-s8n --sources-only
```

With CivSwitch showing **Awaiting payload**, replace `YOUR_SWITCH_IPV4` with the console address and explicitly start the FS-only acquisition in Terminal 1:

```sh
node research/sources/civ7-fs-kaihen/engine/run.cjs YOUR_SWITCH_IPV4
```

This command contacts the console and performs upstream FS acquisition; the preceding Python command only fetches/checks local source. The s8n runner holds by default. Wait for **4/4 verified** and the held FS-ready state, note its actual `runs/switch2-…` folder, and keep Terminal 1/Civilization VII open. Do not run `engine/hen.cjs` first or use `--app-only`/`--no-hold` for a Moader handoff. Unexpected failure is not permission to automate native retries.

### 2. Prepare Moader locally in Terminal 2

From the Moader checkout/kit, replace the quoted placeholder with the actual current s8n run directory. Use the same native Node24 as the acquisition host:

```sh
python3 tools/install-from-rpc.py --prepare \
  --host-profile civ7-s8n \
  --run-dir "/path/to/s8n/runs/<actual-run>" \
  --node node
```

Use `--node /absolute/path/to/node` if `node` would select another executable. On WSL, both terminals must stay in the same distro with Linux Node and POSIX paths. **Preparation is local:** it fetches missing pinned inputs, validates them, creates a unique immutable snapshot bound to `civ7-s8n`, and prints an installation command. It does not acquire FS, run a pasted status/acquisition script, contact the Switch or install HEN.

### 3. Review the printed command before privileged installation

The printed command uses `--prepared` with the unique manifest and actual run directory. The manifest selects the host profile; do not remove or replace it with a SmileBASIC snapshot. Running that command is the privileged step: it must qualify the admitted source/session and live FS/runtime before installation, then perform Moader's combined HEN handoff. A 4/4 message alone does not bypass qualification.

Keep the original host and Civilization VII open throughout. Run the exact printed command **once**; if qualification rejects the run or a native operation fails/times out, stop and preserve the report. Do not edit proof flags, replay handles, repeat an uncertain command or bootstrap stock HEN separately. [Recovery guidance](TROUBLESHOOTING.md#backend-setup-failed-or-timed-out).

A host test or successful upstream HEN run is not proof that this Moader sequence works on a console. Resident status, native routing/logging, Civ7 close/hbmenu handoff and browser lifetime through this entrypoint still require separate hardware validation. Existing SmileBASIC routes remain in [Installation](INSTALLATION.md).

## Network directions, Linux and WSL

| Connection | Direction | Reviewed s8n default |
| --- | --- | --- |
| Send the JavaScript bridge | Computer → console loader | Console TCP9026 |
| Bridge/FS transport | Console → computer listener | Computer TCP39380 |
| Local tools → held RPC bridge | Same host only | Dynamic loopback port with a per-run bearer token; never forward |

s8n's Civ7 CLI takes the **console** IPv4, unlike the SmileBASIC runner's computer-IP argument. Its loader code derives the host's source address for that route and injects it into the bridge; the engine binds that same host address. `NS2_PC_PORT` changes both the host listener and injected bridge port at this revision. [Runner](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/engine/run.cjs), [loader/address injection](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/engine/lib/loader.cjs), [local RPC bridge](https://github.com/s8n/civ7-fs-kaihen/blob/51a804cbce09216e1a42e65929644fd436b4c8bf/engine/lib/bridge.cjs).

Keep the local RPC bearer token and connection metadata out of screenshots and shared logs.

For WSL, successful outbound access to TCP9026 does not prove the Switch can reach the advertised return address. Our source-derived conclusion is that ordinary NAT can advertise a private WSL address that is unreachable from the Switch; a Windows port forward alone does not rewrite that injected address. Confirm both directions and a bindable, LAN-reachable host address before acquisition. This is not a tested WSL recipe. Do not reuse another fork's host-IP flags or port environment variables without checking its pinned source. Linux/WSL acquisition and Moader operation through CivSwitch remain untested here.

## Reviewed source revisions

Dates below are upstream commit dates in UTC; retrieval was 15 November 2026. Links are fixed revisions, not moving default branches.

| Project | Commit | Date |
| --- | --- | --- |
| [Gezine/CivSwitch](https://github.com/Gezine/CivSwitch/tree/f6b0f2bd285e7b32c0a1968181046f18edd79708) | `f6b0f2bd285e7b32c0a1968181046f18edd79708` | 2026-11-14 |
| [HerXayah/civ7-fs](https://github.com/HerXayah/civ7-fs/tree/5e5665b92661f205d853ea4351a9edace7c9c6b5) | `5e5665b92661f205d853ea4351a9edace7c9c6b5` | 2026-11-14 |
| [s8n/civ7-fs-kaihen](https://github.com/s8n/civ7-fs-kaihen/tree/51a804cbce09216e1a42e65929644fd436b4c8bf) | `51a804cbce09216e1a42e65929644fd436b4c8bf` | 2026-11-14 |
| [cpypwn/mac2jb](https://github.com/cpypwn/mac2jb/tree/717022b8a91a713be41a02b5e131d3229dd8b00a) | `717022b8a91a713be41a02b5e131d3229dd8b00a` | 2026-11-12 |
