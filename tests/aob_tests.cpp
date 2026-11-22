// Host-native unit tests for the AOB pattern engine (no framework needed).
//   cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "aob_pattern.h"

using namespace wrsr::aob;

static int g_failed = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                 \
        }                                                               \
    } while (0)

int main()
{
    // ---- parsing
    {
        Pattern p;
        CHECK(Parse("48 8B ?? 05 ? c3", p));
        CHECK(p.bytes.size() == 6);
        CHECK(p.bytes[0] == 0x48 && p.bytes[1] == 0x8B);
        CHECK(p.bytes[2] == kWild && p.bytes[4] == kWild);
        CHECK(p.bytes[5] == 0xC3);

        CHECK(Parse("  0F\tB6\n44  ", p) && p.bytes.size() == 3);
        CHECK(!Parse("", p));
        CHECK(!Parse("?? ??", p));      // no fixed byte
        CHECK(!Parse("4", p));          // half a byte
        CHECK(!Parse("48 8G", p));      // not hex
        CHECK(!Parse("488B", p));       // must be separated
        CHECK(!Parse(nullptr, p));
    }

    // ---- scanning
    const std::vector<uint8_t> data = {
        0x90, 0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0xC3,   // 1: mov rax,[rip+..]; ret
        0x90, 0x48, 0x8B, 0x0D, 0xAA, 0xBB, 0xCC, 0xDD, 0xC3,   // 10: mov rcx,[rip+..]; ret
        0x48, 0x8B};                                            // 18: truncated tail
    {
        Pattern p;
        CHECK(Parse("48 8B 05 ?? ?? ?? ?? C3", p));
        Match m = FindUnique(data.data(), data.size(), p);
        CHECK(m.result == Result::Unique && m.offset == 1 && m.count == 1);
    }
    {
        Pattern p;
        CHECK(Parse("48 8B ?? ?? ?? ?? ?? C3", p));
        Match m = FindUnique(data.data(), data.size(), p);
        CHECK(m.result == Result::Ambiguous && m.count == 2);
        std::vector<size_t> all = FindAll(data.data(), data.size(), p);
        CHECK(all.size() == 2 && all[0] == 1 && all[1] == 10);
    }
    {
        Pattern p;
        CHECK(Parse("48 8B 15", p));
        CHECK(FindUnique(data.data(), data.size(), p).result == Result::NotFound);
    }
    {
        // must not read past the end: "48 8B" at offset 18 is the last 2 bytes
        Pattern p;
        CHECK(Parse("48 8B 05 ?? ?? ?? ?? C3 90", p));
        CHECK(FindAll(data.data(), data.size(), p).size() == 1);
        CHECK(Parse("C3 48 8B ??", p));
        CHECK(FindAll(data.data(), data.size(), p).empty());   // would need byte 20
        CHECK(Parse("C3 48 8B", p));
        CHECK(FindAll(data.data(), data.size(), p).size() == 1);
    }
    {
        // leading wildcard: anchor is not the first byte
        Pattern p;
        CHECK(Parse("?? 8B 0D", p));
        Match m = FindUnique(data.data(), data.size(), p);
        CHECK(m.result == Result::Unique && m.offset == 10);
    }
    {
        // pattern longer than data
        Pattern p;
        CHECK(Parse("90 90 90", p));
        uint8_t two[2] = {0x90, 0x90};
        CHECK(FindAll(two, sizeof(two), p).empty());
    }

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return EXIT_FAILURE; }
    std::printf("aob_tests: all passed\n");
    return EXIT_SUCCESS;
}
