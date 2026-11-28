# 3FP I/O 2.0 — Phase 0 contracts

This directory is the phase-0 foundation for the I/O 2.0 work described in
`3FCompare/docs/134` and `136`. It is deliberately header-only and Windows-free:
the player can compile and test the contracts before any read path is changed.

## What is here

| Header | Contract |
| --- | --- |
| `IoStatus.h` | `Status`, `Error`, `Result`; terminal/retryable classification |
| `SourceCapabilities.h` | byte-random / segmented / realtime / disc source domains |
| `Deadline.h` | RTT-derived read/open/seek/stall deadlines, retry backoff, L0–L4 |
| `IoBudgetMath.h` | BDP, AVIO, ByteWindow, packet/segment/frame/prefetch/resume budgets |
| `ReadLedger.h` | per-source epoch, offset, ETag and Content-Range validation |
| `ProgressWatchdog.h` | G1–G4 health state machine (stall never becomes failure) |
| `BudgetController.h` | 500 ms sampling, 25% step clamp, 10% hysteresis |
| `IoOptions.h` | `CorrectnessFirst` / `Realtime` mode and operation options |
| `IMediaSource.h` | the only byte-source interface a demuxer needs |
| `SessionBudgetLease.h` | 2 s budget lease, so shrinking happens at a safe point |

## Invariants

1. A timeout, short read or transport stall is **not** EOF and **not** a
   permanent failure. `ProgressWatchdog::Tick()` can only produce
   `Stalled`/`WaitingNetwork`.
2. `ReadLedger` rejects a completion whose source epoch, offset, ETag or
   `Content-Range` does not match the request. Old-epoch bytes are never
   appended to the current position.
3. Configuration epochs are separate from source epochs: resizing a window
   must not invalidate already validated bytes.
4. Every arithmetic helper saturates and clamps; zero/unknown measurements
   fall back to a conservative floor, never an immediate timeout.
5. `CanPromiseContinuousPlayback` requires both the rate margin and
   `bitrate × RTT` to fit the ByteWindow ceiling. Otherwise the contract is
   “stay buffering, do not fail” (G3b), not “guarantee playback”.

## Tests

The tests compile the headers with MSVC C++20 and a fake monotonic clock; they
do not start FFmpeg, a player session, or a network connection.

```powershell
cd 3fp/kernel
python FFF.Native.Tests/run_io_v2_tests.py --log FFF.Native.Tests/io_v2_tests.log
```

Expected output: `PASS I/O 2.0 contract arithmetic and state machine`.

The runner trims `PATH` before calling `vcvars64.bat`; the default VS
environment can exceed the `cmd.exe` environment-block limit on some machines.

## Integration boundary (intentionally not in this PR)

Phase 0 does not change `SharedFileInput`, `IocpMediaReader`, `PlayerSession`
or the exported ABI. P1 will:

- wrap the existing local/UNC reader in `IMediaSource`;
- feed `ReadLedger` and `ProgressWatchdog` from the session pump;
- replace the fixed 8 s/15 s/6 s timeouts with `Deadline.h` values;
- keep `FFF_IO_V2=0` as the rollback switch.

Until then the old path remains byte-for-byte unchanged.
