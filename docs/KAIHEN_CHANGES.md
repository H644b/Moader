# How Moader modifies kaiHEN

Moader publishes the full modified source and the complete patch against the pinned upstream revision. The current patch adds cooperative plugin reload alongside the earlier pending-launch fix and read-only game trace. The application, resident service and FS hook have separate roles and validation records.

| Record | Value |
| --- | --- |
| Upstream project | [ouncebrew/kaihen](https://github.com/ouncebrew/kaihen) |
| Source base | [`e897f991524590833b82fbe8d63a3c6775fb8101`](https://github.com/ouncebrew/kaihen/tree/e897f991524590833b82fbe8d63a3c6775fb8101) |
| Official release inputs | [`20261114c`](https://github.com/ouncebrew/kaihen/releases/tag/20261114c) |
| Exact changes | [kaihen-mo.patch](../software-builds/kaihen-mo.patch) |
| Full modified source | [runtime/kaihen](../runtime/kaihen/) |
| Source, patch and HEN/bootstrap digests | [kaihen-mo.json](../software-builds/kaihen-mo.json) |
| Shipping app/resident and HEN/bootstrap sizes and hashes | [release.json](../config/release.json) |

## Pending homebrew launch

An R-button press can occur while SmileBASIC is still active or before opening a Switch 1 game. In `hen/source/game_arm.cpp`, the unsupported-application branch returns success while keeping the native launch pending. It preserves the native target eligibility checks and waits for a suitable application, rather than permanently faulting the pending launch.

## Read-only game trace

`HenControlCommand_MoaderGameTrace`, command `0x4d4f0001`, returns the existing `GameTraceSnapshot`. `common/control.h` defines it, `game_arm.hpp` exposes the snapshot, and `server.cpp` handles the query. It does not enable a mod or install a filesystem hook.

## Cooperative plugin reload

HEN protocol discovery is now **7**. Existing homebrew command IDs, `HenStatus`, `HenPluginName` and scalar reply layouts remain in place. Additive commands 23–26 expose selected-plugin identity, one reload submission, authoritative operation status and capabilities. [reload.h](../runtime/kaihen/libkaihen/include/kaihen/reload.h) gives the exact layouts; the 96-byte submission fits the existing inline request envelope and the largest new reply is 56 bytes.

The reload manager runs outside HEN's request loop. It binds a submission to the actual calling process, selected registry name, expected PID/generation and HEN instance, with nonce deduplication. One Loader lease serializes it with game arming. The lease permits the existing safe WaitForInput/WaitExit checkpoints and preserves the running host's tracking state. Matching the donor program and storage prevents unrelated Loader requests from counting as the selected mount.

The worker stages the selected fixed-path executable and prepares its package before asking the old plugin to stop. An opt-in lifecycle protocol handles busy denial, drain, asynchronous abort, post-reply exit acknowledgement and replacement health. The exact old PID must exit before one replacement is launched. HEN verifies the new startup peer, transfers the required services, waits for transport readiness and separately checks application health before publishing the new generation. Legacy plugins without the handshake are refused before shutdown; there is no forced termination fallback for the old plugin.

Deadlines record uncertainty; they do not cancel native IPC or prove a process exited. Restoration errors remain separate from the primary failure. Unproved cleanup quarantines the operation and retains its resources instead of allowing another writer or launch attempt. [Usage and recovery](PLUGIN_RELOAD.md).

The bootstrap now transfers a retained PM launch capability and the donor launch specification to HEN through an additive control command. Its host input layout stays unchanged. This needs a **matching modified HEN and bootstrap pair**, plus the rebuilt plugin runtime and cooperative resident. Updating a file cannot add commands to an older running HEN.

## Source changes

The complete patch includes the following areas; its individual file diffs are authoritative:

| Area | Files and purpose |
| --- | --- |
| Bootstrap and shared launch metadata | `bootstrap/source/main.cpp`, `common/control.h`, new `common/plugin_launch.hpp`: retained launch session/specification handoff. |
| Package and mount handling | `common/code_mount_request.hpp`, `common/domain_mount.hpp`, `common/policy_package.cpp/.hpp`: exact target matching and checked package digest generation. |
| HEN owner and server | `hen/source/game_arm.cpp/.hpp`, `hen/source/server.cpp/.hpp`: serialized Loader lease, managed records, submission/status and publication. |
| Reload worker | New `hen/source/plugin_reload.cpp/.hpp`, `reload_state.hpp` and `lifecycle_client.hpp`: staging, lifecycle, PM/Loader startup, health, cleanup and bounded retry contracts. |
| Public and plugin APIs | `libkaihen/include/kaihen.h`, `kaihen/protocol.h`, `kaihen/plugin.h`, new `kaihen/reload.h`: additive commands, source-compatible start API and opt-in lifecycle. |
| Plugin runtime/server | `libkaihen/source/plugin_runtime.cpp`, `plugin_server.cpp`, `plugin_startup.h`: startup identity, lifecycle dispatch, guaranteed post-reply notification and retained ownership on failed thread joins. |
| Host regressions | `hen/test.sh`, `hen/tests/reload_test.cpp`, `lifecycle_server_test.cpp` and host declarations: actual protocol/orchestration/server tests with controlled transports. |

The `PluginService` source structure gains lifecycle and thread ownership fields. Rebuild plugins against the matching headers and library; mixing the new headers with a stock precompiled plugin library is unsupported. Already-built legacy plugins carry their own runtime and retain their existing enumeration/open behavior.

## Other components and host profiles

The FS runtime derived from ns2jb-modloader lives under [runtime/modloader](../runtime/modloader/) and is unchanged by the reload feature. It remains installed in FS while the selected resident process restarts. This command cannot repair an FS/system failure or clear a game's loaded assets.

RO, the game loader and hbmenu remain checksum-qualified official release components. HEN **and bootstrap** use the paired source-qualified bytes above. Setup and installation verify the repository's source-tree digest, patch digest, both binary descriptors and their prepared-snapshot proof identity. These hashes record a reviewed build relationship; they are not themselves proof of hardware behavior.

The experimental [CivSwitch profile](CIV7.md) additionally selects target-specific transports and appends a 24-byte tagged-thread try-wait adapter to its in-memory bootstrap image. That adaptation does not modify the pinned file on disk and does not establish Civ7 console acceptance. Its implementation and qualification remain separate from the public kaiHEN patch.

## Rebuild and validation

[Building](BUILDING.md) explains the compiler/SDK and isolated source build. HEN and bootstrap must reproduce their paired digests before setup populates installation inputs. The new build's console validation is recorded explicitly, separately from host tests, in the [build proof](../software-builds/kaihen-mo.json) and [validation summary](VALIDATION.md). No game content, donor policy capture or device credentials are included. Upstream licenses and notices remain in place; see [credits](../THIRD_PARTY.md).
