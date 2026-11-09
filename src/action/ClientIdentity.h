/* AzerothCore module; GNU GPL v2 or later. */
#pragma once
#include <array>
#include <cstdint>
#include <sstream>
#include <string>

namespace Wai
{
struct ClientIdentity
{
    std::string marker;
    std::array<unsigned, 4> bindings{};
    std::uint64_t heartbeat = 0;

    static bool Key(unsigned key) { return (key >= 65 && key <= 90) || (key >= 37 && key <= 40); }

    bool Parse(std::string const& payload)
    {
        std::istringstream input(payload);
        std::string extra;
        ClientIdentity candidate;
        if (!(input >> candidate.marker >> candidate.bindings[0] >> candidate.bindings[1] >> candidate.bindings[2] >>
              candidate.bindings[3]) || (input >> extra) || candidate.marker.size() != 16 ||
            candidate.marker.find_first_not_of("0123456789abcdef") != std::string::npos)
            return false;
        for (unsigned i = 0; i < candidate.bindings.size(); ++i)
        {
            if (!Key(candidate.bindings[i]))
                return false;
            for (unsigned j = 0; j < i; ++j)
                if (candidate.bindings[j] == candidate.bindings[i])
                    return false;
        }
        *this = candidate;
        return true;
    }

    bool Fresh(std::uint64_t now) const { return now >= heartbeat && now - heartbeat <= 3000; }
};
} // namespace Wai
