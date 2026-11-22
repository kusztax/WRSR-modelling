// aob_pattern.h - byte patterns with wildcards ("48 8B ?? 05 ?? ?? ?? ?? C3").
//
// Platform-independent on purpose: it is unit-tested natively on the Mac
// (`cmake --preset host-tests`), which is far faster than testing in the game.
//
// Policy, borrowed from the railspeed plugin and worth keeping: a pattern
// that matches ZERO or MORE THAN ONE place is a failure. Two matches mean the
// signature no longer identifies what we think, and guessing is worse than
// doing nothing - the feature must switch itself off and say so in the log.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wrsr::aob {

constexpr int16_t kWild = -1;

struct Pattern
{
    std::vector<int16_t> bytes;  // 0..255, or kWild
    std::string          text;   // as written, for the log
};

// Accepts hex byte pairs separated by whitespace; "?" or "??" is a wildcard.
// Returns false (and leaves `out` unspecified) on malformed input or when the
// pattern has no fixed byte at all.
bool Parse(const char* text, Pattern& out);

// Offsets of every match in [data, data+size), stopping after `limit` hits.
std::vector<size_t> FindAll(const uint8_t* data, size_t size, const Pattern& p,
                            size_t limit = 16);

enum class Result { Unique, NotFound, Ambiguous };

struct Match
{
    Result result = Result::NotFound;
    size_t offset = 0;   // valid when result == Unique
    size_t count  = 0;   // 0, 1, or >=2 (capped)
};

Match FindUnique(const uint8_t* data, size_t size, const Pattern& p);

const char* ToString(Result r);

}  // namespace wrsr::aob
