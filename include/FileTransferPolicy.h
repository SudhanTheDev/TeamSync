#pragma once
#include <cstdint>
namespace teamsync {
// Transfers are streamed in bounded chunks; these limits protect local disk
// usage, rather than compensating for an in-memory whole-workspace packet.
inline constexpr std::uint64_t maxManagedFileBytes = 2ULL * 1024 * 1024 * 1024;
inline constexpr std::uint64_t maxCompletionBatchBytes = 4ULL * 1024 * 1024 * 1024;
}
