#include "3FP/IO/StepGate.h"

#include <cstdio>

using namespace FFFPlayer::Io;

int main() {
    StepGate gate(2);
    const auto first = gate.BeginGeneration();
    if (first == 0 || !gate.TryEnter(first) || !gate.TryEnter(first)) {
        std::fprintf(stderr, "first enter failed gen=%llu\n", (unsigned long long)first);
        return 1;
    }
    if (gate.TryEnter(first)) {
        std::fprintf(stderr, "max in flight not enforced inFlight=%u\n", gate.InFlight());
        return 1;
    }
    gate.Leave();
    if (gate.InFlight() != 1) {
        std::fprintf(stderr, "leave count=%u\n", gate.InFlight());
        return 1;
    }
    if (!gate.TryEnter(first)) {
        std::fprintf(stderr, "re-enter failed inFlight=%u\n", gate.InFlight());
        return 1;
    }

    const auto second = gate.Cancel();
    if (second <= first || gate.TryEnter(first)) {
        std::fprintf(stderr, "cancel failed first=%llu second=%llu\n",
            (unsigned long long)first, (unsigned long long)second);
        return 1;
    }
    if (!gate.TryEnter(second)) {
        std::fprintf(stderr, "second enter failed gen=%llu current=%llu inFlight=%u\n",
            (unsigned long long)second, (unsigned long long)gate.Generation(), gate.InFlight());
        return 1;
    }
    gate.Leave();
    gate.Leave(); // stale first-generation leave must clamp at zero
    if (gate.InFlight() != 0) {
        std::fprintf(stderr, "inFlight after leaves=%u\n", gate.InFlight());
        return 1;
    }

    const auto third = gate.BeginGeneration();
    if (gate.TryEnter(second)) {
        std::fprintf(stderr, "old second entered after third\n");
        return 1;
    }
    if (!gate.TryEnter(third)) {
        std::fprintf(stderr, "third enter failed gen=%llu current=%llu\n",
            (unsigned long long)third, (unsigned long long)gate.Generation());
        return 1;
    }
    gate.Leave();
    std::printf("PASS StepGate generation cancellation\n");
    return 0;
}
