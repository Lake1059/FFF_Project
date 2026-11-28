#include "pch.h"

#include "3FP/IO/BudgetController.h"
#include "3FP/IO/Deadline.h"
#include "3FP/IO/IoBudgetMath.h"
#include "3FP/IO/IoOptions.h"
#include "3FP/IO/IoStatus.h"
#include "3FP/IO/ProgressWatchdog.h"
#include "3FP/IO/ReadLedger.h"
#include "3FP/IO/SessionBudgetLease.h"
#include "3FP/IO/SourceCapabilities.h"

// This translation unit exists so the MSBuild/Native build compiles the I/O 2.0
// contracts under the real project settings (C++20, /W4, PCH). The implementation
// is header-only in phase 0; P1 sources will get their own .cpp files.
namespace FFFPlayer::Io {
static_assert(IoContractVersion == 1, "I/O 2.0 contract version changed without review");
static_assert(static_cast<std::uint8_t>(Status::Ok) == 0, "Status::Ok must stay zero");
static_assert(static_cast<std::uint8_t>(Status::Eof) == 4, "Status::Eof value is part of the contract");
} // namespace FFFPlayer::Io
