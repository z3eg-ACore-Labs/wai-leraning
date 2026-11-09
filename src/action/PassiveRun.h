/* AzerothCore module; GNU GPL v2 or later. */
#pragma once
#include "ClientProgress.h"
#include <set>
#include <string>

namespace Wai
{
struct PassiveRun
{
    ClientProgress progress;
    std::string generation;
    std::set<std::uint64_t> members;
    std::uint32_t map = 389, difficulty = 0;
    std::uint32_t requiredMask = 0, completedMask = 0;
    bool encounterComplete = false;
    bool finalObserved = false;
    bool completionReported = false;
    bool interrupted = false;

    bool Complete() const
    {
        return !interrupted &&
               (encounterComplete || (map == 389 && progress.Count() == ClientProgress::BossEntries.size()));
    }

    bool Encounter(std::uint32_t required, std::uint32_t completed, bool finalEncounter, bool updated, bool owned)
    {
        if (interrupted || !required || !owned)
            return false;
        requiredMask = required;
        completedMask = completed;
        finalObserved = finalObserved || (updated && finalEncounter);
        if (updated && finalObserved && (completed & required) == required)
            encounterComplete = true;
        return encounterComplete;
    }

    void Death(std::uint64_t guid, std::uint32_t entry, bool owned)
    {
        if (interrupted || Complete())
            return;
        if (members.count(guid))
            ++progress.deaths;
        if (owned && map == 389)
            progress.Kill(entry);
    }
};
} // namespace Wai
