# How Moader modifies kaiHEN

Moader ships a modified kaiHEN for features such as cooperative service reload and game trace diagnostics. Use the exact source and build records for the version you are reviewing.

| Record | Value |
| --- | --- |
| Upstream project | [ouncebrew/kaihen](https://github.com/ouncebrew/kaihen) |
| Source base | [`e897f991524590833b82fbe8d63a3c6775fb8101`](https://github.com/ouncebrew/kaihen/tree/e897f991524590833b82fbe8d63a3c6775fb8101) |
| Official release inputs | [`20261114c`](https://github.com/ouncebrew/kaihen/releases/tag/20261114c) |
| Current source changes | [kaihen-mo.patch](../software-builds/kaihen-mo.patch) |
| Full modified source | [runtime/kaihen](../runtime/kaihen/) |
| Current source, patch and component digests | [kaihen-mo.json](../software-builds/kaihen-mo.json) |
| v0.1.2 release source and binary digests | [Tagged build proof](https://github.com/H644b/Moader/blob/v0.1.2/software-builds/kaihen-mo.json) · [Tagged release descriptor](https://github.com/H644b/Moader/blob/v0.1.2/config/release.json) |

## Current source profile

The source tree has a matched RO, HEN and bootstrap for Seedsow Lullaby Demo. Its schema-3 proof records the exact component hashes. The current HEN and bootstrap retain the bytes from a controlled clean-card Demo installation and game-entry check. The RO now accepts the experimental standalone Seedsow adapter's FS-handle-gated commands on the existing app-space `ldr:ro` service. That new RO and the complete pair have only host validation, so the current proof sets `console_retest_passed` to false. The earlier Demo installation records in [Seedsow validation](VALIDATION.md#seedsow-lullaby-demo-source-workflow) apply to the prior RO bytes. Use the tagged proof above for the v0.1.2 kit.

## Hold-R game entry candidate

The v0.2.5 installer success receipt shows HEN waiting for a game. It does not show whether a later R press was sampled, whether the first game supplied a signed policy, or whether the second launch entered hbmenu. A tester's v0.2.5 archive reached that waiting state and contained no post-install game trace. On the maintainer's working console, the saved game policy predates the installation, so its successful entry did not exercise first-launch policy capture.

The rc.1 HEN sampled fresh R state from SystemExt and the enabled standard controller styles, including FullKey, Handheld, JoyDual and JoyRight. During first-launch capture it waited past a still-closing nonnative Seedsow application instead of faulting before the native game appeared. Host tests covered a fresh FullKey R sample, rejected a stale one and covered this PM transition; the native HEN built with the pinned toolchain. One Demo installation and existing-policy hold-R entry passed with the exact rc.1 candidate. Neither mechanism is established as the tester's exact failure cause, and rc.1 did not pass a clean-policy console test.

An affected tester's later archive confirms installation and readback of that exact candidate, but they reported the same no-hbmenu result. The archive ends before either game launch and cannot identify the game-arm step that failed. The candidate remains diagnostic work rather than a validated fix.

The rc.2 candidate also observes the first native game's PM process when no saved policy exists. It captures that game's signed policy while the first launch stays stock, even if HEN missed its R sample. R remains necessary for the later hbmenu takeover. The PM probe is limited to once per quarter second and ignores Switch 1 titles; host tests cover the automatic first-launch transition and policy staging. A replacement-card Demo check installed the exact rc.2 HEN; a normal first Pokémon Z-A launch was followed by an R launch that opened hbmenu and Moader with runtime/FS connected. Read-only FTP found a 1,908-byte `META` game policy afterward. The starting blank-card state is user-reported, and the failed tester has not retested rc.2; their exact failure cause remains unknown.

## v0.1.2 source selection

Official kaiHEN [`20261117.1`](https://github.com/ouncebrew/kaihen/releases/tag/20261117.1) was inspected. Its [released `waitForProcess` implementation](https://github.com/ouncebrew/kaihen/blob/3695f364f9270686aa72cc39627bb1281752b2d3/hen/source/game_arm.cpp) still returns `UnexpectedState` when an observed title is outside the native application range and the bridge-process exception is inactive. It does not contain a replacement for Moader's removed local workaround. This is a source/history finding, not a Switch 1 gameplay test.

Moader retains the qualified `20261114c` / `e897f991524590833b82fbe8d63a3c6775fb8101` base. The newer official command 23 is `GetLoaderRoot`, while Moader's retained ABI assigns that number to plugin information; the newer release is not integrated. Use the matched Moader components rather than mixing these protocol-7 implementations. The v0.1.2 native rebuild, complete patch replay, source/component checks and host regressions passed. Exact source, patch and component identities are recorded in the linked build proof and release descriptor; new hardware validation remains separate.

## Read-only game trace

`HenControlCommand_MoaderGameTrace`, command `0x4d4f0001`, returns the existing `GameTraceSnapshot`. `common/control.h` defines it, `game_arm.hpp` exposes the snapshot, and `server.cpp` handles the query. It does not enable a mod or install a filesystem hook.

## Cooperative plugin reload

HEN protocol discovery is now **7**. Existing homebrew command IDs, `HenStatus`, `HenPluginName` and scalar reply layouts remain in place. Additive commands 23–27 expose selected-plugin identity, one reload submission, authoritative operation status, capabilities and cached launch diagnostics. [reload.h](../runtime/kaihen/libkaihen/include/kaihen/reload.h) gives the exact layouts; the 96-byte submission fits the existing inline request envelope. Operation status remains 56 bytes; the separate diagnostics reply is 64 bytes.

The reload manager runs outside HEN's request loop. It binds a submission to the actual calling process, selected registry name, expected PID/generation and HEN instance, with nonce deduplication. One Loader lease serializes it with game arming. The lease permits the existing safe WaitForInput/WaitExit checkpoints and preserves the running host's tracking state. Matching the donor program and storage prevents unrelated Loader requests from counting as the selected mount.

The worker stages the selected fixed-path executable and prepares its package before asking the old plugin to stop. An opt-in lifecycle protocol handles busy denial, drain, asynchronous abort, post-reply exit acknowledgement and replacement health. The exact old PID must exit before one replacement is launched. HEN verifies the new startup peer, transfers the required services, waits for transport readiness and separately checks application health before publishing the new generation. Legacy plugins without the handshake are refused before shutdown; there is no forced termination fallback for the old plugin.

Deadlines record uncertainty; they do not cancel native IPC or prove a process exited. Restoration errors remain separate from the primary failure. Unproved cleanup quarantines the operation and retains its resources instead of allowing another writer or launch attempt. [Usage and recovery](PLUGIN_RELOAD.md).

The offline repair uses elapsed-time polling for reload's initial empty park and subsequent real Loader requests. It preserves the established synchronous Bootstrap/GameArm parking entry points and all gate invariant/receipt checks. Progress reporting uses a lock-free atomic store; it adds no service call between waiter selection and cancellation. Diagnostics retain primary gate steps before restoration changes them, and command 27 returns cached state without probing Loader. RO memory requests also provide valid output-handle storage to the SDK response parser. These changes address timing and reply-layout cases reproduced in host fixtures; their device scope is recorded separately.

The package ownership checks track the donor object actually installed in a game's CodeFS entry and compares its address, object and metadata during process cleanup. An entry address reused for another object no longer keeps the old package pinned. The SM proxy also retains session context and partial join state when a thread wait or close fails, including an unstarted thread that could not be closed. Package-open replies now supply the SDK's required output-handle storage. Full address/object/metadata reuse and the stock server's actual allocation budget remain outside the host fixture's proof.

The bootstrap now transfers a retained PM launch capability and the donor launch specification to HEN through an additive control command. Its host input layout stays unchanged. This needs a **matching modified HEN and bootstrap pair**, plus the rebuilt plugin runtime and cooperative resident. Updating a file cannot add commands to an older running HEN.

The host installer also has a separate Loader-restoration adapter: [loader-restoration.cjs](../tools/loader-restoration.cjs) uses a checked receive acknowledgment, observed through [loader-observer.cjs](../tools/loader-observer.cjs), before clearing its parking event. The observer uses the already verified diagnostic RO and closes before that process is retired. This changes the installation procedure, not the HEN/bootstrap binary pair or either pinned acquisition provider. The adapter's validation limits are recorded in [Validation](VALIDATION.md).

## Source changes

The complete patch includes the following areas; its individual file diffs are authoritative:

| Area | Files and purpose |
| --- | --- |
| Bootstrap and shared launch metadata | `bootstrap/source/main.cpp`, `common/control.h`, new `common/plugin_launch.hpp`: retained launch session/specification handoff. |
| Package and mount handling | `common/code_mount_request.hpp`, `common/domain_mount.hpp`, `common/package.hpp`, `common/policy_package.cpp/.hpp`: exact target matching, SDK reply storage and checked package digest generation. |
| Gate and memory transport | `common/loader_gate.hpp`, `common/retained_memory.hpp`: timed reload parking/request polling, cached observations and valid SDK response storage. |
| HEN owner and server | `hen/source/game_arm.cpp/.hpp`, `hen/source/server.cpp/.hpp`: serialized Loader lease, managed records, submission/status and publication. |
| Service-manager proxy | `hen/source/sm_proxy.cpp/.hpp`: retain owned session/thread context on failed cleanup and resume partial joins safely. |
| Reload worker | New `hen/source/plugin_reload.cpp/.hpp`, `reload_state.hpp`, `reload_diagnostics.hpp` and `lifecycle_client.hpp`: staging, lifecycle, PM/Loader startup, cached diagnostics, health, cleanup and bounded retry contracts. |
| Public and plugin APIs | `libkaihen/include/kaihen.h`, `kaihen/protocol.h`, `kaihen/plugin.h`, new `kaihen/reload.h`: additive commands, source-compatible start API and opt-in lifecycle. |
| Plugin runtime/server | `libkaihen/source/plugin_runtime.cpp`, `plugin_server.cpp`, `plugin_startup.h`: startup identity, lifecycle dispatch, guaranteed post-reply notification and retained ownership on failed thread joins. |
| Host regressions | `hen/test.sh`, `hen/tests/reload_test.cpp`, `lifecycle_server_test.cpp`, `loader_gate_test.cpp`, `session_lifetime_test.cpp` and host declarations: production protocol/orchestration/server/gate/lifetime paths with controlled transports. `ro/test.sh` also checks repeated RO client admission and teardown. |

The `PluginService` source structure gains lifecycle and thread ownership fields. Rebuild plugins against the matching headers and library; mixing the new headers with a stock precompiled plugin library is unsupported. Already-built legacy plugins carry their own runtime and retain their existing enumeration/open behavior.

## Other components and host profiles

The FS runtime derived from ns2jb-modloader lives under [runtime/modloader](../runtime/modloader/) and is unchanged by the reload feature. It remains installed in FS while the selected resident process restarts. This command cannot repair an FS/system failure or clear a game's loaded assets.

The v0.1.2 kit uses the official RO, game loader and hbmenu with a matched custom HEN and bootstrap. The Seedsow source build also provides its own matched RO. Setup and installation verify the repository's source-tree digest, patch digest, both binary descriptors and their prepared-snapshot proof identity. These hashes record a reviewed build relationship; they are not themselves proof of hardware behavior.

The experimental [CivSwitch profile](CIV7.md) additionally selects target-specific transports and appends a 24-byte tagged-thread try-wait adapter to its in-memory bootstrap image. That adaptation does not modify the pinned file on disk and does not establish Civ7 console acceptance. Its implementation and qualification remain separate from the public kaiHEN patch.

## Rebuild and validation

[Building](BUILDING.md) explains the compiler/SDK and isolated source build. HEN and bootstrap must reproduce their paired digests before setup populates installation inputs. The new build's console validation is recorded explicitly, separately from host tests, in the [build proof](../software-builds/kaihen-mo.json) and [validation summary](VALIDATION.md). No game content, donor policy capture or device credentials are included. Upstream licenses and notices remain in place; see [credits](../THIRD_PARTY.md).
