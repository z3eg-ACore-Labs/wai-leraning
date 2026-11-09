/* WAI-owned PlayerBots boundary; GNU GPL v2 or later. */
#ifndef AC_LABS_WAI_PLAYERBOTS_ADAPTER_H
#define AC_LABS_WAI_PLAYERBOTS_ADAPTER_H
#include "Player.h"
#include "WorldSession.h"
#include <concepts>
#if __has_include("Playerbots.h")
#define AC_LABS_WAI_PLAYERBOTS 1
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#else
#define AC_LABS_WAI_PLAYERBOTS 0
#endif
#if __has_include("QolClientControl.h")
#include "QolClientControl.h"
#endif
#if AC_LABS_WAI_PLAYERBOTS && defined(AC_PLAYERBOTS_QOL_CLIENT_CONTROL_API) && AC_PLAYERBOTS_QOL_CLIENT_CONTROL_API == 1
#define AC_LABS_WAI_CLIENT_CONTROL 1
#else
#define AC_LABS_WAI_CLIENT_CONTROL 0
#endif

namespace WaiPlayerbotsAdapter
{
enum class Mode { Disabled, Manual, Automatic };

template <typename Session>
bool HumanSession(Session const* session)
{
    if (!session) return false;
    if constexpr (requires { { session->IsHeadless() } -> std::convertible_to<bool>; })
        return !session->IsHeadless();
    else if constexpr (requires { { session->IsBot() } -> std::convertible_to<bool>; })
        return !session->IsBot();
    else
        return !AC_LABS_WAI_PLAYERBOTS; // Stock core has no PlayerBots headless sessions.
}
inline bool HasAI(Player* player)
{
#if AC_LABS_WAI_PLAYERBOTS
    return player && PlayerbotsMgr::instance().GetPlayerbotAI(player);
#else
    return false;
#endif
}
inline Mode GetMode(Player* player)
{
#if AC_LABS_WAI_CLIENT_CONTROL
    auto* ai = player ? PlayerbotsMgr::instance().GetPlayerbotAI(player) : nullptr;
    auto const mode = QolClientControl::Get(ai);
    if (mode == QolClientControl::Mode::Manual) return Mode::Manual;
    if (mode == QolClientControl::Mode::Automatic) return Mode::Automatic;
#endif
    return Mode::Disabled;
}
inline bool Available()
{
#if AC_LABS_WAI_CLIENT_CONTROL
    return QolClientControl::reader && QolClientControl::setter;
#else
    return false;
#endif
}
inline bool SetMode(Player* player, Mode mode)
{
#if AC_LABS_WAI_CLIENT_CONTROL
    auto* ai = player ? PlayerbotsMgr::instance().GetPlayerbotAI(player) : nullptr;
    return mode != Mode::Disabled && QolClientControl::Set(ai,
        mode == Mode::Manual ? QolClientControl::Mode::Manual : QolClientControl::Mode::Automatic, player);
#else
    return false;
#endif
}
inline bool Manual(Player* player) { return !HasAI(player) || SetMode(player, Mode::Manual); }
inline bool AttachManual(Player* player)
{
#if AC_LABS_WAI_CLIENT_CONTROL
    if (!Available() || !player || !HumanSession(player->GetSession()) ||
        sPlayerbotAIConfig.SelfBotLevel == 0 ||
        (sPlayerbotAIConfig.SelfBotLevel == 1 && !player->CanBeGameMaster())) return false;
    auto* ai = PlayerbotsMgr::instance().GetPlayerbotAI(player);
    bool const created = !ai;
    if (!ai)
    {
        PlayerbotsMgr::instance().AddPlayerbotData(player, true);
        ai = PlayerbotsMgr::instance().GetPlayerbotAI(player);
    }
    if (ai && SetMode(player, Mode::Manual)) return true;
    if (created && ai) delete ai;
#endif
    return false;
}
template <typename Session>
bool HeadlessSession(Session const* session)
{
    if (!session) return false;
    if constexpr (requires { { session->IsHeadless() } -> std::convertible_to<bool>; })
        return session->IsHeadless();
    else if constexpr (requires { { session->IsBot() } -> std::convertible_to<bool>; })
        return session->IsBot();
    else
        return false;
}

}
#endif
