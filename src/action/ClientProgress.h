/* AzerothCore module; GNU GPL v2 or later. */
#pragma once
#include <array>
#include <cstdint>

namespace Wai
{
// Reward labels, never movement destinations. Shared identities with the episode runner.
struct ClientProgress
{
    static constexpr std::array<std::uint32_t, 4> BossEntries{11517, 11520, 11518, 11519};
    std::array<bool, 4> killed{};
    std::uint32_t deaths = 0;
    std::uint64_t started = 0;

    bool Kill(std::uint32_t entry)
    {
        for (std::size_t i = 0; i < BossEntries.size(); ++i)
            if (BossEntries[i] == entry && !killed[i])
            {
                killed[i] = true;
                return true;
            }
        return false;
    }

    unsigned Count() const
    {
        unsigned count = 0;
        for (bool value : killed)
            count += value ? 1 : 0;
        return count;
    }
};
} // namespace Wai
