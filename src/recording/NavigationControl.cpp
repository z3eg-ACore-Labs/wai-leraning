/* This file is part of AzerothCore, released under GNU GPL v2 or later. */
#include "NavigationControl.h"
#include "Chat.h"
#include "Config.h"
#include "Map.h"
#include "MotionMaster.h"
#include "NavigationCommand.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "WaiPlayerbotsAdapter.h"
#include "Log.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <locale>
#include <mutex>
#include <sstream>

using namespace Acore::ChatCommands;

namespace
{
std::mutex controlMutex;
std::atomic<uint64> controlled{0};
std::atomic<uint64> managed{0};
std::atomic<bool> enabled{false};
ObjectGuid owner;
uint32 mapId = 0;
uint32 instanceId = 0;
uint64 generation = 0;
uint64 sequence = 0;
uint64 heartbeat = 0;
uint64 observed = 0;
uint64 consumed = 0;
bool running = false;
bool requestedStop = false;
bool nativeCombatActive = false;
bool hasStep = false;
float stepX = 0.0f;
float stepY = 0.0f;
float stepZ = 0.0f;
uint64 stepTime = 0;

uint64 Clock()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool Eligible(Player* player)
{
    return player && player->GetSession() && WaiPlayerbotsAdapter::HumanSession(player->GetSession()) && player->IsInWorld() &&
           !player->IsDuringRemoveFromWorld() && !player->IsBeingTeleported() &&
           !player->GetSession()->IsLoggingOut() && player->GetMap()->IsDungeon() && player->IsAlive();
}

bool Manual(Player* player) { return WaiPlayerbotsAdapter::Manual(player); }

void Release(Player* player)
{
    // Keep recording suppressed until all automation has relinquished movement.
    if (player)
    {
        Manual(player);
        if (player->IsInWorld() && !player->IsBeingTeleported() && !player->IsDuringRemoveFromWorld())
        {
            player->AttackStop();
            player->StopMoving();
            player->GetMotionMaster()->Clear();
        }
    }
    running = false;
    nativeCombatActive = false;
    hasStep = false;
    requestedStop = false;
    controlled = 0;
    heartbeat = 0;
}

bool Reply(ChatHandler* handler, char const* text)
{
    handler->SendSysMessage(text);
    return true;
}

class NavigationCommands : public CommandScript
{
  public:
    NavigationCommands() : CommandScript("GameplayNavigationCommands") {}
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commands{{"wainav", Handle, SEC_PLAYER, Console::Yes}};
        return commands;
    }

    static bool Handle(ChatHandler* handler, char const* arguments)
    {
        std::istringstream input(arguments ? arguments : "");
        input.imbue(std::locale::classic());
        std::string verb;
        input >> verb;
        std::lock_guard lock(controlMutex);
        Player* caller = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!handler->IsConsole())
        {
            if (!caller)
                return false;
            if (verb == "status")
                return Reply(handler, controlled == caller->GetGUID().GetRawValue()
                                          ? (running ? "Navigation: model running (native combat)."
                                                     : "Navigation: waiting for a compatible model/helper.")
                                          : GameplayRecordingStatus(caller));
            if (verb == "record" || verb == "stop")
            {
                if (controlled && controlled != caller->GetGUID().GetRawValue())
                    return Reply(handler, "Navigation: another character owns control.");
                if (controlled)
                    requestedStop = true;
                else
                {
                    if (!Manual(caller))
                        return Reply(handler, "Navigation: manual handoff unavailable; use client manual/off before recording.");
                    managed = caller->GetGUID().GetRawValue();
                }
                return Reply(handler, "Navigation: returning to human control and recording on the next game tick.");
            }
            if (verb != "run")
                return Reply(handler, "Use .wainav run, .wainav record, or .wainav status.");
            if (!enabled || !Eligible(caller) || caller->IsInCombat() || caller->GetTradeData() || controlled)
                return Reply(
                    handler,
                    "Navigation unavailable: enable configuration, leave combat/trade, and release existing control.");
            if (!WaiPlayerbotsAdapter::AttachManual(caller))
                return Reply(handler, "Navigation unavailable: requires PlayerBots, QoL client-control API v1, "
                             "enabled QoL client control and native SelfBot permission.");
            owner = caller->GetGUID();
            managed = owner.GetRawValue();
            mapId = caller->GetMapId();
            instanceId = caller->GetInstanceId();
            generation = (uint64(urand(1, 0xffffffff)) << 32) | urand(1, 0xffffffff);
            sequence = consumed = 0;
            heartbeat = Clock();
            observed = 0;
            running = requestedStop = false;
            nativeCombatActive = false;
            hasStep = false;
            controlled = owner.GetRawValue();
            return Reply(handler,
                         "Navigation: requesting compatible trained model. Manual movement or .wainav record cancels.");
        }
        // Console/SOAP requires the existing administrator authorization.
        if (!controlled)
            return Reply(handler, "NAV_INACTIVE");
        Player* player = ObjectAccessor::FindPlayer(owner);
        if (!Eligible(player) || player->GetMapId() != mapId || player->GetInstanceId() != instanceId ||
            requestedStop || Clock() - heartbeat > 3000)
        {
            requestedStop = true; // Map-thread update performs movement/AI cleanup.
            return Reply(handler, "NAV_INACTIVE");
        }
        if (verb == "observe")
        {
            heartbeat = observed = Clock();
            ++sequence;
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << "{\"schema\":1,\"contract\":\"navigation_delta_v1\",\"generation\":" << generation
                << ",\"seq\":" << sequence << ",\"map\":" << mapId << ",\"instance\":" << instanceId
                << ",\"difficulty\":" << uint32(player->GetMap()->GetDifficulty())
                << ",\"running\":" << (running ? "true" : "false")
                << ",\"combat\":" << (player->IsInCombat() ? "true" : "false")
                << ",\"busy\":" << (player->IsNonMeleeSpellCast(false) || player->GetTradeData() ? "true" : "false")
                << ",\"position\":[" << player->GetPositionX() << ',' << player->GetPositionY() << ','
                << player->GetPositionZ() << ',' << player->GetOrientation() << "]}";
            return Reply(handler, out.str().c_str());
        }
        uint64 token = 0;
        uint64 sample = 0;
        input >> token >> sample;
        if (!input ||
            !GameplayNavigation::ValidCommand(token, generation, sample, sequence, consumed, observed, Clock()))
            return Reply(handler, "NAV_REJECTED stale command");
        consumed = sample;
        if (verb == "release")
        {
            requestedStop = true;
            return Reply(handler, "NAV_RELEASE_REQUESTED");
        }
        if (verb == "ready")
        {
            running = true;
            return Reply(handler, "NAV_READY");
        }
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (WaiPlayerbotsAdapter::GetMode(player) != WaiPlayerbotsAdapter::Mode::Manual)
            return Reply(handler, "NAV_REJECTED native combat has not yielded movement");
        input >> x >> y >> z;
        std::string extra;
        if (verb != "step" || !running || !input || (input >> extra) || !std::isfinite(x) || !std::isfinite(y) ||
            !std::isfinite(z) || player->GetExactDist(x, y, z) > 4.0f || player->IsInCombat() ||
            player->IsNonMeleeSpellCast(false) || player->GetTradeData())
            return Reply(handler, "NAV_REJECTED invalid step or native combat busy");
        stepX = x;
        stepY = y;
        stepZ = z;
        stepTime = Clock();
        hasStep = true;
        return Reply(handler, "NAV_STEP_QUEUED");
    }
};

class NavigationLifecycle : public PlayerScript
{
  public:
    NavigationLifecycle()
        : PlayerScript("GameplayNavigationLifecycle", {PLAYERHOOK_ON_BEFORE_UPDATE, PLAYERHOOK_ON_LOGOUT})
    {
    }
    void OnPlayerBeforeUpdate(Player* player, uint32) override
    {
        if (controlled != player->GetGUID().GetRawValue())
            return;
        std::lock_guard lock(controlMutex);
        if (!controlled || player->GetGUID() != owner)
            return;
        if (!enabled || requestedStop || !Eligible(player) || player->GetMapId() != mapId ||
            player->GetInstanceId() != instanceId || Clock() - heartbeat > 3000)
        {
            Release(player);
            return;
        }
        auto const mode = WaiPlayerbotsAdapter::GetMode(player);
        if (!WaiPlayerbotsAdapter::HasAI(player) || mode == WaiPlayerbotsAdapter::Mode::Disabled)
        {
            Release(player);
            return;
        }
        // Honor takeover through the normal client commands and direct movement input.
        if ((mode == WaiPlayerbotsAdapter::Mode::Manual) == nativeCombatActive)
        {
            Release(player);
            return;
        }
        bool const nativeCombat = running && player->IsInCombat() && !player->GetTradeData();
        if (nativeCombatActive != nativeCombat)
        {
            if (!WaiPlayerbotsAdapter::SetMode(player, nativeCombat ? WaiPlayerbotsAdapter::Mode::Automatic
                                                                  : WaiPlayerbotsAdapter::Mode::Manual))
            {
                Release(player);
                return;
            }
            nativeCombatActive = nativeCombat;
        }
        if (hasStep)
        {
            hasStep = false;
            if (running && !player->IsInCombat() && !player->IsNonMeleeSpellCast(false) && !player->GetTradeData() &&
                Clock() - stepTime <= 1000 && player->GetExactDist(stepX, stepY, stepZ) <= 4.0f)
            {
                PathGenerator path(player);
                path.SetSlopeCheck(true);
                if (!path.CalculatePath(stepX, stepY, stepZ, false) || path.GetPathType() != PATHFIND_NORMAL ||
                    path.GetPath().size() < 2 || path.getPathLength() > 6.0f)
                {
                    Release(player);
                    return;
                }
                // This copies the validated path instead of using MovePoint's no-path fallback.
                Movement::PointsArray points = path.GetPath();
                player->GetMotionMaster()->MoveSplinePath(&points);
            }
        }
    }
    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard lock(controlMutex);
        if (player->GetGUID() == owner)
            Release(player);
        if (managed == player->GetGUID().GetRawValue())
            managed = 0;
    }
};

class NavigationPackets : public ServerScript
{
  public:
    NavigationPackets() : ServerScript("GameplayNavigationManualOverride", {SERVERHOOK_CAN_PACKET_RECEIVE}) {}
    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!controlled || !session || !session->GetPlayer())
            return true;
        uint16 opcode = packet.GetOpcode();
        bool const manual = opcode == MSG_MOVE_START_FORWARD || opcode == MSG_MOVE_START_BACKWARD ||
                            opcode == MSG_MOVE_START_STRAFE_LEFT || opcode == MSG_MOVE_START_STRAFE_RIGHT ||
                            opcode == MSG_MOVE_START_TURN_LEFT || opcode == MSG_MOVE_START_TURN_RIGHT ||
                            opcode == MSG_MOVE_JUMP || opcode == CMSG_CAST_SPELL || opcode == CMSG_ATTACKSWING ||
                            opcode == CMSG_USE_ITEM;
        if (manual)
        {
            std::lock_guard lock(controlMutex);
            if (controlled == session->GetPlayer()->GetGUID().GetRawValue())
                requestedStop = true;
        }
        return true;
    }
};

class NavigationConfiguration : public WorldScript
{
  public:
    NavigationConfiguration() : WorldScript("GameplayNavigationConfiguration", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) {}
    void OnAfterConfigLoad(bool) override
    {
        std::lock_guard lock(controlMutex);
        enabled = sConfigMgr->GetOption<bool>("GameplayNavigation.Enable", true);
        if (enabled && !WaiPlayerbotsAdapter::Available())
            LOG_WARN("module.gameplay-observer", "Learned-navigation handoff unavailable: PlayerBots/QoL client-control API v1 missing. Recorder remains independent.");
        if (controlled)
            requestedStop = true;
    }
};
} // namespace

bool GameplayNavigation::Automated(std::uint64_t guid)
{
    return guid != 0 && controlled == guid;
}
bool GameplayNavigation::Managed(std::uint64_t guid)
{
    return guid != 0 && managed == guid;
}

void AddGameplayNavigationScripts()
{
    new NavigationCommands();
    new NavigationLifecycle();
    new NavigationPackets();
    new NavigationConfiguration();
}
