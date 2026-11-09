/* This file is part of AzerothCore, released under GNU GPL v2 or later. */
#ifndef GAMEPLAY_NAVIGATION_COMMAND_H
#define GAMEPLAY_NAVIGATION_COMMAND_H

#include <cstdint>

namespace GameplayNavigation
{
inline bool ValidCommand(std::uint64_t token, std::uint64_t generation, std::uint64_t sample, std::uint64_t sequence,
                         std::uint64_t consumed, std::uint64_t observed, std::uint64_t now)
{
    return token != 0 && token == generation && sample == sequence && sample > consumed && now >= observed &&
           now - observed <= 1000;
}
} // namespace GameplayNavigation

#endif
