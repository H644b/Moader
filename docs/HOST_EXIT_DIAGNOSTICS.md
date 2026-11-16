# Recording a homebrew host exit

This is an **unreleased diagnostic extension**. The published v0.1.1 downloads do not contain it. It records evidence for the unresolved `2011-0102` lifecycle failure; it does not establish a repair. See the [validation record](VALIDATION.md) for the failed console tests.

The important distinction is how the host closes. Returning from an app normally runs its cleanup code. Closing the whole host with **HOME → X** terminates the process, so the system and other servers must release their references. A saved normal-exit report cannot prove that this second path worked.

## What this build records

HEN keeps a fixed ring of 64 records in memory. It observes calls and transitions already present in the loader:

- Homebrew channel creation, channel/process-reference closure, actual close errors and local occupied-slot counts.
- Existing filesystem clone attempts and their results.
- SM proxy workers that are running, finished but retained, joined or awaiting cleanup; sampled pending service lookups and returned errors.
- The R sample that arms a launch, game-arm state, installed/donor domain identities, existing process-exit observations and package-close boundaries.

HEN's main loop is the ring's only writer. Workers publish atomic mailboxes; each idle slice samples at most 16 SM contexts. Records carry main-thread publication ticks. Cached observations, coalesced mailbox transitions, partial count summaries and overwritten ring records are identified explicitly.

These are **local ownership observations**. A successful local close is not proof that the final remote reference disappeared. A package-close boundary has no native result because its existing close API returns void. An unknown owner PID stays unknown; a process handle is not labeled as a PID. No server-pool capacity, failing allocation stack or kernel teardown order is inferred from these counters.

## How records reach the browser connection

HEN sends batches of at most eight records over the resident plugin's existing control connection. Each batch starts with a coherent header. The receiver validates each frame and acknowledges the HEN boot ID and committed sequence. It stores the frames separately from the mod library, including during resident startup.

Export requires no active reload and an idle game gate. It ordinarily waits for HEN homebrew channels to close. Once GameArm's existing PM check has observed the selected host exit, export can proceed with retained channels, and HEN records a fresh local slot-count snapshot. This exception persists until a different selected process is known. It adds no PM query and does not attribute every remaining channel to the exited PID.

Game arming pauses until the batch's worker has exited and its thread is closed. A same-plugin open or reload that overlaps a pending transfer reports Busy. This changes timing and adds diagnostic overhead: fixed ring/cache storage, larger existing SM contexts and at most one transfer worker with a 16 KiB stack. It opens no additional FS or HEN service session.

Consequently, records can remain buffered while the selected hbmenu host is alive. After **HOME → X**, they can arrive even if a channel remains retained, once the existing process monitor has observed that host's exit. A stalled main loop or a gate outside the allowed idle states can still prevent export. A cached response is not a continuously synchronized global snapshot.

An explicit receiver-busy reply confirms the frame was not accepted; a later batch can resume after the last acknowledgement. A native transport error, malformed response, timeout or uncertain thread/handle cleanup stops further export for that HEN instance. Its unfinished ownership is retained. A late completion does not silently re-enable the channel. Replies with unexpected transferred handles are rejected, and received references are closed once; failed closes remain tracked.

The read-only HEN scalar queries and the resident HTTP route only copy cached memory. They do not probe FS/PM/RO, reap workers, acquire services or start reloads.

## Capture on macOS, Linux or WSL

Use a matched diagnostic **HEN, bootstrap and resident** build. Updating the ordinary NRO alone cannot add these observations to a running HEN. Installing this HEN change requires a fresh, qualified FS session. Retired RPC commands must not be reused.

After that build is installed and Moader's resident Web GUI is enabled:

1. Copy the address and port shown by Moader.
2. In a checkout of this diagnostic branch, run the command below, replacing `SWITCH_ADDRESS` and `PORT`.
3. Enter the six-digit pairing code when prompted. It is hidden while typing.
4. Leave the capture running during the agreed console test. Press **Ctrl+C** afterward.

```sh
python3 tools/capture-host-census.py \
  --endpoint http://SWITCH_ADDRESS:PORT/api/diagnostics/host-census \
  --output host-exit.jsonl
```

The output must be a new filename. The tool creates it with private permissions (`0600`) and refuses existing files. It sends authenticated GET requests only to the exact supplied route, follows no redirects and discovers no devices. The pairing code is absent from the captured output. An optional `--token-file` accepts an owned, private regular file when unattended capture is necessary; the code is never an argument.

The console sequence is coordinated separately so its timestamps can be matched to the capture. If a system error appears, stop that test and preserve the capture. A new FS session is not a substitute for reading the evidence already collected.

## Reading the capture

Each JSONL line has a UTC timestamp, attempt number, elapsed request time and listener observation. A valid response contains schema 1 and cached census data. All 64-bit values are decimal strings so browser/JSON readers preserve them exactly.

Useful fields:

| Field | Meaning |
| --- | --- |
| `receiverId` | This resident receiver's local startup identity. It changes when the resident is replaced. |
| `header.bootId` | HEN instance identity, preserved across resident reload. |
| `sessionGeneration` | Accepted producer-stream generation within this receiver. |
| `header.firstSequence`, `header.nextSequence` | Current advertised ring window; the upper bound is exclusive. |
| `header.overwritten` | Producer records overwritten before the current window. |
| `lastSequence`, `acceptedRecords` | Confirmed accepted record progress in this receiver. |
| `sequenceGaps` | Missing sequence numbers between delivered records. |
| `rejectedFrames`, `busyFrames` | Validation failures and separately counted receive contention. |
| `lastHeaderFrame`, `lastRecordFrame` | Last observed frames, including rejected data; check their status and stream attribution. |
| `records` | At most 64 accepted records retained in the current advertised window. |

Record sources are HEN (1), SM (2) and GameArm (3). Source-mask bits are `1 << source`. Event IDs, flags and exact wire layouts are defined once in [census.h](../runtime/kaihen/libkaihen/include/kaihen/census.h). The three payload fields `a`, `b` and `c` depend on the event; their raw handle values do not measure remote allocator occupancy.

For the HEN `RelayState` event (8), `a` is the resident PID and `c` is the last acknowledged sequence. The low 32 bits of `b` hold the transfer state: idle 0, pending 1, complete 2, unsupported 3, failed 4, retained 5 or deferred 6. Bits 32–47 count unexpected received handles; bits 48–63 count their failed closes. The result is the observed transfer/cleanup result, not a measurement of another server's allocator.

`available:false` is an observation, not a diagnosis. `unsupported` means this WebServer has no cache; `cache-busy` means the snapshot could not acquire the cache immediately. An initialized receiver with no header yet can also be unavailable while HEN defers export. A transport timeout is recorded as **listener state unknown**. It does not establish that the resident process exited.

A resident replacement starts an empty receiver even if HEN's boot ID is unchanged. A fatal can prevent the final buffered records from reaching the listener. Receiver identity, ring overwrites, sequence gaps and the last successful capture delimit what the evidence can support. No extra diagnostic file writer or SD write is added by this extension.
