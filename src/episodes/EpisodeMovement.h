/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_EPISODE_MOVEMENT_H
#define WAI_EPISODE_MOVEMENT_H
#include "BoundaryRules.h"
#include "ObjectGuid.h"
#include <array>
#include <string>

class Player;
namespace WaiEpisodes::Integrity
{
bool Install();
void Shutdown();
void SetRoster(std::array<ObjectGuid, 5> const& roster);
void ClearRoster();
void PermitEntry(ObjectGuid player);
void PermitRecovery(ObjectGuid player, Player* anchor, float radius = 5.0f);
bool Ready(Player* player);
BoundaryReading InspectBoundary(Player* player, float tolerance);
std::string TakeViolation();
} // namespace WaiEpisodes::Integrity
#endif
