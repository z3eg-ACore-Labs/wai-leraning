#include "EpisodeRecovery.h"
#include <cassert>
#include <iostream>

int main()
{
    using WaiEpisodes::CheckPresence;
    using WaiEpisodes::Presence;
    assert(CheckPresence(true, true, true, false, false) == Presence::Present);
    assert(CheckPresence(true, true, true, false, true) == Presence::Present);
    // Releasing to the graveyard, loading, and returning are permitted only
    // for a member whose death recovery has already been started.
    assert(CheckPresence(true, true, false, false, true) == Presence::Recovering);
    assert(CheckPresence(true, false, false, true, true) == Presence::Recovering);
    assert(CheckPresence(true, true, true, true, true) == Presence::Recovering);
    assert(CheckPresence(true, true, false, false, false) == Presence::LeftInstance);
    assert(CheckPresence(true, false, false, true, false) == Presence::LeftInstance);
    assert(CheckPresence(true, true, true, true, false) == Presence::LeftInstance);
    // Recovery must never turn an actual disconnect into an indefinite grace period.
    assert(CheckPresence(false, false, false, false, true) == Presence::Disconnected);
    assert(CheckPresence(false, true, true, false, true) == Presence::Disconnected);
    std::cout << "Recovery presence: native graveyard/transfer grace, unexpected exits and disconnect checks passed\n";
}
