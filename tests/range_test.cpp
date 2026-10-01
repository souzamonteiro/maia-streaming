#include "maia/vod/range.hpp"
#include <cassert>
#include <iostream>

int main() {
    using maia::vod::parseByteRange;
    using maia::vod::formatContentRange;
    using maia::vod::formatContentRangeUnsatisfiable;

    // Normal range
    auto a = parseByteRange("bytes=0-99", 1000);
    assert(a && a->first == 0 && a->last == 99 && a->length() == 100);
    assert(formatContentRange(*a, 1000) == "bytes 0-99/1000");

    // Open-ended range: 900-
    auto b = parseByteRange("bytes=900-", 1000);
    assert(b && b->first == 900 && b->last == 999 && b->length() == 100);
    assert(formatContentRange(*b, 1000) == "bytes 900-999/1000");

    // Suffix range: -100
    auto c = parseByteRange("bytes=-100", 1000);
    assert(c && c->first == 900 && c->last == 999 && c->length() == 100);

    // Suffix larger than file size
    auto d = parseByteRange("bytes=-5000", 1000);
    assert(d && d->first == 0 && d->last == 999 && d->length() == 1000);

    // Range clamped to file size
    auto e = parseByteRange("bytes=500-2000", 1000);
    assert(e && e->first == 500 && e->last == 999 && e->length() == 500);

    // Unsatisfiable range (first >= size)
    assert(!parseByteRange("bytes=1000-", 1000));
    assert(!parseByteRange("bytes=1500-2000", 1000));
    assert(formatContentRangeUnsatisfiable(1000) == "bytes */1000");

    // Inverted range (last < first)
    assert(!parseByteRange("bytes=500-400", 1000));

    // Multi-range rejected (out of scope for v0.1)
    assert(!parseByteRange("bytes=0-1,4-5", 1000));

    // Malformed headers
    assert(!parseByteRange("invalid", 1000));
    assert(!parseByteRange("bytes=", 1000));
    assert(!parseByteRange("bytes=-", 1000));
    assert(!parseByteRange("bytes=-0", 1000));
    assert(!parseByteRange("bytes=abc-def", 1000));

    // Zero-sized object
    assert(!parseByteRange("bytes=0-0", 0));

    std::cout << "[Test PASS] Range parser RFC validation\n";
    return 0;
}
