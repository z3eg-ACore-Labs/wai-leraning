/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_EPISODE_RECOVERY_H
#define WAI_EPISODE_RECOVERY_H

namespace WaiEpisodes
{
enum class Presence
{
    Present,
    Recovering,
    Disconnected,
    LeftInstance
};

inline Presence CheckPresence(bool connected, bool inWorld, bool sameInstance, bool teleporting, bool recovering)
{
    if (!connected)
        return Presence::Disconnected;
    if (inWorld && sameInstance && !teleporting)
        return Presence::Present;
    return recovering ? Presence::Recovering : Presence::LeftInstance;
}
}
#endif
