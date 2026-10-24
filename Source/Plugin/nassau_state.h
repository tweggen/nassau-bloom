#pragma once
// NassauBloom state framing / migration helper.
//
// This header is deliberately DEPENDENCY-FREE (no iPlug2 includes) so the pure
// framing/migration math can be unit-tested standalone. It describes the layout
// of the versioned state chunk NassauBloomPlugin writes, and the tolerant rules for
// reading it back.
//
// Chunk layout (little-endian, as written by IByteChunk::Put):
//   [0..3]   magic  : 4 bytes, {'N','s','B','l'}
//   [4..7]   uint32 : stateVersion (starts at 1)
//   [8..11]  uint32 : storedParamCount (== NParams() at save time)
//   [12..]   double : storedParamCount parameter values, in enum order
//
// APPEND-ONLY PARAM DISCIPLINE
// ----------------------------
// Params are serialized positionally, so their on-disk meaning is their enum
// index. To stay forward/backward compatible NEW params must be APPENDED at the
// end of the enum (just before kNumParams) and NEVER reordered or inserted in
// the middle. Given that discipline:
//   * An OLDER save (storedCount < currentCount) is read for the params it has;
//     the newer, appended params keep their defaults.
//   * A NEWER save (storedCount > currentCount) loaded by an OLDER build reads
//     the params it understands and SKIPS the tail, never reading past the end.

#include <cstdint>

namespace nassau_state {

// 4-byte chunk magic: 'N','s','B','l'. Kept as bytes (not a packed int) so the
// on-disk order is unambiguous regardless of host endianness.
inline constexpr unsigned char kMagic[4] = {'N', 's', 'B', 'l'};

// State format version. Bump when the framing (not just an appended param)
// changes. Appending a param does NOT require a version bump thanks to the
// tolerant read below.
inline constexpr uint32_t kStateVersion = 1;

// Size on disk of a single serialized parameter value (iPlug2 stores each param
// as a double via IByteChunk::Put).
inline constexpr int kParamSize = static_cast<int>(sizeof(double));

// Returns true if the 4 bytes at `p` are the NassauBloom magic.
inline bool MagicMatches(const unsigned char* p)
{
    return p[0] == kMagic[0] && p[1] == kMagic[1] &&
           p[2] == kMagic[2] && p[3] == kMagic[3];
}

// Pure migration plan for reading a param block.
struct FramePlan {
    int paramsToRead;   // params actually read from the chunk into params[0..)
    int paramsToSkip;   // stored-but-unknown tail params to skip over (advance pos)
    int skipBytes;      // bytes to advance past the skipped tail
    // Any current params with index >= paramsToRead keep their defaults.
};

// Given how many params the chunk stored and how many this build has, decide how
// many to read and how many trailing (unknown, newer) params to skip.
inline FramePlan PlanUnserialize(uint32_t storedCount, uint32_t currentCount)
{
    const uint32_t toRead = storedCount < currentCount ? storedCount : currentCount;
    const uint32_t toSkip = storedCount > currentCount ? (storedCount - currentCount) : 0u;
    FramePlan plan{};
    plan.paramsToRead = static_cast<int>(toRead);
    plan.paramsToSkip = static_cast<int>(toSkip);
    plan.skipBytes    = static_cast<int>(toSkip) * kParamSize;
    return plan;
}

} // namespace nassau_state
