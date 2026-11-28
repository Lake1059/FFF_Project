#include "3FP/IO/RouteBudgetArbiter.h"

#include <cstdio>

using namespace FFFPlayer::Io;

int main() {
    RouteBudgetArbiter arbiter(9 * 64ull * 1024 * 1024);
    RouteBudgetLease leases[9];
    for (int i = 0; i < 9; ++i) {
        std::uint64_t bytes = 0;
        if (!arbiter.TryAcquireLease(64ull * 1024 * 1024, bytes)) {
            std::fprintf(stderr, "route %d should fit\n", i);
            return 1;
        }
        leases[i] = RouteBudgetLease(arbiter, bytes);
    }
    if (arbiter.Current().activeRoutes != 9 || arbiter.Current().used != 9 * 64ull * 1024 * 1024)
        return 1;
    if (arbiter.TryAcquire(64ull * 1024 * 1024)) {
        std::fprintf(stderr, "tenth route must be rejected\n");
        return 1;
    }
    leases[3].Release();
    std::uint64_t bytes = 0;
    if (!arbiter.TryAcquireLease(64ull * 1024 * 1024, bytes)) return 1;
    leases[3] = RouteBudgetLease(arbiter, bytes);
    if (arbiter.Current().activeRoutes != 9) return 1;
    if (arbiter.SetCapacity(9 * 64ull * 1024 * 1024) == false) return 1;
    if (arbiter.SetCapacity(8 * 64ull * 1024 * 1024) == true) return 1; // shrink below used fails

    for (auto& lease : leases) lease.Release();
    if (arbiter.Current().used != 0 || arbiter.Current().activeRoutes != 0) return 1;
    std::printf("PASS RouteBudgetArbiter 9-route admission\n");
    return 0;
}
