# Standalone Seedsow: remaining Moader FS installation

This is the development boundary for the experimental [on-console route](SEEDSOW_STANDALONE.md). The first Profile B device run proved upstream FS promotion, HEN startup, hbmenu and Moader app launch. It did not install Moader's FS runtime: the current catalogue and the old acknowledgement had generation 136 but different epochs. The app therefore showed the filesystem backend disconnected. The PC-driven route installs that runtime separately in `tools/install-runtime.cjs` before its HEN handoff.

## Offline payload now available

`tools/seedsow-standalone-fs-payload.cjs` accepts an explicitly selected, previously verified native FS capture. It requires the exact sparse capture SHA-256 `0f58beb0a35ca2dadaf6300a8bbf4432974edfeac405aba4b3a3aa01af7d9271` and the 36 source-qualified Moader FS ABI ranges. It builds the zero-game dynamic Moader runtime from the existing native templates, a report template, 19 hook preimages/replacements and relocation tables. The output is **offline candidate data**, not an installer. It is not copied to the SD card or included in a release.

Two independent prior PC-driven captures produced identical payloads: 86,016 context bytes, 106,496 runtime bytes, 816 gateway bytes, four runtime address relocations and 19 gateway address relocations. The generator applies its relocation table at a separate FS/context/runtime address set and compares the bytes and hook records with an independent `buildAll` result. This verifies the relocation format, not any console write.

## Required on-console adapter

The standalone chain currently calls `s2_kh_ro.run_ro_and_bootstrap` after promotion and has no equivalent of the PC-driven `resident.install` phase. The adapter must run **after current-boot FS memory promotion and before HEN startup**, with a pinned local Lua overlay or native component whose exact bytes and save upload are checked separately from the unchanged upstream source. It must:

1. Bind the owned promoted endpoint, FS process handle and replacement RO reader to the same current boot. Verify FS program ID, current code mappings, all required function bytes, untouched gateway/cave preimages and report ABI before any patch.
2. Open an owned SD filesystem and `/moader/runtime/runtime.log`; record object identity, method tables and reference baselines. Initialize the report, acknowledgement and log slots without borrowing an old epoch.
3. Allocate the context and executable backing in FS using the qualified native call bridge. Apply the offline payload's FS, context and runtime relocations using **observed** addresses, verify every transferred page, map RX code and publish it. No unchecked fixed ASLR address is admissible.
4. Retain the SD/report references exactly once, close temporary owners, stage gateways in the proved empty code cave, compare each of the 19 original instruction blocks, attach hooks, activate the runtime and verify idle counters and report writes. Preserve an uncertain owner and require a full reboot after any ambiguous native operation.
5. Continue the matched RO/HEN/bootstrap chain only after the FS runtime is verified. Once Moader publishes a new catalogue, verify a CRC-valid FS acknowledgement with the **same current epoch and generation** before reporting a complete Moader installation. HEN READY alone is insufficient.

The PC-driven `runtime/modloader/lib/resident.cjs`, `native.cjs`, `context.cjs` and `source.cjs` are the behavior and ownership references. A source-qualified on-console adapter and same-boot acknowledgement remain to be implemented and device-tested. Until then, the experimental route is a kaiHEN diagnostic, and the [PC-driven Seedsow guide](SEEDSOW.md) is the Moader installation route.
