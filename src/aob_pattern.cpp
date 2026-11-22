#include "aob_pattern.h"

#include <cstring>

namespace wrsr::aob {
namespace {

int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

}  // namespace

bool Parse(const char* text, Pattern& out)
{
    out.bytes.clear();
    out.text = text ? text : "";
    if (!text) return false;

    const char* p = text;
    bool anyFixed = false;
    while (*p)
    {
        if (IsSpace(*p)) { ++p; continue; }

        // token = run of non-space chars
        const char* start = p;
        while (*p && !IsSpace(*p)) ++p;
        size_t len = (size_t)(p - start);

        if ((len == 1 && start[0] == '?') || (len == 2 && start[0] == '?' && start[1] == '?'))
        {
            out.bytes.push_back(kWild);
            continue;
        }
        if (len != 2) return false;
        int hi = HexVal(start[0]), lo = HexVal(start[1]);
        if (hi < 0 || lo < 0) return false;
        out.bytes.push_back((int16_t)(hi * 16 + lo));
        anyFixed = true;
    }
    return anyFixed;
}

std::vector<size_t> FindAll(const uint8_t* data, size_t size, const Pattern& p, size_t limit)
{
    std::vector<size_t> hits;
    const size_t n = p.bytes.size();
    if (!data || n == 0 || n > size || limit == 0) return hits;

    // Anchor on the first fixed byte so memchr does the heavy lifting.
    size_t anchor = 0;
    while (anchor < n && p.bytes[anchor] == kWild) ++anchor;
    if (anchor == n) return hits;
    const uint8_t anchorByte = (uint8_t)p.bytes[anchor];

    const size_t last = size - n;  // last valid start offset
    size_t i = 0;
    while (i <= last)
    {
        const void* f = std::memchr(data + i + anchor, anchorByte, (last - i) + 1);
        if (!f) break;
        size_t start = (size_t)((const uint8_t*)f - data) - anchor;

        size_t k = 0;
        for (; k < n; ++k)
        {
            int16_t want = p.bytes[k];
            if (want != kWild && data[start + k] != (uint8_t)want) break;
        }
        if (k == n)
        {
            hits.push_back(start);
            if (hits.size() >= limit) break;
        }
        i = start + 1;
    }
    return hits;
}

Match FindUnique(const uint8_t* data, size_t size, const Pattern& p)
{
    Match m;
    std::vector<size_t> hits = FindAll(data, size, p, 2);
    m.count = hits.size();
    if (hits.size() == 1) { m.result = Result::Unique; m.offset = hits[0]; }
    else if (hits.empty()) m.result = Result::NotFound;
    else                   m.result = Result::Ambiguous;
    return m;
}

const char* ToString(Result r)
{
    switch (r)
    {
    case Result::Unique:    return "unique";
    case Result::NotFound:  return "not found";
    case Result::Ambiguous: return "ambiguous";
    }
    return "?";
}

}  // namespace wrsr::aob
