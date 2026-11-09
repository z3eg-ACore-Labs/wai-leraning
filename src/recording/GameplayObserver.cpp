/*
 * This file is part of the AzerothCore Project, released under GNU GPL v2 or later.
 */

#include "Config.h"
#include "DungeonFileWriter.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "NavigationControl.h"
#include "NavigationSnapshot.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Opcodes.h"
#include "Player.h"
#include "RecordingProfile.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "WaiPlayerbotsAdapter.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace
{
constexpr std::size_t MaxQueueBytes = 8 * 1024 * 1024;
constexpr std::size_t MaxPartyMembers = 40;
constexpr std::size_t MaxAuras = 32;
constexpr std::size_t MaxAbilities = 512;
constexpr std::size_t MaxRequests = 256;
constexpr std::size_t MaxActors = 65536;

float Coordinate(float value)
{
    return std::isfinite(value) ? value : 0.0f;
}

uint64 Now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string Labels(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return "[\"unknown\"]";

    bool damage = false;
    bool heal = false;
    bool interrupt = false;
    bool dispel = false;
    bool control = false;
    bool support = false;
    for (auto const& effect : info->Effects)
    {
        damage |= effect.Effect == SPELL_EFFECT_SCHOOL_DAMAGE || effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE ||
                  effect.Effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE ||
                  effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE;
        heal |= effect.Effect == SPELL_EFFECT_HEAL || effect.Effect == SPELL_EFFECT_HEAL_MAX_HEALTH ||
                effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL;
        interrupt |= effect.Effect == SPELL_EFFECT_INTERRUPT_CAST;
        dispel |= effect.Effect == SPELL_EFFECT_DISPEL;
        control |= effect.ApplyAuraName == SPELL_AURA_MOD_STUN || effect.ApplyAuraName == SPELL_AURA_MOD_ROOT ||
                   effect.ApplyAuraName == SPELL_AURA_MOD_FEAR || effect.ApplyAuraName == SPELL_AURA_MOD_SILENCE;
        support |=
            effect.Effect == SPELL_EFFECT_SUMMON || (effect.Effect == SPELL_EFFECT_APPLY_AURA && info->IsPositive());
    }

    std::ostringstream out;
    out << '[';
    bool first = true;
    auto label = [&](bool present, char const* name)
    {
        if (!present)
            return;
        if (!first)
            out << ',';
        first = false;
        out << '"' << name << '"';
    };
    label(damage, "damage");
    label(heal, "healing");
    label(interrupt, "interrupt");
    label(dispel, "dispel");
    label(control, "crowd_control");
    label(support, "support");
    label(first, "unknown");
    out << ']';
    return out.str();
}

char const* InputCategory(uint16 opcode)
{
    switch (opcode)
    {
    case CMSG_CAST_SPELL:
        return "ability";
    case CMSG_CANCEL_CAST:
    case CMSG_CANCEL_CHANNELLING:
        return "cancel";
    case CMSG_SET_SELECTION:
        return "targeting";
    case CMSG_ATTACKSWING:
    case CMSG_ATTACKSTOP:
        return "autoattack";
    case CMSG_USE_ITEM:
        return "item";
    case CMSG_GAMEOBJ_USE:
    case CMSG_GOSSIP_HELLO:
    case CMSG_LOOT:
        return "interaction";
    case MSG_MOVE_START_FORWARD:
    case MSG_MOVE_START_BACKWARD:
    case MSG_MOVE_STOP:
    case MSG_MOVE_START_STRAFE_LEFT:
    case MSG_MOVE_START_STRAFE_RIGHT:
    case MSG_MOVE_STOP_STRAFE:
    case MSG_MOVE_START_TURN_LEFT:
    case MSG_MOVE_START_TURN_RIGHT:
    case MSG_MOVE_STOP_TURN:
    case MSG_MOVE_JUMP:
    case MSG_MOVE_FALL_LAND:
    case MSG_MOVE_SET_FACING:
        return "movement";
    default:
        return nullptr;
    }
}

class Recorder
{
  public:
    ~Recorder()
    {
        Stop();
    }

    void Configure()
    {
        Stop();
        _profile = GameplayObservation::ParseCaptureProfile(
            sConfigMgr->GetOption<std::string>("GameplayObserver.Profile", "navigation"));
        _interval = std::clamp(sConfigMgr->GetOption<uint32>("GameplayObserver.IntervalMs", 200), 100u, 5000u);
        if (!sConfigMgr->GetOption<bool>("GameplayObserver.Enable", false))
            return;

        std::string directory =
            sConfigMgr->GetOption<std::string>("GameplayObserver.OutputDirectory", "gameplay-observations");
        {
            std::lock_guard<std::mutex> lock(_captureMutex);
            _actors.clear();
            _requests.clear();
            _sequence = 0;
            _nextSnapshot = 0;
            _nextAbilities = 0;
            _episode = 0;
            _activeEpisode = false;
            _selectionStatus = -1;
            _nextSelection = 0;
        }
        _dropped = 0;
        _running = true;
        _writer = std::thread([this, directory]() { Write(directory); });
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(_captureMutex);
            if (_ready)
                EndEpisode("recorder_stop");
            _ready = false;
            _account = 0;
            _humanGuid = 0;
            _running = false;
        }
        _available.notify_all();
        if (_writer.joinable())
            _writer.join();
    }

    char const* RecordingStatus(Player* player)
    {
        std::lock_guard<std::mutex> lock(_captureMutex);
        if (!_ready)
            return "Navigation: human control; observer disabled.";
        if (!Selected(player))
            return "Navigation: human control; recorder waiting for a single eligible human.";
        if (!player->GetMap()->IsDungeon())
            return "Navigation: human control; recorder waiting for dungeon entry.";
        return _activeEpisode ? "Navigation: human recording active."
                              : "Navigation: human recording will resume on the next game tick.";
    }

    bool Selected(Player* player) const
    {
        if (WaiPlayerbotsAdapter::HasAI(player) &&
            WaiPlayerbotsAdapter::GetMode(player) != WaiPlayerbotsAdapter::Mode::Manual)
            return false;
        return _ready && player && player->IsInWorld() && player->GetGUID().GetRawValue() == _humanGuid.load() &&
               player->GetSession() && WaiPlayerbotsAdapter::HumanSession(player->GetSession()) &&
               !GameplayNavigation::Automated(player->GetGUID().GetRawValue());
    }

    void SelectHuman()
    {
        if (!_ready || Now() < _nextSelection)
            return;
        _nextSelection = Now() + 250;
        ObjectGuid candidateGuid;
        bool multiple = false;
        {
            std::shared_lock lock(*HashMapHolder<Player>::GetLock());
            for (auto const& entry : ObjectAccessor::GetPlayers())
            {
                Player* candidate = entry.second;
                WorldSession* session = candidate->GetSession();
                if (!WaiPlayerbotsAdapter::HumanSession(session) || !candidate->IsInWorld() || candidate->IsDuringRemoveFromWorld())
                    continue;
                if (WaiPlayerbotsAdapter::HasAI(candidate) &&
                    (!GameplayNavigation::Managed(candidate->GetGUID().GetRawValue()) ||
                     (WaiPlayerbotsAdapter::GetMode(candidate) != WaiPlayerbotsAdapter::Mode::Manual &&
                      !GameplayNavigation::Automated(candidate->GetGUID().GetRawValue()))))
                    continue;
                if (GameplayNavigation::Managed(candidate->GetGUID().GetRawValue()))
                {
                    candidateGuid = candidate->GetGUID();
                    multiple = false;
                    break;
                }
                if (multiple)
                    continue;
                if (candidateGuid)
                {
                    multiple = true;
                    candidateGuid.Clear();
                    continue;
                }
                candidateGuid = candidate->GetGUID();
            }
        }
        int status = multiple ? 2 : (candidateGuid ? 1 : 0);
        std::lock_guard<std::mutex> lock(_captureMutex);
        if (!_ready || (status == _selectionStatus && candidateGuid.GetRawValue() == _humanGuid.load()))
            return;
        EndEpisode("observer_changed");
        _humanGuid = candidateGuid.GetRawValue();
        _account = 0;
        _selectionStatus = status;
        _nextSnapshot = 0;
        _nextAbilities = 0;
        char const* name = multiple ? "multiple_humans" : (candidateGuid ? "active_human" : "no_human");
        Record("observer_selection",
               "\"status\":\"" + std::string(name) + "\",\"actor\":" + std::to_string(Actor(candidateGuid)));
    }

    void Sample(Player* player)
    {
        if (player && GameplayNavigation::Automated(player->GetGUID().GetRawValue()) &&
            player->GetGUID().GetRawValue() == _humanGuid.load())
        {
            std::lock_guard<std::mutex> lock(_captureMutex);
            EndEpisode("model_execution");
            return;
        }
        if (!Selected(player))
            return;
        std::lock_guard<std::mutex> lock(_captureMutex);
        if (!Selected(player))
            return;
        if (!player->GetMap()->IsDungeon())
        {
            EndEpisode("left_dungeon");
            _account = 0;
            return;
        }
        BeginEpisode(player);
        uint64 now = Now();
        if (now < _nextSnapshot)
            return;
        _nextSnapshot = now + _interval.load();
        _account = player->GetSession()->GetAccountId();
        if (GameplayObservation::CaptureDetailedCombat(_profile.load()) && now >= _nextAbilities)
        {
            _nextAbilities = now + 5000;
            std::ostringstream abilities;
            abilities << "\"actor\":" << Actor(player->GetGUID()) << ",\"spells\":[";
            std::size_t count = 0;
            for (auto const& spell : player->GetSpellMap())
            {
                if (!spell.second || spell.second->State == PLAYERSPELL_REMOVED || !spell.second->Active ||
                    !spell.second->IsInSpec(player->GetActiveSpec()))
                    continue;
                if (count == MaxAbilities)
                    break;
                if (count++)
                    abilities << ',';
                SpellInfo const* info = sSpellMgr->GetSpellInfo(spell.first);
                abilities << "{\"spell\":" << spell.first
                          << ",\"passive\":" << (info && info->IsPassive() ? "true" : "false")
                          << ",\"labels\":" << Labels(spell.first) << '}';
            }
            abilities << "],\"bounded\":true,\"limit\":" << MaxAbilities;
            Record("abilities", abilities.str());
        }
        Record("state", "\"state\":" + Snapshot(player));
    }

    void Input(Player* player, WorldPacket const& packet)
    {
        char const* category = InputCategory(packet.GetOpcode());
        if (!category || !GameplayObservation::CaptureInput(_profile.load(), category) || !Selected(player) ||
            !player->GetMap()->IsDungeon())
            return;
        std::lock_guard<std::mutex> lock(_captureMutex);
        if (!GameplayObservation::CaptureInput(_profile.load(), category) || !Selected(player))
            return;
        BeginEpisode(player);
        _account = player->GetSession()->GetAccountId();
        std::ostringstream payload;
        payload.imbue(std::locale::classic());
        payload << "\"source\":\"human_request\",\"state_phase\":\"pre_handler\",\"category\":\"" << category
                << "\",\"opcode\":" << packet.GetOpcode() << ",\"state\":" << Snapshot(player);
        WorldPacket copy(packet);
        copy.rpos(0);
        uint8 castCount = 0;
        uint32 spellId = 0;
        bool cast = packet.GetOpcode() == CMSG_CAST_SPELL;
        try
        {
            if (cast)
            {
                uint8 flags;
                copy >> castCount >> spellId >> flags;
                SpellCastTargets targets;
                targets.Read(copy, player);
                payload << ",\"spell\":" << spellId << ",\"cast_count\":" << uint32(castCount)
                        << ",\"target\":" << Actor(targets.GetUnitTargetGUID()) << ",\"labels\":" << Labels(spellId)
                        << ",\"cooldown_ms\":" << player->GetSpellCooldownDelay(spellId);
                if (targets.HasDst())
                {
                    WorldLocation const* point = targets.GetDstPos();
                    bool finite = std::isfinite(point->GetPositionX()) && std::isfinite(point->GetPositionY()) &&
                                  std::isfinite(point->GetPositionZ());
                    if (finite)
                        payload << ",\"ground\":[" << point->GetPositionX() << ',' << point->GetPositionY() << ','
                                << point->GetPositionZ() << ']';
                    else
                        payload << ",\"invalid_ground\":true";
                }
            }
            else if (packet.GetOpcode() == CMSG_SET_SELECTION || packet.GetOpcode() == CMSG_ATTACKSWING ||
                     packet.GetOpcode() == CMSG_GAMEOBJ_USE || packet.GetOpcode() == CMSG_GOSSIP_HELLO ||
                     packet.GetOpcode() == CMSG_LOOT)
            {
                ObjectGuid target;
                copy >> target;
                payload << ",\"target\":" << Actor(target);
            }
        }
        catch (ByteBufferException const&)
        {
            payload << ",\"decode_error\":true";
            cast = false;
        }
        uint64 sequence = Record("input", payload.str());
        if (cast)
        {
            _requests[Key(spellId, castCount)] = sequence;
            if (_requests.size() > MaxRequests)
            {
                auto oldest =
                    std::min_element(_requests.begin(), _requests.end(),
                                     [](auto const& left, auto const& right) { return left.second < right.second; });
                _requests.erase(oldest);
            }
        }
    }

    void Cast(Player* player, Spell* spell)
    {
        if (!GameplayObservation::CaptureDetailedCombat(_profile.load()) || !Selected(player) || !spell ||
            !player->GetMap()->IsDungeon())
            return;
        std::lock_guard<std::mutex> lock(_captureMutex);
        if (!GameplayObservation::CaptureDetailedCombat(_profile.load()) || !Selected(player))
            return;
        BeginEpisode(player);
        uint32 id = spell->GetSpellInfo()->Id;
        std::ostringstream payload;
        bool triggered = spell->IsTriggered() || spell->GetTriggeredByAuraSpellInfo();
        payload << "\"spell\":" << id << ",\"cast_count\":" << uint32(spell->m_cast_count)
                << ",\"triggered\":" << (triggered ? "true" : "false")
                << ",\"request_seq\":" << (triggered ? 0 : Request(id, spell->m_cast_count))
                << ",\"target\":" << Actor(spell->m_targets.GetUnitTargetGUID()) << ",\"labels\":" << Labels(id);
        Record("cast_pre_execute", payload.str());
    }

    void Outgoing(WorldSession* session, WorldPacket const& packet)
    {
        if (!GameplayObservation::CaptureDetailedCombat(_profile.load()) || !_ready || !_account || !session ||
            session->GetAccountId() != _account.load())
            return;
        uint16 opcode = packet.GetOpcode();
        if (opcode != SMSG_CAST_FAILED && opcode != SMSG_SPELL_START && opcode != SMSG_SPELL_GO)
            return;
        WorldPacket copy(packet);
        copy.rpos(0);
        try
        {
            uint8 castCount;
            uint32 spellId;
            if (opcode != SMSG_CAST_FAILED)
            {
                ObjectGuid item;
                ObjectGuid caster;
                copy >> item.ReadAsPacked() >> caster.ReadAsPacked();
                if (caster.GetRawValue() != _humanGuid.load())
                    return;
            }
            copy >> castCount >> spellId;
            std::lock_guard<std::mutex> lock(_captureMutex);
            if (!GameplayObservation::CaptureDetailedCombat(_profile.load()) || !_ready || !_activeEpisode ||
                session->GetAccountId() != _account.load())
                return;
            std::ostringstream payload;
            payload << "\"spell\":" << spellId << ",\"cast_count\":" << uint32(castCount)
                    << ",\"request_seq\":" << Request(spellId, castCount);
            if (opcode == SMSG_CAST_FAILED)
            {
                uint8 result;
                copy >> result;
                payload << ",\"result\":" << uint32(result);
            }
            Record(opcode == SMSG_CAST_FAILED ? "cast_failed" : (opcode == SMSG_SPELL_START ? "cast_start" : "cast_go"),
                   payload.str());
        }
        catch (ByteBufferException const&)
        {
            ++_dropped;
        }
    }

    void Logout(Player* player)
    {
        if (!player || player->GetGUID().GetRawValue() != _humanGuid.load())
            return;
        _account = 0;
        if (!_ready)
            return;
        std::lock_guard<std::mutex> lock(_captureMutex);
        EndEpisode("logout");
        Record("logout", "\"actor\":" + std::to_string(Actor(player->GetGUID())));
        _humanGuid = 0;
        _requests.clear();
    }

  private:
    void BeginEpisode(Player* player)
    {
        if (_activeEpisode && (_map != player->GetMapId() || _instance != player->GetInstanceId()))
            EndEpisode("instance_change");
        if (_activeEpisode)
            return;
        std::string dungeon = player->GetMap()->GetMapName();
        if (!QueueBoundary({GameplayObservation::CommandKind::Open, dungeon}))
            return;
        _activeEpisode = true;
        _sequence = 0;
        _actors.clear();
        _dropped = 0;
        ++_episode;
        _map = player->GetMapId();
        _instance = player->GetInstanceId();
        _requests.clear();
        _nextAbilities = 0;
        Record("observer_selection",
               "\"status\":\"active_human\",\"actor\":" + std::to_string(Actor(player->GetGUID())));
        Record("episode_start", "\"map\":" + std::to_string(_map) + ",\"instance\":" + std::to_string(_instance) +
                                    ",\"difficulty\":" + std::to_string(uint32(player->GetMap()->GetDifficulty())) +
                                    ",\"provenance\":\"human\"" + ",\"profile\":\"" +
                                    GameplayObservation::CaptureProfileName(_profile.load()) + "\"");
    }

    void EndEpisode(char const* reason)
    {
        if (!_activeEpisode)
            return;
        Record("episode_end", "\"reason\":\"" + std::string(reason) + "\"");
        QueueBoundary({GameplayObservation::CommandKind::Close, "", _dropped.load()});
        _activeEpisode = false;
        _requests.clear();
    }

    static uint64 Key(uint32 spell, uint8 count)
    {
        return (uint64(spell) << 8) | count;
    }

    uint64 Request(uint32 spell, uint8 count) const
    {
        auto found = _requests.find(Key(spell, count));
        return found == _requests.end() ? 0 : found->second;
    }

    uint64 Actor(ObjectGuid guid)
    {
        if (!guid)
            return 0;
        auto found = _actors.find(guid.ToString());
        if (found != _actors.end())
            return found->second;
        if (_actors.size() >= MaxActors)
            return 0;
        return _actors.emplace(guid.ToString(), _actors.size() + 1).first->second;
    }

    std::string UnitState(Unit* unit)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(7) << "{\"actor\":" << Actor(unit->GetGUID())
            << ",\"class\":" << uint32(unit->getClass()) << ",\"level\":" << uint32(unit->GetLevel())
            << ",\"position\":[" << Coordinate(unit->GetPositionX()) << ',' << Coordinate(unit->GetPositionY()) << ','
            << Coordinate(unit->GetPositionZ()) << ',' << Coordinate(unit->GetOrientation())
            << "],\"health\":" << unit->GetHealth() << ",\"max_health\":" << unit->GetMaxHealth()
            << ",\"power_type\":" << uint32(unit->getPowerType())
            << ",\"power\":" << unit->GetPower(unit->getPowerType())
            << ",\"max_power\":" << unit->GetMaxPower(unit->getPowerType())
            << ",\"alive\":" << (unit->IsAlive() ? "true" : "false")
            << ",\"combat\":" << (unit->IsInCombat() ? "true" : "false") << ",\"target\":" << Actor(unit->GetTarget())
            << ",\"casts\":[";
        bool first = true;
        for (uint32 type = CURRENT_GENERIC_SPELL; type < CURRENT_MAX_SPELL; ++type)
        {
            if (Spell* spell = unit->GetCurrentSpell(type))
            {
                if (!first)
                    out << ',';
                first = false;
                out << spell->GetSpellInfo()->Id;
            }
        }
        out << "],\"auras\":[";
        std::size_t count = 0;
        for (auto const& aura : unit->GetAppliedAuras())
        {
            if (count == MaxAuras)
                break;
            if (count++)
                out << ',';
            out << aura.first;
        }
        out << "],\"auras_truncated\":" << (unit->GetAppliedAuras().size() > MaxAuras ? "true" : "false") << '}';
        return out.str();
    }

    std::string Snapshot(Player* player)
    {
        if (_profile.load() == GameplayObservation::CaptureProfile::Navigation)
            return GameplayObservation::NavigationSnapshot(
                player->GetMapId(), player->GetInstanceId(), Actor(player->GetGUID()), player->GetPositionX(),
                player->GetPositionY(), player->GetPositionZ(), player->GetOrientation(), player->IsAlive(),
                player->IsInCombat(), player->IsNonMeleeSpellCast(false));
        std::ostringstream out;
        out << "{\"map\":" << player->GetMapId() << ",\"instance\":" << player->GetInstanceId()
            << ",\"class\":" << uint32(player->getClass()) << ",\"level\":" << uint32(player->GetLevel())
            << ",\"self\":" << UnitState(player) << ",\"party\":[";
        std::size_t count = 0;
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref && count < MaxPartyMembers; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || member == player || !member->IsInWorld() || member->GetMap() != player->GetMap())
                    continue;
                if (count++)
                    out << ',';
                out << UnitState(member);
            }
        }
        out << "],\"selected\":";
        Unit* selected = player->GetSelectedUnit();
        out << (selected && selected->GetMap() == player->GetMap() ? UnitState(selected) : "null") << '}';
        return out.str();
    }

    bool QueueBoundary(GameplayObservation::WriterCommand command)
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        if (!_ready || _pendingBoundaries >= 128)
        {
            _ready = false;
            _running = false;
            _available.notify_one();
            LOG_ERROR("module.gameplay_observer", "Recorder stopped: dungeon boundary queue is unavailable");
            return false;
        }
        ++_pendingBoundaries;
        _queue.push_back(std::move(command));
        _available.notify_one();
        return true;
    }

    uint64 Record(char const* kind, std::string const& payload)
    {
        // Selection/login events outside an episode never create a recording.
        if (!_activeEpisode)
            return 0;
        uint64 sequence = ++_sequence;
        std::ostringstream out;
        out << "{\"schema\":1,\"seq\":" << sequence << ",\"time_ms\":" << Now() << ",\"episode\":" << _episode
            << ",\"kind\":\"" << kind << "\"," << payload << '}';
        std::string line = out.str();
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (!_ready || line.size() + _queueBytes > MaxQueueBytes)
            {
                ++_dropped;
                return sequence;
            }
            _queueBytes += line.size();
            _queue.push_back({GameplayObservation::CommandKind::Line, std::move(line)});
        }
        _available.notify_one();
        return sequence;
    }

    void Write(std::string const& directory)
    {
        try
        {
            GameplayObservation::DungeonFileWriter file(directory);
            _ready = _running.load();
            while (true)
            {
                GameplayObservation::WriterCommand command;
                bool haveCommand = false;
                {
                    std::unique_lock<std::mutex> lock(_queueMutex);
                    _available.wait_for(lock, std::chrono::seconds(1),
                                        [this]() { return !_running || !_queue.empty(); });
                    if (_queue.empty() && !_running)
                        break;
                    if (!_queue.empty())
                    {
                        command = std::move(_queue.front());
                        _queue.pop_front();
                        if (command.kind == GameplayObservation::CommandKind::Line)
                            _queueBytes -= command.text.size();
                        else
                            --_pendingBoundaries;
                        haveCommand = true;
                    }
                }
                if (haveCommand)
                    file.Apply(command);
                file.Flush(false);
            }
            // A clean stop must have queued the episode close before draining.
            if (file.IsOpen())
                throw std::runtime_error("Recorder stopped before dungeon boundary was saved");
        }
        catch (std::exception const& error)
        {
            _ready = false;
            LOG_ERROR("module.gameplay_observer", "Recorder stopped: {}", error.what());
        }
        _ready = false;
        std::lock_guard<std::mutex> lock(_queueMutex);
        _queue.clear();
        _queueBytes = 0;
        _pendingBoundaries = 0;
    }

    std::atomic<GameplayObservation::CaptureProfile> _profile{GameplayObservation::CaptureProfile::Navigation};
    std::atomic<bool> _running{false};
    std::atomic<bool> _ready{false};
    std::atomic<uint64> _humanGuid{0};
    std::atomic<uint32> _account{0};
    std::atomic<uint32> _interval{200};
    std::atomic<uint64> _dropped{0};
    std::mutex _captureMutex;
    std::mutex _queueMutex;
    std::condition_variable _available;
    std::deque<GameplayObservation::WriterCommand> _queue;
    std::size_t _pendingBoundaries = 0;
    std::size_t _queueBytes = 0;
    std::thread _writer;
    std::unordered_map<std::string, uint64> _actors;
    std::map<uint64, uint64> _requests;
    uint64 _sequence = 0;
    uint64 _nextSnapshot = 0;
    uint64 _nextAbilities = 0;
    uint64 _episode = 0;
    uint32 _map = 0;
    uint32 _instance = 0;
    bool _activeEpisode = false;
    uint64 _nextSelection = 0;
    int _selectionStatus = -1;
};

Recorder& Observer()
{
    static Recorder recorder;
    return recorder;
}

class GameplayObserverWorld : public WorldScript
{
  public:
    GameplayObserverWorld()
        : WorldScript("GameplayObserverWorld",
                      {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_SHUTDOWN, WORLDHOOK_ON_UPDATE})
    {
    }
    void OnAfterConfigLoad(bool /*reload*/) override
    {
        Observer().Configure();
    }
    void OnShutdown() override
    {
        Observer().Stop();
    }
    void OnUpdate(uint32 /*diff*/) override
    {
        Observer().SelectHuman();
    }
};

class GameplayObserverPlayer : public PlayerScript
{
  public:
    GameplayObserverPlayer()
        : PlayerScript("GameplayObserverPlayer",
                       {PLAYERHOOK_ON_BEFORE_UPDATE, PLAYERHOOK_ON_SPELL_CAST, PLAYERHOOK_ON_LOGOUT})
    {
    }
    void OnPlayerBeforeUpdate(Player* player, uint32 /*diff*/) override
    {
        Observer().Sample(player);
    }
    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        Observer().Cast(player, spell);
    }
    void OnPlayerLogout(Player* player) override
    {
        Observer().Logout(player);
    }
};

class GameplayObserverPackets : public ServerScript
{
  public:
    GameplayObserverPackets()
        : ServerScript("GameplayObserverPackets", {SERVERHOOK_CAN_PACKET_RECEIVE, SERVERHOOK_CAN_PACKET_SEND})
    {
    }
    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (session)
            Observer().Input(session->GetPlayer(), packet);
        return true;
    }
    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        Observer().Outgoing(session, packet);
        return true;
    }
};
} // namespace

char const* GameplayRecordingStatus(Player* player)
{
    return Observer().RecordingStatus(player);
}

void AddGameplayObserverScripts()
{
    new GameplayObserverWorld();
    new GameplayObserverPlayer();
    new GameplayObserverPackets();
}
