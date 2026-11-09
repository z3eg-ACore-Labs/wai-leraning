/* This file is part of AzerothCore, released under GNU GPL v2 or later. */
#ifndef GAMEPLAY_NAVIGATION_CONTROL_H
#define GAMEPLAY_NAVIGATION_CONTROL_H

#include <cstdint>

class Player;
char const* GameplayRecordingStatus(Player* player);

namespace GameplayNavigation
{
bool Automated(std::uint64_t guid);
bool Managed(std::uint64_t guid);
} // namespace GameplayNavigation

void AddGameplayNavigationScripts();

#endif
