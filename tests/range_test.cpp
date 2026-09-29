#include "maia/vod/range.hpp"
#include <cassert>
int main() {
    using maia::vod::parseByteRange;
    auto a = parseByteRange("bytes=0-99", 1000);
    assert(a && a->first == 0 && a->last == 99 && a->length() == 100);
    auto b = parseByteRange("bytes=900-", 1000);
    assert(b && b->first == 900 && b->last == 999);
    auto c = parseByteRange("bytes=-100", 1000);
    assert(c && c->first == 900 && c->last == 999);
    assert(!parseByteRange("bytes=1000-", 1000));
    assert(!parseByteRange("bytes=0-1,4-5", 1000));
}
