/* AzerothCore module; GNU GPL v2 or later. */
#include "ActionLease.h"
#include "CellImpl.h"
#include "Chat.h"
#include "ClientProgress.h"
#include "ClientIdentity.h"
#include "Config.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "PassiveRun.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "WaiPlayerbotsAdapter.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <list>
#include <locale>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>

using namespace Acore::ChatCommands;
namespace
{
constexpr std::array<uint32, 6> LearnedSpells{779, 5232, 5487, 6808, 8618, 16857};
constexpr float SearchRange = 15.0f;
constexpr float MaxMove = 8.0f;
std::mutex mutex;
Wai::Lease lease;
ObjectGuid ownerGuid, observedTarget;
uint32 mapId = 43, instanceId = 0;
std::atomic<bool> enabled{false}, clientEnabled{false};
uint32 clientMapId = 389;
bool clientMode = false;
std::atomic<uint64> activeOwner{0};
bool allowRoute = false, stopping = false;
Wai::ClientProgress clientProgress;
std::set<ObjectGuid> clientMembers;
struct PassiveWatch
{
    std::string token;
    uint64 heartbeat = 0, sequence = 0;
    uint32 lastInstance = 0;
    std::map<uint32, Wai::PassiveRun> runs;
};
std::map<uint64, PassiveWatch> passiveWatches;
std::map<uint64, Wai::ClientIdentity> clientIdentities;

uint64 Now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Stop(Player* player, bool interrupt = true)
{
    bool controlled = (lease.active || stopping || activeOwner.load() != 0) && !clientMode;
    clientMode = false;
    lease.active = false;
    activeOwner = 0;
    stopping = false;
    observedTarget.Clear();
    if (controlled && player && player->IsInWorld())
    {
        player->AttackStop();
        player->GetMotionMaster()->Clear();
        player->StopMoving();
        if (interrupt)
            player->InterruptNonMeleeSpells(false);
    }
}

bool Eligible(Player* player, bool client = false)
{
    if (!(client ? clientEnabled.load() : enabled.load()) || !player || !player->IsInWorld() || !player->GetSession() ||
        !WaiPlayerbotsAdapter::HumanSession(player->GetSession()) || (!client && !player->IsAlive()) ||
        (!client && player->getClass() != CLASS_DRUID) || player->IsBeingTeleported() ||
        player->GetMapId() != (client ? clientMapId : mapId) || !player->GetMap()->IsDungeon())
        return false;
    if (WaiPlayerbotsAdapter::HasAI(player))
        return false;
    return true;
}

bool EligibleHuman(Player* player)
{
    if (!clientEnabled.load() || !player || !player->IsInWorld() || !player->GetSession() ||
        !WaiPlayerbotsAdapter::HumanSession(player->GetSession()) || player->IsBeingTeleported() || !player->GetMap()->IsDungeon() ||
        player->GetMap()->IsRaid())
        return false;
    if (WaiPlayerbotsAdapter::HasAI(player))
        return false;
    return true;
}

uint32 RequiredEncounters(DungeonEncounterList const* encounters)
{
    uint32 required = 0;
    bool hasFinal = false;
    if (!encounters || encounters->empty())
        return 0;
    for (DungeonEncounter const* encounter : *encounters)
    {
        if (!encounter || !encounter->dbcEntry || encounter->dbcEntry->encounterIndex >= 32)
            return 0;
        required |= uint32(1) << encounter->dbcEntry->encounterIndex;
        hasFinal = hasFinal || encounter->lastEncounterDungeon != 0;
    }
    return hasFinal ? required : 0;
}

std::vector<Player*> Party(Player* player)
{
    std::vector<Player*> result{player};
    if (Group* group = player->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref && result.size() < 5; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member != player && member->IsInWorld() && member->GetMap() == player->GetMap())
                    result.push_back(member);
    return result;
}

bool OwnLoot(Player* player, Creature* creature)
{
    return creature && (creature->GetLootRecipientGUID() == player->GetGUID() ||
                        (player->GetGroup() && creature->GetLootRecipientGroup() == player->GetGroup()));
}

Unit* ChooseTarget(Player* player)
{
    if (Unit* victim = player->GetVictim())
        if (victim->IsAlive() && player->IsValidAttackTarget(victim))
            return victim;
    if (!player->IsInCombat() && player->GetShapeshiftForm() == FORM_NONE)
        for (Player* member : Party(player))
            if (member->IsAlive() && !member->HasAura(5232) && player->IsWithinDistInMap(member, 30.0f))
                return member;
    if (Unit* selected = player->GetSelectedUnit())
        if (selected->ToCreature() && selected->IsAlive() && player->IsValidAttackTarget(selected) &&
            player->IsWithinDistInMap(selected, SearchRange) && player->IsWithinLOSInMap(selected))
            return selected;
    std::list<Unit*> units;
    // The stock AnyUnitInObjectRangeCheck excludes corpses.
    auto check = [player](Unit* unit) { return player->IsWithinDistInMap(unit, SearchRange); };
    Acore::UnitListSearcher<decltype(check)> searcher(player, units, check);
    Cell::VisitObjects(player, searcher, SearchRange);
    Unit* nearestEnemy = nullptr;
    Unit* nearestCorpse = nullptr;
    for (Unit* unit : units)
        if (Creature* creature = unit->ToCreature())
        {
            if (!player->IsWithinLOSInMap(creature))
                continue;
            if (creature->IsAlive() && player->IsValidAttackTarget(creature))
            {
                if (!nearestEnemy || player->GetDistance(creature) < player->GetDistance(nearestEnemy))
                    nearestEnemy = creature;
            }
            else if (!creature->IsAlive() && OwnLoot(player, creature) &&
                     (creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE) ||
                      (player->HasSpell(8618) && creature->HasUnitFlag(UNIT_FLAG_SKINNABLE))))
            {
                if (!nearestCorpse || player->GetDistance(creature) < player->GetDistance(nearestCorpse))
                    nearestCorpse = creature;
            }
        }
    if (!player->IsInCombat() && nearestCorpse)
        return nearestCorpse;
    return nearestEnemy ? nearestEnemy : player;
}

float Health(Unit* unit)
{
    return unit ? float(unit->GetHealth()) / std::max<uint32>(1, unit->GetMaxHealth()) : 0.0f;
}

std::array<float, 16> Features(Player* player, Unit* selected)
{
    auto party = Party(player);
    bool ally = std::find(party.begin(), party.end(), selected) != party.end();
    float mean = 0.0f, lowest = 1.0f, combat = 0.0f, missingMark = 0.0f;
    for (Player* member : party)
    {
        mean += Health(member);
        lowest = std::min(lowest, Health(member));
        combat += member->IsInCombat() ? 1.0f : 0.0f;
        missingMark += member->HasAura(5232) ? 0.0f : 1.0f;
    }
    float count = float(party.size());
    std::array<float, 16> result{Health(player),
                                 float(player->GetPower(player->getPowerType())) /
                                     std::max<uint32>(1, player->GetMaxPower(player->getPowerType())),
                                 player->getPowerType() == POWER_RAGE ? 1.0f : 0.0f,
                                 player->IsInCombat() ? 1.0f : 0.0f,
                                 player->HasAura(5487) ? 1.0f : 0.0f,
                                 ally ? 1.0f : 0.0f,
                                 selected == player ? 1.0f : 0.0f,
                                 selected && !selected->IsAlive() ? 1.0f : 0.0f,
                                 Health(selected),
                                 selected ? std::min(1.0f, player->GetExactDist(selected) / 100.0f) : 0.0f,
                                 mean / count,
                                 lowest,
                                 combat / count,
                                 missingMark / count,
                                 selected && selected->HasAura(5232) ? 1.0f : 0.0f,
                                 player->IsNonMeleeSpellCast(false) ? 1.0f : 0.0f};
    return result;
}

Unit* SpellTarget(Player* player, Unit* selected, uint32 spell)
{
    if (spell == 5487 || spell == 779)
        return player;
    return selected;
}

bool CanCast(Player* player, Unit* selected, uint32 id)
{
    if (!selected || !player->HasSpell(id) || player->HasSpellCooldown(id) || player->IsNonMeleeSpellCast(false))
        return false;
    bool ally = selected == player ||
                (selected->ToPlayer() && player->GetGroup() && selected->ToPlayer()->GetGroup() == player->GetGroup());
    if ((id == 5232 && (!ally || selected->HasAura(id) || player->GetShapeshiftForm() != FORM_NONE)) ||
        (id == 5487 && player->HasAura(id)) ||
        (id == 8618 && (selected->IsAlive() || !selected->ToCreature() || !OwnLoot(player, selected->ToCreature()))) ||
        ((id == 6808 || id == 16857 || id == 779) && (!selected->IsAlive() || !player->IsValidAttackTarget(selected))))
        return false;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
    if (!info)
        return false;
    Spell probe(player, info, TRIGGERED_NONE);
    probe.m_targets.SetUnitTarget(SpellTarget(player, selected, id));
    return probe.CheckCast(true) == SPELL_CAST_OK;
}

bool Reply(ChatHandler* handler, char const* text)
{
    handler->SendSysMessage(text);
    return true;
}

std::string ClientObservation(Player* player, Wai::ClientProgress const& progress, std::string const& generation,
                              uint64 sequence)
{
    auto party = Party(player);
    float mean = 0, minimum = 1, alive = 0, combat = 0;
    for (auto member : party)
    {
        float health = member->GetHealthPct() / 100.0f;
        mean += health / party.size();
        minimum = std::min(minimum, health);
        alive += member->IsAlive() ? 1.0f / party.size() : 0;
        combat += member->IsInCombat() ? 1.0f / party.size() : 0;
    }
    bool supported = !player->IsFlying() && !player->IsInWater() && !player->GetVehicle() && !player->GetTransport() &&
                     !player->IsInFlight() && !player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING);
    bool busy = player->IsNonMeleeSpellCast(false);
    bool ready = supported && !busy && combat == 0 && alive > 0.99f;
    unsigned bosses = progress.Count();
    bool rewardAvailable = player->GetMapId() == 389;
    char const* outcome = rewardAvailable && bosses == 4 ? "completed_all_bosses"
                          : !player->IsAlive()           ? "player_died"
                                                         : "running";
    std::array<float, 16> values{player->GetPositionX() / 1000.0f,
                                 player->GetPositionY() / 1000.0f,
                                 player->GetPositionZ() / 1000.0f,
                                 std::sin(player->GetOrientation()),
                                 std::cos(player->GetOrientation()),
                                 player->GetHealthPct() / 100.0f,
                                 float(player->GetPower(player->getPowerType())) /
                                     std::max<uint32>(1, player->GetMaxPower(player->getPowerType())),
                                 player->IsInCombat() ? 1.0f : 0.0f,
                                 mean,
                                 minimum,
                                 alive,
                                 combat,
                                 float(bosses) / 4.0f,
                                 std::min(1.0f, float(Now() - progress.started) / 3600000.0f),
                                 ready ? 1.0f : 0.0f,
                                 player->isMoving() ? 1.0f : 0.0f};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema\":1,\"backend\":\"client_input_probe\",\"generation\":\"" << generation
        << "\",\"seq\":" << sequence << ",\"player_guid\":\"" << player->GetGUID().GetRawValue()
        << "\",\"map\":" << player->GetMapId() << ",\"instance\":" << player->GetInstanceId()
        << ",\"boss_progress_available\":" << (rewardAvailable ? "true" : "false")
        << ",\"episode_time_available\":true,\"elapsed_ms\":" << Now() - progress.started << ",\"bosses\":" << bosses
        << ",\"deaths\":" << progress.deaths << ",\"outcome\":\"" << outcome
        << "\",\"supported\":" << (supported ? "true" : "false") << ",\"ready\":" << (ready ? "true" : "false")
        << ",\"features\":[";
    for (std::size_t i = 0; i < values.size(); ++i)
        out << (i ? "," : "") << values[i];
    out << "]}";
    return out.str();
}

// Passive polling never touches the action lease or movement. One observer per character.
bool ObserveHuman(ChatHandler* handler, std::istringstream& input)
{
    std::string guidText, token, extra;
    input >> guidText >> token;
    if (!handler->IsConsole() || guidText.empty() || guidText.size() > 20 || token.empty() || token.size() > 32 ||
        guidText.find_first_not_of("0123456789") != std::string::npos ||
        token.find_first_not_of("0123456789") != std::string::npos || (input >> extra))
        return Reply(handler, "WAI_REJECTED: passive watch requires a character GUID and observer token via SOAP.");
    uint64 guid = 0;
    try
    {
        guid = std::stoull(guidText);
    }
    catch (std::exception const&)
    {
        return Reply(handler, "WAI_REJECTED: invalid character GUID.");
    }
    uint64 now = Now();
    for (auto it = passiveWatches.begin(); it != passiveWatches.end();)
        if (now - it->second.heartbeat > 60000)
            it = passiveWatches.erase(it);
        else
            ++it;
    auto found = passiveWatches.find(guid);
    if (found == passiveWatches.end() && passiveWatches.size() >= 16)
        return Reply(handler, "WAI_REJECTED: passive observer capacity reached.");
    auto& watch = passiveWatches[guid];
    if (!watch.token.empty() && watch.token != token && now - watch.heartbeat <= 3000)
        return Reply(handler, "WAI_REJECTED: another passive observer owns this character.");
    if (watch.token != token || now - watch.heartbeat > 3000)
    {
        // Lost continuity cannot become a completed demonstration later.
        for (auto& [id, run] : watch.runs)
            run.interrupted = true;
        watch.token = token;
    }
    watch.heartbeat = now;
    ++watch.sequence;
    Player* player = ObjectAccessor::FindPlayer(ObjectGuid(guid));
    bool conflict = lease.active || stopping;
    conflict = conflict || (player && WaiPlayerbotsAdapter::HasAI(player));
    bool eligible = !conflict && EligibleHuman(player);
    char const* state = !player ? "offline" : conflict ? "blocked" : eligible ? "active" : "outside_or_ineligible";
    // Deliver an already-earned completion before beginning a different instance.
    auto priorRun = watch.runs.find(watch.lastInstance);
    if (eligible && watch.lastInstance != player->GetInstanceId() && priorRun != watch.runs.end() &&
        priorRun->second.Complete() && !priorRun->second.completionReported)
    {
        eligible = false;
        state = "completion_pending";
    }
    if (eligible)
    {
        uint32 instance = player->GetInstanceId();
        if (watch.lastInstance && watch.lastInstance != instance)
            watch.runs[watch.lastInstance].interrupted = true;
        if (!watch.runs.count(instance) && watch.runs.size() >= 16)
            for (auto prior = watch.runs.begin(); prior != watch.runs.end(); ++prior)
                if (prior->second.interrupted || prior->second.Complete())
                {
                    watch.runs.erase(prior);
                    break;
                }
        if (!watch.runs.count(instance) && watch.runs.size() >= 16)
            return Reply(handler, "WAI_REJECTED: observer instance capacity reached; restart observer.");
        auto& run = watch.runs[instance];
        watch.lastInstance = instance;
        if (run.generation.empty())
        {
            run.generation = std::to_string(urand(1, 0xffffffff)) + std::to_string(urand(1, 0xffffffff));
            run.progress.started = now;
            run.map = player->GetMapId();
            run.difficulty = uint32(player->GetMap()->GetDifficulty());
            if (auto* script = player->GetMap()->ToInstanceMap()->GetInstanceScript())
            {
                run.requiredMask =
                    RequiredEncounters(sObjectMgr->GetDungeonEncounterList(run.map, player->GetMap()->GetDifficulty()));
                run.completedMask = script->GetCompletedEncounterMask();
            }
        }
        run.members.clear();
        for (Player* member : Party(player))
            run.members.insert(member->GetGUID().GetRawValue());
    }
    auto run = watch.runs.find(watch.lastInstance);
    if (conflict)
        for (auto& [id, prior] : watch.runs)
            prior.interrupted = true;
    std::ostringstream out;
    out << "{\"schema\":1,\"backend\":\"human_dungeon_watch\",\"state\":\"" << state << "\",\"player_guid\":\"" << guid
        << "\",\"seq\":" << watch.sequence;
    if (run != watch.runs.end())
    {
        auto& current = run->second;
        out << ",\"run\":\"" << current.generation << "\",\"map\":" << current.map
            << ",\"difficulty\":" << current.difficulty << ",\"instance\":" << run->first
            << ",\"interrupted\":" << (current.interrupted ? "true" : "false")
            << ",\"completed\":" << (current.Complete() ? "true" : "false")
            << ",\"bosses\":" << current.progress.Count() << ",\"completion_source\":\""
            << (current.encounterComplete ? "encounter_mask"
                : current.map == 389      ? "ragefire_bosses"
                                          : "unavailable")
            << "\""
            << ",\"required_mask\":" << current.requiredMask << ",\"completed_mask\":" << current.completedMask
            << ",\"completion_available\":" << (current.map == 389 || current.requiredMask ? "true" : "false");
        if (eligible)
            out << ",\"observation\":"
                << ClientObservation(player, current.progress, current.generation, watch.sequence);
        if (current.Complete())
            current.completionReported = true;
    }
    out << "}";
    return Reply(handler, out.str().c_str());
}

bool DiscoverHumans(ChatHandler* handler)
{
    if (!handler->IsConsole())
        return Reply(handler, "WAI_REJECTED: discovery requires authenticated SOAP.");
    std::ostringstream out;
    out << "{\"schema\":1,\"backend\":\"human_client_discovery\",\"clients\":[";
    bool comma = false;
    for (auto it = clientIdentities.begin(); it != clientIdentities.end();)
    {
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(it->first));
        if (!it->second.Fresh(Now()) || !player || !player->GetSession() || !WaiPlayerbotsAdapter::HumanSession(player->GetSession()))
        {
            it = clientIdentities.erase(it);
            continue;
        }
        auto const& identity = it->second;
        out << (comma ? "," : "") << "{\"player_guid\":\"" << it->first << "\",\"marker\":\"" << identity.marker
            << "\",\"in_dungeon\":" << (EligibleHuman(player) ? "true" : "false")
            << ",\"bindings\":{\"forward\":" << identity.bindings[0] << ",\"back\":" << identity.bindings[1]
            << ",\"left\":" << identity.bindings[2] << ",\"right\":" << identity.bindings[3] << "}}";
        comma = true;
        ++it;
    }
    out << "]}";
    return Reply(handler, out.str().c_str());
}

class HumanIdentity : public PlayerScript
{
  public:
    HumanIdentity() : PlayerScript("WaiHumanIdentity", {PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_LOGOUT}) {}
    using PlayerScript::OnPlayerCanUseChat;
    bool OnPlayerCanUseChat(Player* player, uint32, uint32 language, std::string& message, Player* receiver) override
    {
        if (language != LANG_ADDON || message.rfind("WAI_OBS\t", 0) != 0)
            return true;
        if (!clientEnabled.load() || !player || receiver != player || !player->GetSession() || !WaiPlayerbotsAdapter::HumanSession(player->GetSession()))
            return false;
        if (WaiPlayerbotsAdapter::HasAI(player))
            return false;
        Wai::ClientIdentity identity;
        if (message.size() > 96 || !identity.Parse(message.substr(8)))
            return false;
        std::lock_guard<std::mutex> guard(mutex);
        if (!clientIdentities.count(player->GetGUID().GetRawValue()) && clientIdentities.size() >= 64)
            for (auto it = clientIdentities.begin(); it != clientIdentities.end();)
                if (!it->second.Fresh(Now()))
                    it = clientIdentities.erase(it);
                else
                    ++it;
        if (clientIdentities.size() < 64 || clientIdentities.count(player->GetGUID().GetRawValue()))
        {
            identity.heartbeat = Now();
            clientIdentities[player->GetGUID().GetRawValue()] = identity;
        }
        return false; // Registration only; never forward a command or grant control.
    }
    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(mutex);
        clientIdentities.erase(player->GetGUID().GetRawValue());
    }
};

class Commands : public CommandScript
{
  public:
    Commands() : CommandScript("WaiActionCommands") {}
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commands{{"wai", Handle, SEC_PLAYER, Console::Yes}};
        return commands;
    }
    static bool Handle(ChatHandler* handler, char const* arguments)
    {
        std::istringstream input(arguments ? arguments : "");
        input.imbue(std::locale::classic());
        std::string verb;
        input >> verb;
        bool clientRequest = verb == "client";
        if (clientRequest)
            input >> verb;
        std::lock_guard<std::mutex> guard(mutex);
        if (clientRequest && verb == "discover")
            return DiscoverHumans(handler);
        if (clientRequest && verb == "watch")
            return ObserveHuman(handler, input);
        Player* player = ObjectAccessor::FindPlayer(ownerGuid);
        Player* caller = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (verb == "stop")
        {
            if (clientRequest && handler->IsConsole())
            {
                std::string generation;
                input >> generation;
                if (!clientMode || generation != lease.generation)
                    return Reply(handler, "WAI_REJECTED: client lease changed.");
            }
            if (!handler->IsConsole() && (!caller || caller->GetGUID() != ownerGuid))
                return Reply(handler, "WAI: this character does not own action mode.");
            Stop(player);
            return Reply(handler, "WAI: observation mode. Control released.");
        }
        if (verb == "start")
        {
            if (handler->IsConsole() || !Eligible(caller, clientRequest) || !caller->IsAlive() || lease.active ||
                stopping)
                return Reply(handler, "WAI: start requires your eligible logged-in druid in the configured dungeon, "
                                      "with no SelfBot or existing lease.");
            for (auto& [guid, watch] : passiveWatches)
                for (auto& [id, run] : watch.runs)
                    run.interrupted = true;
            clientMode = clientRequest;
            ownerGuid = caller->GetGUID();
            activeOwner = ownerGuid.GetRawValue();
            instanceId = caller->GetInstanceId();
            lease = {};
            lease.active = true;
            lease.generation = std::to_string(urand(1, 0xffffffff)) + std::to_string(urand(1, 0xffffffff));
            lease.heartbeat = Now();
            clientProgress = {};
            clientProgress.started = lease.heartbeat;
            clientMembers.clear();
            if (clientMode)
                for (Player* member : Party(caller))
                    clientMembers.insert(member->GetGUID());
            return Reply(handler,
                         clientMode
                             ? "WAI: client-input observation lease granted. No server actions. .wai stop releases it."
                             : "WAI: action mode granted. Controller must connect within 3 seconds. .wai stop releases "
                               "control.");
        }
        if (verb == "status")
            return Reply(handler, lease.Fresh(Now()) ? "WAI: action mode active." : "WAI: observation mode.");
        // The external process uses an authenticated administrator SOAP connection.
        // Normal clients can start/stop their own lease but cannot submit model actions.
        if (!handler->IsConsole())
            return Reply(handler, "WAI: use .wai start, .wai stop, or .wai status.");
        if (!Eligible(player, clientMode) || player->GetInstanceId() != instanceId || !lease.Fresh(Now()))
        {
            Stop(player);
            return Reply(handler, "WAI_INACTIVE");
        }
        if (clientRequest != clientMode)
            return Reply(handler, "WAI_REJECTED: wrong control backend.");
        if (clientMode)
        {
            if (verb != "observe")
                return Reply(handler, "WAI_REJECTED: client mode accepts observations only.");
            lease.heartbeat = lease.observed = Now();
            ++lease.sequence;
            handler->SendSysMessage(
                ClientObservation(player, clientProgress, lease.generation, lease.sequence).c_str());
            return true;
        }
        if (verb == "observe")
        {
            Unit* target = ChooseTarget(player);
            observedTarget = target->GetGUID();
            lease.heartbeat = lease.observed = Now();
            ++lease.sequence;
            auto values = Features(player, target);
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << "{\"schema\":1,\"generation\":\"" << lease.generation << "\",\"seq\":" << lease.sequence
                << ",\"map\":" << mapId << ",\"features\":[";
            for (size_t i = 0; i < values.size(); ++i)
                out << (i ? "," : "") << values[i];
            out << "],\"position\":[" << player->GetPositionX() << ',' << player->GetPositionY() << ','
                << player->GetPositionZ() << "],\"allowed_spells\":[";
            bool comma = false;
            for (uint32 spell : LearnedSpells)
                if (CanCast(player, target, spell))
                {
                    out << (comma ? "," : "") << spell;
                    comma = true;
                }
            Creature* creature = target->ToCreature();
            out << "],\"enemy\":" << (target->IsAlive() && player->IsValidAttackTarget(target) ? "true" : "false")
                << ",\"distance\":" << player->GetDistance(target) << ",\"lootable\":"
                << (creature && !creature->IsAlive() && OwnLoot(player, creature) &&
                            creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE)
                        ? "true"
                        : "false")
                << ",\"busy\":" << (player->IsNonMeleeSpellCast(false) ? "true" : "false")
                << ",\"combat\":" << (player->IsInCombat() ? "true" : "false")
                << ",\"attacking\":" << (player->GetVictim() ? "true" : "false")
                << ",\"route_allowed\":" << (allowRoute ? "true" : "false") << '}';
            handler->SendSysMessage(out.str().c_str());
            return true;
        }
        std::string generation, action;
        uint64 sequence = 0;
        input >> generation >> sequence >> action;
        if (verb != "act" || !input || !lease.Consume(generation, sequence, Now()))
            return Reply(handler, "WAI_REJECTED: stale observation, lease or command.");
        Unit* target = ObjectAccessor::GetUnit(*player, observedTarget);
        if (!target || target->GetMap() != player->GetMap())
            return Reply(handler, "WAI_REJECTED: target disappeared.");
        if (action == "wait")
            return Reply(handler, "WAI_WAIT");
        if (action == "cast")
        {
            uint32 spell = 0;
            input >> spell;
            if (!input || std::find(LearnedSpells.begin(), LearnedSpells.end(), spell) == LearnedSpells.end() ||
                !CanCast(player, target, spell))
                return Reply(handler, "WAI_REJECTED: spell unavailable.");
            player->SetSelection(target->GetGUID());
            player->CastSpell(SpellTarget(player, target, spell), spell, TRIGGERED_NONE);
            return Reply(handler, "WAI_CAST_REQUESTED");
        }
        if (action == "attack" && target->IsAlive() && player->IsValidAttackTarget(target) &&
            player->IsWithinDistInMap(target, SearchRange) && player->IsWithinLOSInMap(target))
        {
            player->SetSelection(target->GetGUID());
            player->Attack(target, true);
            player->GetMotionMaster()->MoveChase(target);
            return Reply(handler, "WAI_NATIVE_ATTACK");
        }
        if (action == "loot" && !player->IsInCombat() && target->ToCreature() && !target->IsAlive() &&
            OwnLoot(player, target->ToCreature()) && player->IsWithinDistInMap(target, INTERACTION_DISTANCE))
        {
            ObjectGuid corpseGuid = target->GetGUID();
            player->SendLoot(corpseGuid, LOOT_CORPSE);
            if (player->GetLootGUID() != corpseGuid)
                return Reply(handler, "WAI_REJECTED: corpse unavailable.");
            WorldPacket money(CMSG_LOOT_MONEY);
            player->GetSession()->HandleLootMoneyOpcode(money);
            size_t slots = target->ToCreature()->loot.items.size() + target->ToCreature()->loot.quest_items.size();
            for (uint8 slot = 0; slot < std::min<size_t>(slots, 64); ++slot)
            {
                WorldPacket item(CMSG_AUTOSTORE_LOOT_ITEM);
                item << slot;
                player->GetSession()->HandleAutostoreLootItemOpcode(item);
            }
            WorldPacket release(CMSG_LOOT_RELEASE);
            release << corpseGuid;
            player->GetSession()->HandleLootReleaseOpcode(release);
            return Reply(handler, "WAI_NATIVE_LOOT_ATTEMPTED");
        }
        if (action == "approach" && !player->IsInCombat() && target->ToCreature() && !target->IsAlive() &&
            OwnLoot(player, target->ToCreature()) && player->IsWithinDistInMap(target, SearchRange) &&
            player->IsWithinLOSInMap(target) && !player->IsNonMeleeSpellCast(false))
        {
            player->GetMotionMaster()->MovePoint(1, target->GetPositionX(), target->GetPositionY(),
                                                 target->GetPositionZ(), FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, false);
            return Reply(handler, "WAI_NATIVE_CORPSE_APPROACH");
        }
        if (action == "move" && allowRoute && !player->IsInCombat() && !player->IsNonMeleeSpellCast(false))
        {
            float x, y, z;
            input >> x >> y >> z;
            if (!input || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                player->GetExactDist(x, y, z) > MaxMove)
                return Reply(handler, "WAI_REJECTED: route step invalid or beyond 8 yards.");
            player->GetMotionMaster()->MovePoint(1, x, y, z, FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, false);
            return Reply(handler, "WAI_ROUTE_REPLAY");
        }
        return Reply(handler, "WAI_REJECTED: action unavailable.");
    }
};

class Lifecycle : public PlayerScript
{
  public:
    Lifecycle() : PlayerScript("WaiActionLifecycle", {PLAYERHOOK_ON_BEFORE_UPDATE, PLAYERHOOK_ON_LOGOUT}) {}
    void OnPlayerBeforeUpdate(Player* player, uint32) override
    {
        if (activeOwner.load() != player->GetGUID().GetRawValue())
            return;
        std::lock_guard<std::mutex> guard(mutex);
        if (player->GetGUID() != ownerGuid || (!lease.active && !stopping))
            return;
        if (stopping || !lease.Fresh(Now()) || !Eligible(player, clientMode) || player->GetInstanceId() != instanceId)
            Stop(player, !stopping);
    }
    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (player->GetGUID() == ownerGuid)
            Stop(player);
    }
};

class ClientRewards : public UnitScript
{
  public:
    ClientRewards() : UnitScript("WaiClientRewards", true, {UNITHOOK_ON_UNIT_DEATH}) {}
    void OnUnitDeath(Unit* unit, Unit*) override
    {
        if (!activeOwner.load())
            return;
        std::lock_guard<std::mutex> guard(mutex);
        if (!clientMode || !lease.Fresh(Now()) || unit->GetMapId() != clientMapId ||
            unit->GetInstanceId() != instanceId)
            return;
        if (unit->ToPlayer() && clientMembers.count(unit->GetGUID()))
        {
            ++clientProgress.deaths;
            LOG_INFO("server.wai.events", "CLIENT LEARNING: PARTY DEATH RECORDED. DEATHS {}.", clientProgress.deaths);
        }
        if (clientMapId == 389)
            if (Creature* creature = unit->ToCreature())
                if (Player* player = ObjectAccessor::FindPlayer(ownerGuid))
                    if (OwnLoot(player, creature) && clientProgress.Kill(creature->GetEntry()))
                        LOG_INFO("server.wai.events", "CLIENT LEARNING: BOSS KILLED. BOSSES {}/4.",
                                 clientProgress.Count());
    }
};

class HumanRewards : public UnitScript
{
  public:
    HumanRewards() : UnitScript("WaiHumanRewards", true, {UNITHOOK_ON_UNIT_DEATH}) {}
    void OnUnitDeath(Unit* unit, Unit*) override
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (!clientEnabled.load())
            return;
        for (auto& [guid, watch] : passiveWatches)
        {
            auto it = watch.runs.find(unit->GetInstanceId());
            if (it == watch.runs.end())
                continue;
            auto& run = it->second;
            Player* player = ObjectAccessor::FindPlayer(ObjectGuid(guid));
            if (Now() - watch.heartbeat > 3000 || lease.active || stopping)
            {
                run.interrupted = true;
                continue;
            }
            if (!EligibleHuman(player) || run.map != unit->GetMapId() || player->GetMapId() != unit->GetMapId() ||
                player->GetInstanceId() != unit->GetInstanceId())
                continue;
            Creature* creature = unit->ToCreature();
            run.Death(unit->ToPlayer() ? unit->GetGUID().GetRawValue() : 0, creature ? creature->GetEntry() : 0,
                      creature && OwnLoot(player, creature));
        }
    }
};

// Existing core encounter credits provide completion; no guessed last boss or exit shortcut.
class HumanEncounters : public GlobalScript
{
  public:
    HumanEncounters() : GlobalScript("WaiHumanEncounters", {GLOBALHOOK_ON_AFTER_UPDATE_ENCOUNTER_STATE}) {}
    void OnAfterUpdateEncounterState(Map* map, EncounterCreditType, uint32, Unit* source, Difficulty difficulty,
                                     DungeonEncounterList const* encounters, uint32 dungeonCompleted,
                                     bool updated) override
    {
        if (!map || !map->IsDungeon() || map->IsRaid() || !source || source->GetMap() != map)
            return;
        std::lock_guard<std::mutex> guard(mutex);
        auto* script = map->ToInstanceMap()->GetInstanceScript();
        uint32 required = RequiredEncounters(encounters);
        if (!script || !required)
            return;
        for (auto& [guid, watch] : passiveWatches)
        {
            auto it = watch.runs.find(map->GetInstanceId());
            if (it == watch.runs.end())
                continue;
            auto& run = it->second;
            if (run.map != map->GetId() || run.difficulty != uint32(difficulty))
                continue;
            Player* player = ObjectAccessor::FindPlayer(ObjectGuid(guid));
            if (Now() - watch.heartbeat > 3000 || lease.active || stopping)
            {
                run.interrupted = true;
                continue;
            }
            if (!EligibleHuman(player) || player->GetMap() != map)
                continue;
            Creature* creature = source->ToCreature();
            Player* actor = source->GetCharmerOrOwnerPlayerOrPlayerItself();
            auto party = Party(player);
            bool member = actor && std::find(party.begin(), party.end(), actor) != party.end();
            bool owned = (creature && OwnLoot(player, creature)) || member;
            run.Encounter(required, script->GetCompletedEncounterMask(), dungeonCompleted != 0, updated, owned);
        }
    }
};

class Input : public ServerScript
{
  public:
    Input() : ServerScript("WaiActionManualOverride", {SERVERHOOK_CAN_PACKET_RECEIVE}) {}
    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!enabled.load() || !activeOwner.load())
            return true;
        uint16 opcode = packet.GetOpcode();
        bool manual = opcode == CMSG_CAST_SPELL || opcode == CMSG_ATTACKSWING || opcode == CMSG_SET_SELECTION ||
                      opcode == MSG_MOVE_START_FORWARD || opcode == MSG_MOVE_START_BACKWARD ||
                      opcode == MSG_MOVE_START_STRAFE_LEFT || opcode == MSG_MOVE_START_STRAFE_RIGHT ||
                      opcode == MSG_MOVE_START_TURN_LEFT || opcode == MSG_MOVE_START_TURN_RIGHT ||
                      opcode == MSG_MOVE_JUMP || opcode == CMSG_CANCEL_CAST || opcode == CMSG_CANCEL_CHANNELLING ||
                      opcode == CMSG_USE_ITEM || opcode == CMSG_LOOT;
        if (manual && session && session->GetPlayer())
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (lease.active && !clientMode && session->GetPlayer()->GetGUID() == ownerGuid)
            {
                lease.active = false;
                stopping = true;
            }
        }
        return true;
    }
};

class Configuration : public WorldScript
{
  public:
    Configuration() : WorldScript("WaiActionConfiguration", {WORLDHOOK_ON_AFTER_CONFIG_LOAD}) {}
    void OnAfterConfigLoad(bool) override
    {
        std::lock_guard<std::mutex> guard(mutex);
        Stop(ObjectAccessor::FindPlayer(ownerGuid));
        enabled = sConfigMgr->GetOption<bool>("Wai.Action.Enable", false);
        mapId = sConfigMgr->GetOption<uint32>("Wai.Action.Map", 43);
        passiveWatches.clear();
        clientIdentities.clear();
        clientEnabled = sConfigMgr->GetOption<bool>("Wai.Action.Client.Enable", false);
        clientMapId = sConfigMgr->GetOption<uint32>("Wai.Action.Client.Map", 389);
        allowRoute = sConfigMgr->GetOption<bool>("Wai.Action.AllowRouteReplay", false);
    }
};
} // namespace

void AddWaiActionScripts()
{
    new Commands();
    new Lifecycle();
    new ClientRewards();
    new HumanRewards();
    new HumanEncounters();
    new HumanIdentity();
    new Input();
    new Configuration();
}
