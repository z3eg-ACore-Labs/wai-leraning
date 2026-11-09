/* AzerothCore module; GNU GPL v2 or later. */
#pragma once
#include <cstdint>
#include <string>

namespace Wai
{
struct Lease
{
    bool active = false;
    std::string generation;
    uint64_t heartbeat = 0;
    uint64_t observed = 0;
    uint64_t sequence = 0;
    uint64_t consumed = 0;

    bool Fresh(uint64_t now) const
    {
        return active && now >= heartbeat && now - heartbeat <= 3000;
    }
    bool Consume(std::string const& epoch, uint64_t request, uint64_t now)
    {
        if (!Fresh(now) || epoch != generation || request != sequence || request <= consumed || now < observed ||
            now - observed > 1000)
            return false;
        consumed = request;
        heartbeat = now;
        return true;
    }
};
} // namespace Wai
