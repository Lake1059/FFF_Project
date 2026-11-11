#pragma once

#include "3FP/Api/FFF.Player.Api.h"

// Startup CPU placement control surface (docs/146). Appended additively like the
// prefetch and audio-tap entries: this header is NOT included from FFF.Player.Api.h,
// adds no FFF3FPSnapshot field and does not move PlayerApiVersion (20) or
// SnapshotVersion (23). Hosts resolve both exports dynamically and degrade to today's
// behaviour when they are absent.
//
// What it does: raises the process-default CPU Set for the startup window so the
// threads that open the first frame prefer the fastest domain --
//   * Intel hybrid parts: the fastest EfficiencyClass group (the P-cores);
//   * AMD multi-CCD parts: the L3 domain the calling (UI) thread was already placed on
//     ("main CCD" has no public Windows definition; see docs/146 §四);
//   * machines with more than one processor group: refused, because the OS ignores CPU
//     Set assignments whose group does not match the thread's affinity group.
// CPU Sets are a soft affinity preference, not a hard pin.
//
// Lifecycle: opt-in through FFF_STARTUP_PLACEMENT=1; every other value is a no-op.
// The host calls Begin once from its startup hook and keeps a fallback End for
// sessions that never present a frame. The kernel also ends the placement by itself on
// the first presented frame. If a playback session has already taken ownership of the
// process-default CPU set, End leaves it alone (the playback policy owns it from then
// on). See PlayerSession/PresentVideoFrame and PlacementPolicy.
//
// Both functions are safe to call repeatedly and from any thread.
//   FFF3FP_BeginStartupCpuPlacement: 1 when the process default was applied (or was
//       already applied), 0 when it was disabled, unavailable or refused.
//   FFF3FP_EndStartupCpuPlacement: void; idempotent.
FFF3FP_API int FFF3FP_BeginStartupCpuPlacement() noexcept;
FFF3FP_API void FFF3FP_EndStartupCpuPlacement() noexcept;