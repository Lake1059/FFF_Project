#include "3FP/Stream/SegmentCache.h"

#include <cstdio>
#include <vector>

using namespace FFFPlayer::Stream;

int main() {
    SegmentCache cache(2048);
    std::vector<std::uint8_t> one(1024, 1);
    std::vector<std::uint8_t> two(1024, 2);
    if (!cache.Put(1, one) || !cache.Put(2, two)) return 1;
    if (cache.BytesUsed() != 2048 || !cache.Get(1) || !cache.Get(2)) return 1;
    if (!cache.Put(1, two)) return 1; // replacement must not duplicate the LRU key
    if (cache.BytesUsed() != 2048 || cache.Get(1)->front() != 2) return 1;
    std::vector<std::uint8_t> three(1024, 3);
    if (!cache.Put(3, three)) return 1;
    if (cache.BytesUsed() != 2048) return 1;
    if (cache.Get(1)) return 1; // oldest entry evicted
    if (cache.Get(3)->front() != 3) return 1;
    if (cache.Put(4, std::vector<std::uint8_t>(4096, 4))) return 1; // larger than capacity
    cache.Clear();
    if (cache.BytesUsed() != 0) return 1;
    std::printf("PASS SegmentCache\n");
    return 0;
}
