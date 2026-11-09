/* AzerothCore module; GNU GPL v2 or later. */
#include "ScriptMgr.h"
#include "Chat.h"
#include "Log.h"
#include "WorldScript.h"
#ifdef MOD_PLAYERBOTS
#include "AccountMgr.h"
#include "AccountSetup.h"
#include "AiObjectContext.h"
#include "AllMapScript.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "EpisodeData.h"
#include "EpisodeJournal.h"
#include "EpisodeMovement.h"
#include "EpisodeOwnership.h"
#include "EpisodeRecovery.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "LootObjectStack.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MovementActions.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerDump.h"
#include "PlayerScript.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "ReleaseSpiritAction.h"
#include "StartProcess.h"
#include "WaiProcessAdapter.h"
#include "StringFormat.h"
#include "UnitScript.h"
#include "UseMeetingStoneAction.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include "WaiPlayerbotsAdapter.h"
#include "WorldSessionMgr.h"
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <type_traits>

void QueueWaiObserverClients(ChatHandler* handler);

using namespace Acore::ChatCommands;
namespace
{
constexpr uint32 DungeonMap = 389;
constexpr std::size_t PartySize = 5;
enum class Phase
{
    Empty,
    Loading,
    Entering,
    Running,
    Finished,
    Resetting
};

struct Member
{
    ObjectGuid guid;
    std::string name;
    std::string snapshot;
    bool loginRequested = false;
    bool prepared = false;
    bool strategiesSet = false;
    bool recovering = false;
    uint64 recoveryStarted = 0;
    uint64 nextRecoveryAttempt = 0;
    unsigned recoveryAttempts = 0;
    ObjectGuid previousMaster;
    std::vector<std::string> previousStrategies;
};

struct Death
{
    uint64 wall;
    uint64 tick;
    uint32 entry;
    ObjectGuid player;
};

struct Request
{
    std::string operation;
    std::vector<std::string> arguments;
};

constexpr unsigned MaxParallelParties = 4;
std::atomic<unsigned> parallelParties{1};
std::atomic<bool> sharedControllerActive{false};
std::mutex bestTimeMutex;
std::map<std::string, uint64> bestTimes;

template <unsigned Slot> struct PartyRuntime
{
    inline static std::mutex guard;
    inline static std::atomic<bool> enabled{false};
    inline static std::atomic<uint32> activeInstance{0};
    inline static std::array<Member, PartySize> members;
    inline static std::array<Position, 4> destinations;
    inline static std::deque<Request> requests;
    inline static std::deque<Death> deaths;
    inline static std::vector<ObjectGuid> retired;
    inline static WaiEpisodes::Progress progress;
    inline static Phase phase = Phase::Empty;
    inline static uint32 account = 0, instance = 0, timeoutSeconds = 3600;
    inline static uint64 started = 0, ended = 0, setupStarted = 0, nextWorldUpdate = 0, nextDrive = 0,
                         nextHeartbeat = 0;
    inline static uint64 nextPathWarning = 0;
    inline static std::string episode;
    inline static std::string outputDirectory = "wai-episodes";
    inline static WaiEpisodes::Journal journal;
    inline static bool journalFailed = false;
    inline static std::string stateDirectory = "wai-episode-state";
    inline static WaiEpisodes::Ownership ownership;
    inline static std::array<std::string, 6> configured;
    inline static bool frozen = false, wipeDetected = false;
    inline static bool repeatPending = false;
    inline static bool existingParty = false;
    inline static bool recoveryEnabled = true;
    inline static uint32 recoveryTimeoutSeconds = 120;
    inline static bool learnedNavigation = true;
    inline static uint64 policySequence = 0, pendingSequence = 0, controllerHeartbeat = 0;
    inline static uint64 policyPublished = 0, actionQueued = 0;
    inline static int pendingAction = -1;
    inline static bool decisionExecuting = false;
    inline static std::string policyFeatures, terminalOutcome, policyVersion, trainingStage;
    inline static std::string trainedVersion;
    inline static bool trainingReported = false;
    inline static std::string actionResult = "none";
    inline static bool cycleRequested = false;
    inline static bool boundaryEnabled = true;
    inline static float boundaryTolerance = 3.0f;
    inline static std::array<WaiEpisodes::BoundaryWatch, PartySize> boundaryWatches;
    inline static uint64 nextBoundaryWarning = 0;
    inline static uint64 bestTime = 0;

    static std::string ResultReason()
    {
        if (terminalOutcome == "completed_all_bosses")
            return "GOAL ACHIEVED. ALL THE BOSSES KILLED";
        if (terminalOutcome == "wandering_off_limits")
            return "WANDERING OFF LIMITS";
        std::string reason = terminalOutcome;
        std::transform(reason.begin(), reason.end(), reason.begin(),
                       [](unsigned char value) { return value == '_' ? ' ' : static_cast<char>(std::toupper(value)); });
        return reason;
    }
    inline static WaiEpisodes::AccountSetupWait accountSetup;
    inline static std::vector<std::string> accountSetupArguments;
    inline static bool accountSetupRecover = false;
    inline static std::shared_ptr<Acore::AsyncProcessResult> learnerProcess;
    inline static std::string learnerExecutable, learnerScript, learnerHome, learnerCredentials, learnerUrl;

    static bool StartLearner()
    {
        if (!WaiProcessAdapter::Available)
        {
            LOG_ERROR("server.wai.events", "WAI learner unavailable: safe process cancellation API v1 missing. See mod-wai/compatibility.");
            return false;
        }
        if constexpr (Slot > 0)
            return sharedControllerActive;
        if (!learnedNavigation)
            return true;
        if (learnerProcess)
        {
            auto& result = learnerProcess->GetFutureResult();
            if (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return true;
            auto previous = std::move(learnerProcess);
            try
            {
                previous->GetFutureResult().get();
            }
            catch (std::exception const&)
            {
                LOG_ERROR("server.wai.events",
                          "WAI_LEARNING Previous controller launch failed; inspect learner configuration.");
                sharedControllerActive = false;
                return false;
            }
        }
        std::error_code error;
        if (learnerExecutable.empty() || learnerScript.empty() || learnerHome.empty() ||
            !std::filesystem::is_regular_file(learnerExecutable, error) || error ||
            !std::filesystem::is_regular_file(learnerScript, error) || error)
        {
            LOG_ERROR("server.wai.events",
                      "WAI_LEARNING Cannot start episode: configure the learner executable, script and home.");
            sharedControllerActive = false;
            return false;
        }
        std::vector<std::string> arguments{"-u",     std::filesystem::absolute(learnerScript).string(),
                                           "--home", std::filesystem::absolute(learnerHome).string(),
                                           "--url",  learnerUrl};
        if (!learnerCredentials.empty())
        {
            arguments.push_back("--credentials");
            arguments.push_back(std::filesystem::absolute(learnerCredentials).string());
        }
        arguments.push_back("--parties");
        arguments.push_back(std::to_string(parallelParties.load()));
        // Reuse the core's process supervisor; process creation/waiting and output collection are asynchronous.
        // Configuration supplies paths only; credentials never appear as command-line values.
        learnerProcess = WaiProcessAdapter::Start(learnerExecutable, arguments, "server.wai.learning", "", true);
        sharedControllerActive = true;
        LOG_INFO("server.wai.events", "WAI_LEARNING Controller/trainer launch requested by episode start; "
                                      "training begins only after a completed rollout.");
        return true;
    }

    static char const* PolicyName()
    {
        return learnedNavigation ? "learned_local_navigation_native_combat" : "native_bots_scripted_boss_route";
    }

    static char const* PhaseName()
    {
        switch (phase)
        {
        case Phase::Empty:
            return "empty";
        case Phase::Loading:
            return "loading";
        case Phase::Entering:
            return "entering";
        case Phase::Running:
            return "running";
        case Phase::Finished:
            return "finished";
        case Phase::Resetting:
            return "resetting";
        }
        return "unknown";
    }

    // Reuse PlayerBots movement gates, navigation and timing; this adapter is not an engine action.
    class RouteMove : public MovementAction
    {
      public:
        explicit RouteMove(PlayerbotAI* ai) : MovementAction(ai, "wai episode route") {}
        bool Advance(Position const& destination)
        {
            return MoveTo(DungeonMap, destination.GetPositionX(), destination.GetPositionY(),
                          destination.GetPositionZ());
        }
    };

    static uint64 Tick()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    static uint64 Wall()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    static void Emit(char const* event, std::string const& fields = "", uint64 wall = 0, uint64 tick = 0)
    {
        wall = wall ? wall : Wall();
        tick = tick ? tick : (ended ? ended : Tick());
        std::string payload = Acore::StringFormat(
            "{{\"schema_version\":1,\"event\":\"{}\",\"episode\":\"{}\",\"timestamp_ms\":{},\"elapsed_ms\":{},"
            "\"map\":{},\"instance\":{},\"bosses_killed\":{},\"bosses_required\":4,\"deaths\":{}{} }}",
            event, episode, wall, started && tick >= started ? tick - started : 0, DungeonMap, instance,
            progress.killed.size(), progress.deaths, fields);
        payload.insert(payload.rfind('}'), Acore::StringFormat(",\"party\":{}", Slot + 1));
        if (std::string_view(event) == "start")
        {
            payload.insert(payload.rfind('}'),
                           Acore::StringFormat(
                               ",\"recovery_enabled\":{},\"recovery_timeout_seconds\":{},\"summon_revive_mode\":{}",
                               recoveryEnabled, recoveryTimeoutSeconds, sPlayerbotAIConfig.ReviveBotWhenSummoned));
            LOG_INFO(
                "server.wai.events",
                "WAI_OBSERVER PARTY {}: map {}, instance {}, tank {}. "
                "Use these commands in your GM client's chat, not the server console; remain outside the bot party.",
                Slot + 1, DungeonMap, instance, members[0].name);
            LOG_INFO("server.wai.events", "WAI_OBSERVER First: .gm on | .gm visible off | .tele Orgrimmar");
            LOG_INFO("server.wai.events",
                     "WAI_OBSERVER Wait until the outdoor teleport completes, then: "
                     "/cleartarget | .instance unbind 389 | .appear {}",
                     members[0].name);
            LOG_INFO("server.wai.events",
                     "WAI_OBSERVER Compare your .gps with .gps {} and instance {}. "
                     "Do not interact with the bots or enemies while observing.",
                     members[0].name, instance);
        }
        // Keep the recording schema stable; emphasize counters in human-facing server logs.
        std::string display = payload;
        for (std::string key : {"bosses_killed", "deaths"})
        {
            std::string token = "\"" + key + "\"";
            auto position = display.find(token);
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
            if (position != std::string::npos)
                display.replace(position, token.size(), "\"" + key + "\"");
        }
        LOG_INFO("server.wai.episodes", "WAI_EPISODE {}", display);
        if (!journalFailed && !journal.Write(payload))
        {
            journalFailed = true;
            LOG_ERROR("server.wai.events", "WAI episode JSONL write failed; episode recording is incomplete.");
        }
    }

    static Player* Online(Member const& member)
    {
        return ObjectAccessor::FindConnectedPlayer(member.guid);
    }

    static std::string MemberState(std::size_t slot, Player* player)
    {
        std::string fields = ",\"member_slot\":" + std::to_string(slot + 1);
        if (!player)
            return fields + ",\"online\":false";
        return fields + Acore::StringFormat(
                            ",\"online\":true,\"world\":{},\"member_map\":{},\"member_instance\":{},\"alive\":{},"
                            "\"ghost\":{},\"combat\":{},\"teleporting\":{},\"health_pct\":{},\"mana_pct\":{},"
                            "\"x\":{},\"y\":{},\"z\":{}",
                            player->IsInWorld(), player->GetMapId(), player->GetInstanceId(), player->IsAlive(),
                            player->HasPlayerFlag(PLAYER_FLAGS_GHOST), player->IsInCombat(),
                            player->IsBeingTeleported(), player->GetHealthPct(), player->GetPowerPct(POWER_MANA),
                            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
    }

    inline static std::mutex telemetryMutex;
    inline static std::array<ObjectGuid, PartySize> telemetryRoster{};
    struct Telemetry
    {
        std::string event;
        std::string fields;
        uint64 wall;
        uint64 tick;
    };
    inline static std::deque<Telemetry> telemetry;
    inline static unsigned telemetryDropped = 0;

    static void SetTelemetryRoster(std::array<ObjectGuid, PartySize> const& roster)
    {
        std::lock_guard lock(telemetryMutex);
        telemetryRoster = roster;
        telemetry.clear();
        telemetryDropped = 0;
    }

    static void QueueTelemetry(char const* event, Player* player, std::string const& extra = "")
    {
        if (!activeInstance)
            return;
        std::lock_guard lock(telemetryMutex);
        auto it = std::find(telemetryRoster.begin(), telemetryRoster.end(), player->GetGUID());
        if (it == telemetryRoster.end())
            return;
        if (telemetry.size() >= 512)
        {
            ++telemetryDropped;
            return;
        }
        telemetry.push_back(
            {event, MemberState(std::distance(telemetryRoster.begin(), it), player) + extra, Wall(), Tick()});
    }

    static void DrainTelemetry()
    {
        std::deque<Telemetry> pending;
        unsigned dropped = 0;
        {
            std::lock_guard lock(telemetryMutex);
            pending.swap(telemetry);
            dropped = telemetryDropped;
            telemetryDropped = 0;
        }
        if (phase != Phase::Running)
            return;
        for (auto const& item : pending)
            Emit(item.event.c_str(), item.fields, item.wall, item.tick);
        if (dropped)
            Emit("telemetry_dropped", Acore::StringFormat(",\"count\":{}", dropped));
    }

    static void RestoreExistingParty()
    {
        if (!existingParty)
            return;
        for (auto const& member : members)
            if (Player* player = Online(member); player && WaiPlayerbotsAdapter::HeadlessSession(player->GetSession()))
                if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player))
                {
                    player->StopMoving();
                    ai->ClearStrategies(BOT_STATE_NON_COMBAT);
                    std::string restore;
                    for (auto const& name : member.previousStrategies)
                        restore += (restore.empty() ? "+" : ",+") + name;
                    ai->ChangeStrategy(restore, BOT_STATE_NON_COMBAT);
                    ai->SetMaster(ObjectAccessor::FindConnectedPlayer(member.previousMaster));
                }
    }

    static void Finish(char const* outcome, uint64 wall = 0, uint64 tick = 0)
    {
        if (phase == Phase::Finished || phase == Phase::Empty)
            return;
        ended = tick ? tick : Tick();
        terminalOutcome = outcome;
        if (started)
        {
            // Best time is scoped to this server session and matching templates/rules.
            std::string profile = PolicyName() + std::to_string(timeoutSeconds) + std::to_string(boundaryEnabled) +
                                  std::to_string(boundaryTolerance) + std::to_string(recoveryEnabled) + "|" +
                                  std::to_string(recoveryTimeoutSeconds);
            for (unsigned i = 1; i < configured.size(); ++i)
                profile += "|" + configured[i];
            for (auto const& member : members)
                profile += "|" + std::to_string(std::hash<std::string>{}(member.snapshot));
            std::lock_guard bestLock(bestTimeMutex);
            auto& best = bestTimes[profile];
            if (terminalOutcome == "completed_all_bosses" && (!best || ended - started < best))
                best = ended - started;
            bestTime = best;
        }
        LOG_INFO("server.wai.events",
                 "RUN {}. PARTY {}. REASON: {}. CURRENT TIME: {:.1f} S. BEST TIME (SESSION): {}. "
                 "WEIGHTS CHANGES: NOT YET REPORTED.",
                 terminalOutcome == "completed_all_bosses" ? "FINISHED" : "TERMINATED", Slot + 1, ResultReason(),
                 started ? (ended - started) / 1000.0 : 0.0,
                 bestTime ? Acore::StringFormat("{:.1f} S", bestTime / 1000.0) : "NONE");
        pendingAction = -1;
        decisionExecuting = false;
        Emit("end", std::string(",\"outcome\":\"") + outcome + "\"", wall, ended);
        phase = Phase::Finished;
        WaiEpisodes::Integrity::ClearRoster();
        frozen = false;
        RestoreExistingParty();
        if (existingParty)
        {
            activeInstance = 0;
            WaiEpisodes::Integrity::ClearRoster();
            frozen = true;
        }
    }

    static bool LoadDestinations()
    {
        std::array<bool, 4> found{};
        for (auto const& [spawn, data] : sObjectMgr->GetAllCreatureData())
        {
            if (data.mapid != DungeonMap || !(data.spawnMask & 1))
                continue;
            for (std::size_t i = 0; i < WaiEpisodes::Bosses.size(); ++i)
                if (data.id == WaiEpisodes::Bosses[i])
                {
                    if (found[i])
                        return false;
                    found[i] = true;
                    destinations[i].Relocate(data.posX, data.posY, data.posZ, data.orientation);
                }
        }
        return std::all_of(found.begin(), found.end(), [](bool value) { return value; });
    }

    static std::string RandomName()
    {
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            std::string name = "Wai";
            for (unsigned i = 0; i < 8; ++i)
                name += static_cast<char>('a' + urand(0, 25));
            if (!sCharacterCache->GetCharacterCacheByName(name) &&
                ObjectMgr::CheckPlayerName(name, true) == CHAR_NAME_SUCCESS)
                return name;
        }
        throw std::runtime_error("Unable to allocate training character name");
    }

    static bool CloneRoster()
    {
        boundaryWatches = {};
        nextBoundaryWarning = 0;
        bestTime = 0;
        existingParty = false;
        phase = Phase::Loading;
        setupStarted = Tick();
        started = 0;
        ended = 0;
        progress = {};
        instance = 0;
        activeInstance = 0;
        frozen = false;
        wipeDetected = false;
        nextPathWarning = 0;
        deaths.clear();
        policySequence = pendingSequence = controllerHeartbeat = 0;
        policyPublished = actionQueued = 0;
        decisionExecuting = false;
        pendingAction = -1;
        policyFeatures.clear();
        actionResult = "none";
        terminalOutcome.clear();
        policyVersion.clear();
        trainingStage.clear();
        trainedVersion.clear();
        trainingReported = false;
        episode = std::to_string(Wall()) + "-" + std::to_string(urand(0, 999999));
        journalFailed = false;
        if (!journal.Open(outputDirectory, episode))
        {
            journalFailed = true;
            LOG_ERROR("server.wai.events", "Cannot create WAI episode JSONL directory/file; no clones created.");
            Finish("recording_open_failed");
            return false;
        }
        Emit("episode_created", Acore::StringFormat(",\"policy\":\"{}\"", PolicyName()));
        if (journalFailed)
        {
            Finish("recording_write_failed");
            return false;
        }
        for (auto& member : members)
        {
            member.name = RandomName();
            member.recovering = false;
            member.recoveryStarted = member.nextRecoveryAttempt = 0;
            member.recoveryAttempts = 0;
            member.loginRequested = false;
            member.prepared = false;
            member.strategiesSet = false;
            if (PlayerDumpReader().LoadDumpFromString(member.snapshot, account, member.name, 0) != DUMP_SUCCESS)
            {
                Finish("clone_failed");
                return false;
            }
            member.guid = sCharacterCache->GetCharacterGuidByName(member.name);
            if (member.guid.IsEmpty())
            {
                Finish("clone_cache_failed");
                return false;
            }
            if (!ownership.Remember(member.guid.GetCounter(), member.name))
            {
                Finish("clone_ownership_write_failed");
                return false;
            }
            Emit("clone_created", ",\"member_slot\":" + std::to_string(&member - members.data() + 1) +
                                      ",\"clone_name\":\"" + member.name + "\"");
        }
        std::array<ObjectGuid, PartySize> roster;
        std::transform(members.begin(), members.end(), roster.begin(),
                       [](Member const& member) { return member.guid; });
        WaiEpisodes::Integrity::SetRoster(roster);
        SetTelemetryRoster(roster);
        Emit("prepared", Acore::StringFormat(",\"policy\":\"{}\",\"xp_locked\":true", PolicyName()));
        return true;
    }

    static bool RemoveClones()
    {
        if (existingParty)
        {
            Emit("error", ",\"reason\":\"existing_party_use_detach_never_clone_cleanup\"");
            return false;
        }
        // Delete only validated ledger-owned or explicitly recovered clones, never templates/unrelated characters.
        for (auto const& member : members)
        {
            if (member.guid.IsEmpty())
                continue;
            if (sCharacterCache->GetCharacterAccountIdByGuid(member.guid) != account)
            {
                Emit("error", ",\"reason\":\"clone_ownership_changed\"");
                return false;
            }
            if (Player* player = Online(member); player && !WaiPlayerbotsAdapter::HeadlessSession(player->GetSession()))
            {
                Emit("error", ",\"reason\":\"human_logged_into_clone_account\"");
                return false;
            }
            if (member.loginRequested && !Online(member) && !sRandomPlayerbotMgr.GetPlayerBot(member.guid))
            {
                Emit("error", ",\"reason\":\"login_or_logout_pending_restart_required\"");
                return false;
            }
        }
        activeInstance = 0;
        WaiEpisodes::Integrity::ClearRoster();
        for (auto const& member : members)
            if (Player* player = Online(member))
            {
                if (Group* group = player->GetGroup())
                    group->Disband();
                if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player))
                    ai->SetMaster(nullptr);
            }
        for (auto& member : members)
        {
            if (member.guid.IsEmpty())
                continue;
            if (sRandomPlayerbotMgr.GetPlayerBot(member.guid))
                sRandomPlayerbotMgr.LogoutPlayerBot(member.guid);
            if (Online(member))
                return false;
            retired.push_back(member.guid);
            Player::DeleteFromDB(member.guid.GetCounter(), account, true, true);
            member.guid.Clear();
            member.loginRequested = false;
        }
        phase = Phase::Resetting;
        setupStarted = Tick();
        return true;
    }

    static void Create(std::vector<std::string> const& arguments, bool recover = false)
    {
        if (existingParty)
        {
            Emit("error", ",\"reason\":\"detach_existing_party_first\"");
            return;
        }

        if (phase != Phase::Empty || accountSetup.Pending() || arguments.size() != 6)
        {
            Emit("error", ",\"reason\":\"requires_empty_state_account_and_five_offline_templates\"");
            return;
        }
        std::string accountName = arguments.front();
        std::transform(accountName.begin(), accountName.end(), accountName.begin(),
                       [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
        uint32 candidate = AccountMgr::GetId(accountName);
        if constexpr (Slot > 0)
            if (!candidate)
            {
                // Dedicated clone accounts only. Password is never logged or returned.
                AccountOpResult result = sAccountMgr->CreateAccount(
                    accountName, Acore::StringFormat("{:08x}{:08x}", urand(0, 0xffffffff), urand(0, 0xffffffff)));
                if (result != AOR_OK && result != AOR_NAME_ALREADY_EXIST)
                {
                    Emit("error", ",\"reason\":\"dedicated_account_creation_failed\"");
                    cycleRequested = false;
                    return;
                }
                // Native account creation queues database writes; a synchronous lookup
                // on another connection need not see them until a later world update.
                accountSetup.Begin(Tick());
                accountSetupArguments = arguments;
                accountSetupArguments.front() = accountName;
                accountSetupRecover = recover;
                Emit("account_preparing");
                return;
            }
        if (!candidate || sWorldSessionMgr->FindSession(candidate) ||
            std::find(sPlayerbotAIConfig.RandomBotAccounts.begin(), sPlayerbotAIConfig.RandomBotAccounts.end(),
                      candidate) != sPlayerbotAIConfig.RandomBotAccounts.end())
        {
            Emit("error", ",\"reason\":\"requires_dedicated_offline_nonrandom_account\"");
            return;
        }
        std::set<ObjectGuid> distinct;
        std::array<std::string, PartySize> snapshots;
        uint32 team = 0;
        for (std::size_t i = 0; i < PartySize; ++i)
        {
            std::string name = arguments[i + 1];
            if (!normalizePlayerName(name))
            {
                Emit("error", ",\"reason\":\"invalid_template_name\"");
                return;
            }
            auto source = sCharacterCache->GetCharacterGuidByName(name);
            auto cache = sCharacterCache->GetCharacterCacheByGuid(source);
            uint32 memberTeam = sCharacterCache->GetCharacterTeamByGuid(source);
            if (!cache || cache->Level < 13 || cache->Level > 20 || Online(Member{source}) ||
                cache->AccountId == candidate || !distinct.insert(source).second || (i && team != memberTeam))
            {
                Emit("error", ",\"reason\":\"templates_must_be_distinct_offline_same_faction_level_13_to_20\"");
                return;
            }
            if ((i == 0 && cache->Class != CLASS_WARRIOR && cache->Class != CLASS_PALADIN &&
                 cache->Class != CLASS_DRUID) ||
                (i == 1 && cache->Class != CLASS_PRIEST && cache->Class != CLASS_PALADIN &&
                 cache->Class != CLASS_DRUID && cache->Class != CLASS_SHAMAN))
            {
                Emit("error", ",\"reason\":\"template_class_cannot_fill_configured_role\"");
                return;
            }
            team = memberTeam;
            std::string dump;
            if (PlayerDumpWriter().WriteDumpToString(dump, source.GetCounter()) != DUMP_SUCCESS)
            {
                Emit("error", ",\"reason\":\"template_export_failed\"");
                return;
            }
            snapshots[i] = WaiEpisodes::SanitizeTemplate(dump, memberTeam == ALLIANCE);
        }
        if (!learnedNavigation && !LoadDestinations())
        {
            Emit("error", ",\"reason\":\"requires_one_normal_spawn_for_each_of_four_bosses\"");
            return;
        }
        if (!ownership.Load(std::filesystem::path(stateDirectory) / (std::to_string(candidate) + ".txt")))
        {
            Emit("error", ",\"reason\":\"ownership_ledger_unreadable_or_corrupt\"");
            return;
        }
        std::vector<Member> previous;
        auto statement = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARS_BY_ACCOUNT_ID);
        statement->SetData(0, candidate);
        if (PreparedQueryResult result = CharacterDatabase.Query(statement))
            do
            {
                auto guid = ObjectGuid::Create<HighGuid::Player>(result->Fetch()[0].Get<uint32>());
                auto cache = sCharacterCache->GetCharacterCacheByGuid(guid);
                if (!cache || cache->AccountId != candidate || cache->Level < 13 || cache->Level > 20 ||
                    !WaiEpisodes::IsCloneName(cache->Name) || Online(Member{guid}) ||
                    sRandomPlayerbotMgr.GetPlayerBot(guid) || previous.size() >= PartySize ||
                    (!recover && !ownership.Owns(guid.GetCounter(), cache->Name)))
                {
                    Emit("error",
                         ",\"reason\":\"account_contains_unowned_or_online_characters_use_recover_for_legacy_clones\"");
                    return;
                }
                previous.push_back(Member{guid, cache->Name});
            } while (result->NextRow());
        // Explicit legacy recovery validates the ENTIRE roster before persisting/deleting any member.
        for (auto const& old : previous)
            if (!ownership.Remember(old.guid.GetCounter(), old.name))
            {
                Emit("error", ",\"reason\":\"clone_ownership_write_failed\"");
                return;
            }
        account = candidate;
        if (!StartLearner())
        {
            Emit("error", ",\"reason\":\"learner_not_configured\"");
            cycleRequested = false;
            return;
        }
        for (std::size_t i = 0; i < PartySize; ++i)
            members[i] = Member{ObjectGuid::Empty, "", std::move(snapshots[i]), false};
        if (!previous.empty())
        {
            for (std::size_t i = 0; i < previous.size(); ++i)
            {
                members[i].guid = previous[i].guid;
                members[i].name = previous[i].name;
            }
            Emit("recovered_clones", ",\"count\":" + std::to_string(previous.size()));
            if (RemoveClones())
                repeatPending = true;
            else
                phase = Phase::Finished;
            return;
        }
        CloneRoster();
    }

    static void Prepare(Player* player)
    {
        player->SetPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
        player->SetDungeonDifficulty(DUNGEON_DIFFICULTY_NORMAL);
        player->SetMoney(0);
        if (!player->IsAlive())
            player->ResurrectPlayer(1.0f);
        player->SetFullHealth();
        player->DurabilityRepairAll(false, 0, false);
        Powers power = player->getPowerType();
        player->SetPower(power, power == POWER_RAGE ? 0 : player->GetMaxPower(power));
        if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player))
        {
            ai->SetMaster(nullptr);
            ai->ResetStrategies();
            ai->ChangeStrategy("+stay,-follow,-grind,-rpg,-new rpg,-travel,-lfg,-bg,-loot,-gather",
                               BOT_STATE_NON_COMBAT);
        }
    }

    static void Launch()
    {
        std::array<Player*, PartySize> players{};
        for (std::size_t i = 0; i < PartySize; ++i)
        {
            auto& member = members[i];
            players[i] = Online(member);
            if (!players[i])
            {
                if (!member.loginRequested)
                {
                    auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_GUID);
                    stmt->SetData(0, member.guid.GetCounter());
                    if (CharacterDatabase.Query(stmt))
                    {
                        sRandomPlayerbotMgr.AddPlayerBot(member.guid, 0);
                        member.loginRequested = true;
                    }
                }
                return;
            }
            if (!players[i]->IsInWorld() || players[i]->IsBeingTeleported())
                return;
            if (!WaiPlayerbotsAdapter::HeadlessSession(players[i]->GetSession()) || !sPlayerbotsMgr.GetPlayerbotAI(players[i]))
                return;
            if (!member.prepared)
            {
                Prepare(players[i]);
                member.prepared = true;
            }
        }
        for (auto player : players)
            Prepare(player);
        LOG_INFO("server.wai.events",
                 "WAI_OBSERVER All five episode bots are logged in: {}, {}, {}, {}, {}. "
                 "Preparing dungeon entry; GM observation commands follow once the run begins.",
                 members[0].name, members[1].name, members[2].name, members[3].name, members[4].name);
        auto group = std::make_unique<Group>();
        if (!group->Create(players[0]))
        {
            Finish("group_create_failed");
            return;
        }
        Group* party = group.release();
        sGroupMgr->AddGroup(party);
        party->SetDungeonDifficulty(DUNGEON_DIFFICULTY_NORMAL);
        for (std::size_t i = 1; i < PartySize; ++i)
            if (!party->AddMember(players[i]))
            {
                party->Disband();
                Finish("group_add_failed");
                return;
            }
        sPlayerbotsMgr.GetPlayerbotAI(players[0])->ChangeStrategy("+tank", BOT_STATE_COMBAT);
        if (!PlayerbotAI::IsHeal(players[1]))
            sPlayerbotsMgr.GetPlayerbotAI(players[1])->ChangeStrategy("+heal", BOT_STATE_COMBAT);
        phase = Phase::Entering;
        WaiEpisodes::Integrity::PermitEntry(players[0]->GetGUID());
        if (!players[0]->TeleportTo(DungeonMap, 3.81f, -14.82f, -17.84f, 4.39f))
            Finish("leader_entry_failed");
    }

    static void Enter()
    {
        Player* leader = Online(members[0]);
        if (!leader || !leader->IsInWorld() || leader->IsBeingTeleported() || leader->GetMapId() != DungeonMap)
            return;
        instance = leader->GetInstanceId();
        if (!instance)
        {
            Finish("missing_instance");
            return;
        }
        for (auto const& member : members)
        {
            Player* player = Online(member);
            if (!player || player->IsBeingTeleported() || !player->IsInWorld())
                return;
            if (player->GetMapId() != DungeonMap)
            {
                WaiEpisodes::Integrity::PermitEntry(player->GetGUID());
                if (!player->TeleportTo(DungeonMap, 3.81f, -14.82f, -17.84f, 4.39f))
                    Finish("member_entry_failed");
                return;
            }
            if (player->GetInstanceId() != instance)
            {
                Finish("split_instance");
                return;
            }
        }
        for (auto const& member : members)
            if (!WaiEpisodes::Integrity::Ready(Online(member)))
            {
                Finish("invalid_movement_environment");
                return;
            }
        started = Tick();
        nextDrive = started + 10000;
        nextHeartbeat = started + 30000;
        activeInstance = instance;
        phase = Phase::Running;
        Emit("start",
             Acore::StringFormat(",\"policy\":\"{}\",\"movement_integrity_enabled\":", PolicyName()) +
                 (sConfigMgr->GetOption<bool>("Wai.Episodes.MovementIntegrity.Enable", false) ? "true" : "false") +
                 Acore::StringFormat(",\"boundary_enabled\":{},\"boundary_tolerance_yards\":{},"
                                     "\"boundary_grace_ms\":3000",
                                     boundaryEnabled, boundaryTolerance));
    }

    static void ProcessDeaths()
    {
        while (!deaths.empty())
        {
            Death death = deaths.front();
            deaths.pop_front();
            if (phase != Phase::Running)
                continue;
            if (!death.player.IsEmpty())
            {
                ++progress.deaths;
                auto it = std::find_if(members.begin(), members.end(),
                                       [&](Member const& member) { return member.guid == death.player; });
                Emit("death", ",\"member_slot\":" + std::to_string(std::distance(members.begin(), it) + 1), death.wall,
                     death.tick);
            }
            else if (progress.Kill(death.entry))
            {
                auto it = std::find(WaiEpisodes::Bosses.begin(), WaiEpisodes::Bosses.end(), death.entry);
                auto name = WaiEpisodes::BossNames[std::distance(WaiEpisodes::Bosses.begin(), it)];
                std::string headline = name;
                std::transform(headline.begin(), headline.end(), headline.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                LOG_INFO("server.wai.events",
                         "WAI_BOSS_KILLED: {} | PARTY {} | BOSSES {}/{} | ELAPSED {:.1f} SECONDS | DEATHS {}", headline,
                         Slot + 1, progress.killed.size(), WaiEpisodes::Bosses.size(),
                         started && death.tick >= started ? (death.tick - started) / 1000.0 : 0.0, progress.deaths);
                Emit("boss_kill", ",\"boss_entry\":" + std::to_string(death.entry) + ",\"boss\":\"" + name + "\"",
                     death.wall, death.tick);
                if (progress.Complete())
                    Finish("completed_all_bosses", death.wall, death.tick);
            }
        }
    }

    static void AttachExistingParty()
    {
        if (learnedNavigation)
        {
            Emit("error", ",\"reason\":\"learned_loop_requires_cloned_episode_use_start\"");
            return;
        }
        if (phase != Phase::Empty || existingParty)
        {
            Emit("error", ",\"reason\":\"cleanup_or_detach_previous_episode_first\"");
            return;
        }
        std::array<Player*, PartySize> players{};
        Group* group = nullptr;
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            std::string name = configured[i + 1];
            if (!normalizePlayerName(name))
            {
                Emit("error", ",\"reason\":\"invalid_existing_party_name\"");
                return;
            }
            Player* player = ObjectAccessor::FindPlayerByName(name);
            if (!player || !WaiPlayerbotsAdapter::HeadlessSession(player->GetSession()) || !sPlayerbotsMgr.GetPlayerbotAI(player) ||
                !player->IsAlive() || player->IsInCombat() || player->IsBeingTeleported() ||
                player->GetMapId() != DungeonMap || !player->GetInstanceId() || !player->GetGroup())
            {
                Emit("error", ",\"reason\":\"existing_party_requires_five_live_idle_bots_in_ragefire\"");
                return;
            }
            if (!i)
                group = player->GetGroup();
            if (player->GetGroup() != group || group->GetMembersCount() != PartySize ||
                (i && player->GetInstanceId() != players[0]->GetInstanceId()))
            {
                Emit("error", ",\"reason\":\"existing_party_group_or_instance_mismatch\"");
                return;
            }
            for (std::size_t j = 0; j < i; ++j)
                if (players[j] == player)
                    return;
            players[i] = player;
        }
        if (group->GetLeaderGUID() != players[0]->GetGUID() || (!learnedNavigation && !LoadDestinations()))
        {
            Emit("error", ",\"reason\":\"existing_party_requires_configured_tank_leader_and_boss_destinations\"");
            return;
        }
        journal.Close();
        episode = std::to_string(Wall()) + "-" + std::to_string(urand(0, 999999));
        journalFailed = false;
        if (!journal.Open(outputDirectory, episode))
        {
            journalFailed = true;
            return;
        }
        members = {};
        progress = {};
        deaths.clear();
        policySequence = pendingSequence = controllerHeartbeat = 0;
        policyPublished = actionQueued = 0;
        decisionExecuting = false;
        pendingAction = -1;
        policyFeatures.clear();
        actionResult = "none";
        terminalOutcome.clear();
        policyVersion.clear();
        trainingStage.clear();
        trainedVersion.clear();
        trainingReported = false;
        ended = 0;
        started = Tick();
        instance = players[0]->GetInstanceId();
        frozen = wipeDetected = false;
        nextPathWarning = 0;
        std::array<ObjectGuid, PartySize> roster{};
        for (std::size_t i = 0; i < players.size(); ++i)
        {
            auto ai = sPlayerbotsMgr.GetPlayerbotAI(players[i]);
            members[i].guid = roster[i] = players[i]->GetGUID();
            members[i].name = players[i]->GetName();
            if (Player* master = ai->GetMaster())
                members[i].previousMaster = master->GetGUID();
            members[i].previousStrategies = ai->GetStrategies(BOT_STATE_NON_COMBAT);
        }
        existingParty = true;
        WaiEpisodes::Integrity::SetRoster(roster);
        SetTelemetryRoster(roster);
        activeInstance = instance;
        phase = Phase::Running;
        for (auto player : players)
            if (!WaiEpisodes::Integrity::Ready(player))
            {
                Finish("invalid_movement_environment");
                return;
            }
        nextDrive = started + 10000;
        nextHeartbeat = started;
        Emit("start", Acore::StringFormat(",\"mode\":\"existing_party\",\"policy\":\"{}\",\"xp_flag_preserved\":true",
                                          PolicyName()));
    }

    static void RecoverMembers()
    {
        if (!recoveryEnabled || phase != Phase::Running)
            return;
        Player* anchor = nullptr;
        std::size_t anchorSlot = 0;
        bool inCombat = false;
        for (std::size_t i = 0; i < members.size(); ++i)
            if (Player* player = Online(members[i]); player && player->IsInWorld() && player->IsAlive() &&
                                                     !player->IsBeingTeleported() && player->GetMapId() == DungeonMap &&
                                                     player->GetInstanceId() == instance)
            {
                if (!anchor)
                {
                    anchor = player;
                    anchorSlot = i;
                }
                inCombat = inCombat || player->IsInCombat() || (player->GetPet() && player->GetPet()->IsInCombat());
            }
        if (!anchor)
        {
            // A transient teleport is not proof of a full-party death.
            bool allDead = true;
            for (auto const& member : members)
                if (Player* player = Online(member); !player || player->IsAlive())
                    allDead = false;
            if (allDead)
                Finish("wipe");
            return;
        }
        bool needsRecovery = std::any_of(members.begin(), members.end(),
                                         [](Member const& member)
                                         {
                                             Player* player = Online(member);
                                             return member.recovering || (player && !player->IsAlive());
                                         });
        if (needsRecovery && !inCombat)
            for (auto& member : members)
                if (Player* player = Online(member);
                    player && player->IsAlive() && player->GetMap() == anchor->GetMap())
                    if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player))
                    {
                        ai->ChangeStrategy("+stay,-grind,-follow", BOT_STATE_NON_COMBAT);
                        player->StopMoving();
                        member.strategiesSet = false;
                    }
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            auto& member = members[i];
            Player* player = Online(member);
            if (!player)
                continue;
            if (!player->IsAlive() && !member.recovering)
            {
                member.recovering = true;
                member.recoveryStarted = Tick();
                member.recoveryAttempts = 0;
                member.nextRecoveryAttempt = 0;
                Emit("recovery_begin", MemberState(i, player));
            }
            if (!member.recovering)
                continue;
            if (Tick() - member.recoveryStarted > uint64(recoveryTimeoutSeconds) * 1000)
            {
                Emit("recovery_failed", MemberState(i, player) + ",\"reason\":\"recovery_timeout\"");
                Finish("recovery_timeout");
                return;
            }
            if (player->IsInWorld() && !player->IsBeingTeleported() && player->IsAlive() &&
                player->GetMapId() == DungeonMap && player->GetInstanceId() == instance)
            {
                member.recovering = false;
                if (!WaiEpisodes::Integrity::Ready(player))
                {
                    Finish("invalid_recovery_surface");
                    return;
                }
                Emit("recovery_complete", MemberState(i, player));
                continue;
            }
            if (!player->IsInWorld() || player->IsBeingTeleported() || Tick() < member.nextRecoveryAttempt)
                continue;
            member.nextRecoveryAttempt = Tick() + 5000;
            if (inCombat || anchor->isMoving() || anchor->IsNonMeleeSpellCast(false))
            {
                Emit("recovery_wait", MemberState(i, player) + ",\"reason\":\"survivors_busy\"");
                continue;
            }
            PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player);
            if (!ai)
                continue;
            if (!player->IsAlive() && !player->HasPlayerFlag(PLAYER_FLAGS_GHOST))
            {
                WaiEpisodes::Integrity::PermitRecovery(player->GetGUID(), nullptr);
                bool accepted = ReleaseSpiritAction(ai).Execute(Event());
                Emit("recovery_release", MemberState(i, player) + Acore::StringFormat(",\"accepted\":{}", accepted));
                continue;
            }
            if (sPlayerbotAIConfig.ReviveBotWhenSummoned == 0)
            {
                Emit("recovery_failed", MemberState(i, player) + ",\"reason\":\"summon_revive_disabled\"");
                Finish("recovery_configuration");
                return;
            }
            WaiEpisodes::Integrity::PermitRecovery(player->GetGUID(), anchor, sPlayerbotAIConfig.FollowDistance + 1.0f);
            ++member.recoveryAttempts;
            bool accepted = SummonAction(ai).Teleport(anchor, player, true);
            Emit("recovery_summon",
                 MemberState(i, player) + Acore::StringFormat(",\"anchor_slot\":{},\"attempt\":{},\"accepted\":{}",
                                                              anchorSlot + 1, member.recoveryAttempts, accepted));
        }
    }

    static void WorldUpdate()
    {
        if (learnerProcess &&
            learnerProcess->GetFutureResult().wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto completed = std::move(learnerProcess);
            int result = EXIT_FAILURE;
            try
            {
                result = completed->GetFutureResult().get();
            }
            catch (std::exception const&)
            {
                LOG_ERROR("server.wai.events", "WAI_LEARNING Process supervisor reported a launch failure.");
            }
            LOG_ERROR("server.wai.events",
                      "WAI_LEARNING Controller/trainer exited with code {}; "
                      "automatic cycling stopped. Inspect learning receipts before restarting.",
                      result);
            cycleRequested = false;
            sharedControllerActive = false;
            if (learnedNavigation && (phase == Phase::Loading || phase == Phase::Entering || phase == Phase::Running))
                Finish("controller_disconnected");
        }
        if constexpr (Slot > 0)
            if (cycleRequested && !sharedControllerActive && phase != Phase::Empty && phase != Phase::Finished)
            {
                cycleRequested = false;
                Finish("controller_disconnected");
            }
        ProcessDeaths();
        DrainTelemetry();
        auto movementViolation = WaiEpisodes::Integrity::TakeViolation();
        if (!movementViolation.empty())
        {
            Emit("movement_violation", ",\"reason\":\"" + movementViolation + "\"");
            Finish("invalid_movement_environment");
        }
        if (!enabled && (phase == Phase::Loading || phase == Phase::Entering || phase == Phase::Running))
            Finish("disabled");
        while (!requests.empty())
        {
            Request request = std::move(requests.front());
            requests.pop_front();
            if (request.operation == "status")
                Emit("status", std::string(",\"phase\":\"") + PhaseName() +
                                   Acore::StringFormat("\",\"account_preparing\":{}", accountSetup.Pending()));
            else if (request.operation == "stop")
            {
                accountSetup.Clear();
                accountSetupArguments.clear();
                cycleRequested = false;
                if (learnerProcess)
                    learnerProcess->Terminate();
                if (phase == Phase::Resetting)
                    Emit("error", ",\"reason\":\"cleanup_already_in_progress\"");
                else
                    Finish("stopped_by_admin");
            }
            else if (request.operation == "attach" && enabled)
                AttachExistingParty();
            else if (request.operation == "detach" && existingParty)
            {
                Finish("detached_by_admin");
                existingParty = false;
                members = {};
                phase = Phase::Empty;
                activeInstance = 0;
                WaiEpisodes::Integrity::ClearRoster();
                journal.Close();
            }
            else if ((request.operation == "start" || request.operation == "recover") && enabled)
            {
                cycleRequested = true;
                Create(std::vector<std::string>(configured.begin(), configured.end()), request.operation == "recover");
            }
            else if (request.operation == "cleanup" || request.operation == "repeat")
            {
                bool repeat = request.operation == "repeat";
                if (!repeat && phase == Phase::Empty)
                {
                    accountSetup.Clear();
                    accountSetupArguments.clear();
                    cycleRequested = false;
                    Emit("cleanup");
                    continue;
                }
                if (phase != Phase::Finished || (repeat && !enabled))
                    Emit("error", ",\"reason\":\"stop_or_finish_episode_before_cleanup_or_repeat\"");
                else if (repeat && learnedNavigation && trainingStage != "completed")
                    Emit("error", ",\"reason\":\"learned_repeat_requires_completed_training\"");
                else if (repeat && !StartLearner())
                    Emit("error", ",\"reason\":\"learner_not_configured\"");
                else if (RemoveClones())
                {
                    cycleRequested = repeat;
                    repeatPending = repeat;
                }
            }
            else
                Emit("error", ",\"reason\":\"episodes_disabled\"");
        }
        if (accountSetup.Pending())
        {
            if (!enabled || !cycleRequested || !sharedControllerActive)
            {
                accountSetup.Clear();
                accountSetupArguments.clear();
                Emit("account_preparation_cancelled");
            }
            else
            {
                auto state = accountSetup.Poll(AccountMgr::GetId(accountSetupArguments.front()) != 0, Tick());
                if (state == WaiEpisodes::AccountSetupState::Ready)
                {
                    auto arguments = std::move(accountSetupArguments);
                    accountSetup.Clear();
                    Emit("account_ready");
                    Create(arguments, accountSetupRecover);
                }
                else if (state == WaiEpisodes::AccountSetupState::TimedOut)
                {
                    accountSetup.Clear();
                    accountSetupArguments.clear();
                    cycleRequested = false;
                    Emit("error", ",\"reason\":\"dedicated_account_creation_timeout\"");
                }
            }
        }
        if (phase == Phase::Resetting)
        {
            bool pending = false;
            for (auto guid : retired)
            {
                auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_GUID);
                stmt->SetData(0, guid.GetCounter());
                if (CharacterDatabase.Query(stmt))
                    pending = true;
            }
            if (pending && Tick() - setupStarted > 120000)
                Finish("cleanup_timeout");
            else if (!pending)
            {
                retired.clear();
                phase = Phase::Empty;
                Emit("cleanup");
                if (repeatPending)
                    CloneRoster();
                else
                    members = {};
            }
        }
        if (phase == Phase::Running)
            for (std::size_t i = 0; i < members.size(); ++i)
                if (Player* player = Online(members[i]); player && !WaiPlayerbotsAdapter::HeadlessSession(player->GetSession()))
                {
                    Emit("human_control", MemberState(i, player));
                    Finish("human_control_detected");
                    break;
                }
        RecoverMembers();
        if (phase == Phase::Running && wipeDetected)
            Finish("wipe");
        if (phase == Phase::Loading || phase == Phase::Entering)
        {
            if (Tick() - setupStarted > 120000)
                Finish("setup_timeout");
            else if (phase == Phase::Loading)
                Launch();
            else
                Enter();
        }
        if (phase == Phase::Running)
        {
            if (learnedNavigation && controllerHeartbeat && Tick() - controllerHeartbeat > 15000)
                Finish("controller_disconnected");
            if (Tick() - started >= uint64(timeoutSeconds) * 1000)
                Finish("timeout");
            for (auto const& member : members)
            {
                Player* player = Online(member);
                auto presence = WaiEpisodes::CheckPresence(
                    player != nullptr, player && player->IsInWorld(),
                    player && player->GetMapId() == DungeonMap && player->GetInstanceId() == instance,
                    player && player->IsBeingTeleported(), recoveryEnabled && member.recovering);
                if (presence != WaiEpisodes::Presence::Present)
                {
                    Emit("member_presence", MemberState(&member - members.data(), player));
                    if (presence == WaiEpisodes::Presence::Recovering)
                        continue;
                    Finish(presence == WaiEpisodes::Presence::Disconnected ? "member_disconnected"
                                                                           : "member_left_instance");
                    break;
                }
            }
            if (phase == Phase::Running && Tick() >= nextHeartbeat)
            {
                Emit("heartbeat");
                for (std::size_t i = 0; i < members.size(); ++i)
                    Emit("member_state", MemberState(i, Online(members[i])));
                nextHeartbeat = Tick() + 30000;
            }
        }
    }

    static void Drive(Map* map)
    {
        std::array<Player*, PartySize> players{};
        for (std::size_t i = 0; i < PartySize; ++i)
        {
            players[i] = Online(members[i]);
        }
        if (phase == Phase::Finished)
        {
            if (!frozen)
            {
                for (auto player : players)
                    if (player && player->GetMap() == map)
                        if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player))
                        {
                            ai->ChangeStrategy("+stay,-grind,-follow", BOT_STATE_NON_COMBAT);
                            player->GetMotionMaster()->Clear();
                            player->StopMoving();
                        }
                frozen = true;
            }
            return;
        }
        for (auto player : players)
            if (!player || player->GetMap() != map)
                return;
        if (phase != Phase::Running || Tick() < nextDrive)
            return;
        nextDrive = Tick() + 2000;
        if (boundaryEnabled)
            for (std::size_t i = 0; i < players.size(); ++i)
            {
                auto reading = members[i].recovering
                                   ? WaiEpisodes::BoundaryReading{}
                                   : WaiEpisodes::Integrity::InspectBoundary(players[i], boundaryTolerance);
                if (reading.state == WaiEpisodes::BoundaryState::Unavailable)
                {
                    Emit("boundary_environment_error", Acore::StringFormat(",\"member_slot\":{}", i + 1));
                    Finish("invalid_movement_environment");
                    return;
                }
                if (reading.state == WaiEpisodes::BoundaryState::Unknown && Tick() >= nextBoundaryWarning)
                {
                    Emit("boundary_uncertain", Acore::StringFormat(",\"member_slot\":{}", i + 1));
                    nextBoundaryWarning = Tick() + 30000;
                }
                if (boundaryWatches[i].Update(reading, boundaryTolerance, Tick()))
                {
                    Emit("boundary_escape", Acore::StringFormat(",\"member_slot\":{},\"distance_yards\":{},"
                                                                "\"tolerance_yards\":{}",
                                                                i + 1, reading.distance, boundaryTolerance));
                    Finish("wandering_off_limits");
                    return;
                }
            }
        if (std::any_of(members.begin(), members.end(), [](Member const& member) { return member.recovering; }))
            return;
        unsigned alive = 0;
        bool ready = true;
        for (std::size_t i = 0; i < PartySize; ++i)
        {
            Player* player = players[i];
            if (!existingParty)
                player->SetPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
            alive += player->IsAlive() ? 1 : 0;
            if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player); ai && !members[i].strategiesSet)
            {
                ai->SetMaster(i ? players[0] : nullptr);
                ai->ChangeStrategy(i ? "+follow,+loot,+gather,-stay,-grind"
                                     : (learnedNavigation
                                            ? "+loot,+gather,-stay,-grind,-follow,-travel,-rpg,-new rpg,-lfg,-bg"
                                            : "+grind,-stay,-follow"),
                                   BOT_STATE_NON_COMBAT);
                members[i].strategiesSet = true;
            }
            // Yield navigation to native corpse looting/gathering, including travel to its selected target.
            // Respect native retry deferrals so an inaccessible corpse cannot reserve navigation forever.
            if (learnedNavigation)
                if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player))
                {
                    auto context = ai->GetAiObjectContext();
                    auto loot = context->GetValue<LootObject>("loot target")->Get();
                    auto available = context->GetValue<LootObjectStack*>("available loot")->Get();
                    if (player->GetLootGUID() || available->IsLootPending() ||
                        (!loot.IsEmpty() && available->CanAttemptLoot(loot.guid) && loot.IsLootPossible(player)))
                        ready = false;
                }
            if (!player->IsAlive() || player->IsInCombat() || player->GetHealthPct() < 80.0f ||
                player->IsNonMeleeSpellCast(false) || player->GetExactDist(players[0]) > 30.0f ||
                (player->getPowerType() == POWER_MANA && player->GetPowerPct(POWER_MANA) < 60.0f))
                ready = false;
        }
        if (!alive)
        {
            // Death hooks are drained by the world thread before the terminal summary.
            wipeDetected = true;
            return;
        }
        if (learnedNavigation)
        {
            bool refresh =
                !policyFeatures.empty() && !decisionExecuting && pendingAction < 0 && Tick() - policyPublished >= 5000;
            bool publish = policyFeatures.empty() || decisionExecuting || refresh;
            if (decisionExecuting)
            {
                decisionExecuting = false;
                ++policySequence;
            }
            else if (refresh)
                ++policySequence;
            if (pendingAction >= 0)
            {
                int action = pendingAction;
                pendingAction = -1;
                actionResult = action ? "party_busy" : "wait";
                if (Tick() - actionQueued > 5000)
                    actionResult = "expired";
                else if (action && ready && !players[0]->isMoving())
                {
                    constexpr float directions[8][2] = {
                        {1, 0},  {0.707107f, 0.707107f},   {0, 1},  {-0.707107f, 0.707107f},
                        {-1, 0}, {-0.707107f, -0.707107f}, {0, -1}, {0.707107f, -0.707107f}};
                    Position destination = players[0]->GetPosition();
                    float heading = players[0]->GetOrientation();
                    float forward = directions[action - 1][0], left = directions[action - 1][1];
                    destination.m_positionX += (forward * std::cos(heading) - left * std::sin(heading)) * 6.0f;
                    destination.m_positionY += (forward * std::sin(heading) + left * std::cos(heading)) * 6.0f;
                    if (PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(players[0]))
                        actionResult = RouteMove(ai).Advance(destination) ? "movement_started" : "movement_rejected";
                }
                // Observe the result on the next drive interval, after movement/combat has advanced.
                decisionExecuting = true;
                return;
            }
            if (!publish)
                return;
            if (!policySequence)
                policySequence = 1;
            // Publish features after each applied decision. No undiscovered boss positions or route hints.
            float mean = 0, minimum = 1, combat = 0;
            for (auto player : players)
            {
                float health = player->GetHealthPct() / 100.0f;
                mean += health / PartySize;
                minimum = std::min(minimum, health);
                combat += player->IsInCombat() ? 1.0f / PartySize : 0;
            }
            auto tank = players[0];
            std::array<float, 16> values{tank->GetPositionX() / 1000.0f,
                                         tank->GetPositionY() / 1000.0f,
                                         tank->GetPositionZ() / 1000.0f,
                                         std::sin(tank->GetOrientation()),
                                         std::cos(tank->GetOrientation()),
                                         tank->GetHealthPct() / 100.0f,
                                         float(tank->GetPower(tank->getPowerType())) /
                                             std::max<uint32>(1, tank->GetMaxPower(tank->getPowerType())),
                                         tank->IsInCombat() ? 1.0f : 0.0f,
                                         mean,
                                         minimum,
                                         float(alive) / PartySize,
                                         combat,
                                         float(progress.killed.size()) / 4.0f,
                                         std::min(1.0f, float(Tick() - started) / (timeoutSeconds * 1000.0f)),
                                         ready ? 1.0f : 0.0f,
                                         tank->isMoving() ? 1.0f : 0.0f};
            std::ostringstream features;
            features.imbue(std::locale::classic());
            for (std::size_t i = 0; i < values.size(); ++i)
                features << (i ? "," : "") << values[i];
            policyFeatures = features.str();
            policyPublished = Tick();
            return;
        }
        if (!ready || players[0]->isMoving())
        {
            if (Tick() >= nextPathWarning)
            {
                Emit("route_pause", ",\"reason\":\"party_not_ready\"");
                for (std::size_t i = 0; i < players.size(); ++i)
                    Emit("member_state", MemberState(i, players[i]));
                nextPathWarning = Tick() + 30000;
            }
            return;
        }
        for (std::size_t i = 0; i < WaiEpisodes::Bosses.size(); ++i)
        {
            if (progress.killed.count(WaiEpisodes::Bosses[i]))
                continue;
            auto const& destination = destinations[i];
            PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(players[0]);
            if (ai && !RouteMove(ai).Advance(destination) && Tick() >= nextPathWarning)
            {
                Emit("route_wait", ",\"boss_entry\":" + std::to_string(WaiEpisodes::Bosses[i]));
                nextPathWarning = Tick() + 30000;
            }
            return;
        }
    }

    class Commands : public CommandScript
    {
      public:
        Commands() : CommandScript(Acore::StringFormat("WaiEpisodeCommands{}", Slot + 1).c_str()) {}
        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable commands{{"waiepisode", Handle, SEC_ADMINISTRATOR, Console::Yes}};
            return commands;
        }
        static bool Handle(ChatHandler* handler, Tail text)
        {
            std::istringstream stream{std::string(text)};
            Request request;
            stream >> request.operation;
            std::string value;
            while (stream >> value)
                request.arguments.push_back(value);
            if (request.operation == "observe" || request.operation == "act" || request.operation == "training")
            {
                if (!handler->IsConsole())
                {
                    handler->SendSysMessage("WAI learning control requires the authenticated administrator console.");
                    return true;
                }
                std::lock_guard lock(guard);
                if (!learnedNavigation)
                {
                    handler->SendSysMessage("WAI_LEARNING_DISABLED");
                    return true;
                }
                if (request.operation == "observe" && request.arguments.empty())
                {
                    if (phase == Phase::Running)
                        controllerHeartbeat = Tick();
                    handler->SendSysMessage(Acore::StringFormat(
                        "{{\"schema\":1,\"episode\":\"{}\",\"phase\":\"{}\",\"seq\":{},\"features\":[{}],"
                        "\"elapsed_ms\":{},\"bosses\":{},\"deaths\":{},\"outcome\":\"{}\",\"auto_repeat\":{},"
                        "\"mode\":\"{}\",\"action_result\":\"{}\"}}",
                        episode, PhaseName(), policySequence, policyFeatures,
                        started ? (ended ? ended : Tick()) - started : 0, progress.killed.size(), progress.deaths,
                        terminalOutcome, cycleRequested, existingParty ? "existing_party" : "cloned", actionResult));
                    return true;
                }
                auto safeToken = [](std::string const& token)
                {
                    return !token.empty() && token.size() <= 64 &&
                           std::all_of(token.begin(), token.end(),
                                       [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
                };
                if (request.operation == "training" && request.arguments.size() == 11 &&
                    request.arguments[0] == "report")
                {
                    auto const& args = request.arguments;
                    std::array<double, 8> numbers{};
                    bool valid = phase == Phase::Finished && args[1] == episode && trainingStage == "completed" &&
                                 args[2] == trainedVersion && !trainingReported;
                    for (std::size_t i = 0; i < numbers.size(); ++i)
                    {
                        std::istringstream input(args[i + 3]);
                        input.imbue(std::locale::classic());
                        valid = valid && bool(input >> numbers[i]) && input.peek() == EOF && std::isfinite(numbers[i]);
                    }
                    valid = valid && numbers[0] >= 1 && numbers[0] <= 16384 && numbers[2] >= 0 && numbers[3] >= 0 &&
                            numbers[3] <= 8 && numbers[3] == std::floor(numbers[3]) && numbers[4] >= 0 &&
                            numbers[4] <= 1 && numbers[5] >= 0 && numbers[5] <= 1 && numbers[6] >= 0 &&
                            numbers[6] <= 1 && (numbers[7] == 0 || numbers[7] == 1);
                    if (!valid)
                    {
                        handler->SendSysMessage("WAI_TRAINING_REPORT_REJECTED");
                        return true;
                    }
                    constexpr char const* actions[] = {"wait",       "forward",   "forward-left",
                                                       "left",       "back-left", "back",
                                                       "back-right", "right",     "forward-right"};
                    trainingReported = true;
                    LOG_INFO("server.wai.events",
                             "RUN TRAINED. PARTY {}. REASON: {}. CURRENT TIME: {:.1f} S. BEST TIME (SESSION): {}. "
                             "WEIGHTS CHANGES (L2): {:.6f}. CHECKPOINT: {}.",
                             Slot + 1, ResultReason(), (ended - started) / 1000.0,
                             bestTime ? Acore::StringFormat("{:.1f} S", bestTime / 1000.0) : "NONE", numbers[2],
                             trainedVersion);
                    LOG_INFO(
                        "server.wai.events",
                        "WAI_LEARNING Training complete: {:.0f} recorded decisions from {} parties; checkpoint {}.",
                        numbers[0], parallelParties.load(), trainedVersion);
                    LOG_INFO("server.wai.events",
                             "WAI_LEARNING Party {} result: {} bosses, {} deaths, {:.1f} seconds; outcome {}.",
                             Slot + 1, progress.killed.size(), progress.deaths, (ended - started) / 1000.0,
                             terminalOutcome);
                    LOG_INFO("server.wai.events", "WAI_LEARNING Total reward {:.2f}; weight change (L2) {:.6f}.",
                             numbers[1], numbers[2]);
                    LOG_INFO("server.wai.events",
                             "WAI_LEARNING Largest sampled probability change: tank HP {:.0f}%, combat {}.",
                             numbers[6] * 100, numbers[7] != 0);
                    LOG_INFO("server.wai.events",
                             "WAI_LEARNING In that same recorded state, '{}' probability: {:.1f}% -> {:.1f}%.",
                             actions[static_cast<unsigned>(numbers[3])], numbers[4] * 100, numbers[5] * 100);
                    LOG_INFO("server.wai.events",
                             "WAI_LEARNING A policy change is not proof of improved play; compare later clears.");
                    handler->SendSysMessage("WAI_TRAINING_REPORT_RECORDED");
                    return true;
                }
                if (request.operation == "act" && request.arguments.size() == 4)
                {
                    auto const& args = request.arguments;
                    uint64 sequence = 0;
                    int action = -1;
                    std::istringstream seqInput(args[1]), actionInput(args[2]);
                    bool parsed = bool(seqInput >> sequence) && seqInput.peek() == EOF && bool(actionInput >> action) &&
                                  actionInput.peek() == EOF;
                    if (phase == Phase::Running && args[0] == episode && parsed && sequence == policySequence &&
                        !policyFeatures.empty() && pendingAction < 0 && pendingSequence != sequence &&
                        Tick() - policyPublished < 5000 && action >= 0 && action <= 8 && safeToken(args[3]) &&
                        (policyVersion.empty() || policyVersion == args[3]))
                    {
                        if (policyVersion.empty())
                            Emit("policy_connected", Acore::StringFormat(",\"model_version\":\"{}\"", args[3]));
                        policyVersion = args[3];
                        pendingSequence = sequence;
                        pendingAction = action;
                        actionQueued = Tick();
                        controllerHeartbeat = Tick();
                        handler->SendSysMessage("WAI_ACTION_QUEUED");
                    }
                    else
                        handler->SendSysMessage("WAI_ACTION_REJECTED");
                    return true;
                }
                if (request.operation == "training" && request.arguments.size() == 3)
                {
                    auto const& args = request.arguments;
                    bool validStage = (args[0] == "started" && trainingStage.empty()) ||
                                      ((args[0] == "completed" || args[0] == "failed") && trainingStage == "started");
                    if (phase == Phase::Finished && args[1] == episode && safeToken(args[2]) && validStage)
                    {
                        trainingStage = args[0];
                        if (trainingStage == "started")
                            LOG_INFO("server.wai.events",
                                     "WAI_LEARNING Training has begun on the completed dungeon rollout.");
                        if (trainingStage == "completed")
                            trainedVersion = args[2];
                        if (trainingStage == "failed")
                            LOG_ERROR("server.wai.events",
                                      "WAI_LEARNING Training failed; automatic advancement stopped.");
                        Emit(("training_" + trainingStage).c_str(),
                             Acore::StringFormat(",\"model_version\":\"{}\"", args[2]));
                        handler->SendSysMessage("WAI_TRAINING_RECORDED");
                    }
                    else
                        handler->SendSysMessage("WAI_TRAINING_REJECTED");
                    return true;
                }
                handler->SendSysMessage("WAI_LEARNING_BAD_COMMAND");
                return true;
            }
            if ((request.operation != "start" && request.operation != "status" && request.operation != "stop" &&
                 request.operation != "repeat" && request.operation != "cleanup" && request.operation != "recover" &&
                 request.operation != "attach" && request.operation != "detach") ||
                !request.arguments.empty())
            {
                handler->SendSysMessage(
                    "waiepisode start | recover | attach | detach | status | stop | repeat | cleanup; "
                    "roles come from config.");
                return true;
            }
            std::lock_guard lock(guard);
            if (requests.size() >= 16)
                handler->SendSysMessage("WAI episode command queue full.");
            else
            {
                requests.push_back(std::move(request));
                handler->SendSysMessage(Acore::StringFormat(
                    "WAI episode PARTY {} request queued; inspect WAI_EPISODE server log receipts.", Slot + 1));
            }
            return true;
        }
    };

    class EpisodeWorld : public WorldScript
    {
      public:
        EpisodeWorld()
            : WorldScript(Acore::StringFormat("WaiEpisodeWorld{}", Slot + 1).c_str(),
                          {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN})
        {
        }
        void OnAfterConfigLoad(bool /*reload*/) override
        {
            std::lock_guard lock(guard);
            enabled = sConfigMgr->GetOption<bool>("Wai.Episodes.Enable", false);
            recoveryEnabled = sConfigMgr->GetOption<bool>("Wai.Episodes.Recovery.Enable", true);
            boundaryEnabled = sConfigMgr->GetOption<bool>("Wai.Episodes.Boundary.Enable", true);
            boundaryTolerance =
                std::clamp(sConfigMgr->GetOption<float>("Wai.Episodes.Boundary.ToleranceYards", 3.0f), 2.0f, 10.0f);
            if (!std::isfinite(boundaryTolerance))
                boundaryTolerance = 3.0f;
            if (phase == Phase::Empty && !learnerProcess)
            {
                auto policy = sConfigMgr->GetOption<std::string>("Wai.Episodes.Policy", "learned");
                if (policy != "learned")
                {
                    enabled = false;
                    LOG_ERROR("server.wai.events",
                              "WAI episodes require learned navigation; scripted routing is disabled. "
                              "Set Wai.Episodes.Policy to learned and configure the learner.");
                }
                learnedNavigation = true;
                learnerExecutable = sConfigMgr->GetOption<std::string>("Wai.Episodes.Learner.Executable", "");
                learnerScript = sConfigMgr->GetOption<std::string>("Wai.Episodes.Learner.Script", "");
                learnerHome = sConfigMgr->GetOption<std::string>("Wai.Episodes.Learner.Home", "wai-learning-rl");
                learnerCredentials = sConfigMgr->GetOption<std::string>("Wai.Episodes.Learner.Credentials", "");
                learnerUrl = sConfigMgr->GetOption<std::string>("Wai.Episodes.Learner.Url", "http://127.0.0.1:7878/");
            }
            recoveryTimeoutSeconds =
                std::clamp(sConfigMgr->GetOption<uint32>("Wai.Episodes.Recovery.TimeoutSeconds", 120), 30u, 600u);
            outputDirectory = sConfigMgr->GetOption<std::string>("Wai.Episodes.OutputDirectory", "wai-episodes");
            stateDirectory = sConfigMgr->GetOption<std::string>("Wai.Episodes.StateDirectory", "wai-episode-state");
            timeoutSeconds =
                std::clamp(sConfigMgr->GetOption<uint32>("Wai.Episodes.TimeoutSeconds", 3600), 60u, 14400u);
            configured = {sConfigMgr->GetOption<std::string>("Wai.Episodes.Account", ""),
                          sConfigMgr->GetOption<std::string>("Wai.Episodes.Tank", ""),
                          sConfigMgr->GetOption<std::string>("Wai.Episodes.Healer", ""),
                          sConfigMgr->GetOption<std::string>("Wai.Episodes.Damage1", ""),
                          sConfigMgr->GetOption<std::string>("Wai.Episodes.Damage2", ""),
                          sConfigMgr->GetOption<std::string>("Wai.Episodes.Damage3", "")};
            if constexpr (Slot > 0)
                configured[0] = sConfigMgr->GetOption<std::string>(
                    Acore::StringFormat("Wai.Episodes.Account{}", Slot + 1),
                    configured[0].empty() ? "" : configured[0] + "_" + std::to_string(Slot + 1), false);
        }
        void OnShutdown() override
        {
            std::lock_guard lock(guard);
            Finish("server_shutdown");
            cycleRequested = false;
            if (learnerProcess)
                learnerProcess->Terminate();
            journal.Close();
            if constexpr (Slot == 0)
            {
                sharedControllerActive = false;
                WaiEpisodes::Integrity::Shutdown();
            }
        }
        void OnUpdate(uint32 /*diff*/) override
        {
            std::lock_guard lock(guard);
            if (Tick() < nextWorldUpdate)
                return;
            nextWorldUpdate = Tick() + 1000;
            try
            {
                if (journalFailed && (phase == Phase::Loading || phase == Phase::Entering || phase == Phase::Running))
                    Finish("recording_write_failed");
                WorldUpdate();
            }
            catch (std::exception const&)
            {
                Emit("error", ",\"reason\":\"episode_operation_exception\"");
                Finish("setup_error");
            }
        }
    };

    class Maps : public AllMapScript
    {
      public:
        Maps() : AllMapScript(Acore::StringFormat("WaiEpisodeMaps{}", Slot + 1).c_str(), {ALLMAPHOOK_ON_MAP_UPDATE}) {}
        void OnMapUpdate(Map* map, uint32 /*diff*/) override
        {
            if (map->GetId() != DungeonMap || !activeInstance || map->GetInstanceId() != activeInstance)
                return;
            std::lock_guard lock(guard);
            Drive(map);
        }
    };

    class Deaths : public UnitScript
    {
      public:
        Deaths()
            : UnitScript(Acore::StringFormat("WaiEpisodeDeaths{}", Slot + 1).c_str(), true, {UNITHOOK_ON_UNIT_DEATH})
        {
        }
        void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
        {
            if (!activeInstance || unit->GetMapId() != DungeonMap || unit->GetInstanceId() != activeInstance)
                return;
            std::lock_guard lock(guard);
            if (phase != Phase::Running)
                return;
            Death death{Wall(), Tick(), 0, ObjectGuid::Empty};
            if (Creature* creature = unit->ToCreature())
            {
                if (std::find(WaiEpisodes::Bosses.begin(), WaiEpisodes::Bosses.end(), creature->GetEntry()) ==
                    WaiEpisodes::Bosses.end())
                    return;
                death.entry = creature->GetEntry();
            }
            else
                return;
            deaths.push_back(death);
        }
    };

    class RecoveryTelemetry : public PlayerScript
    {
      public:
        RecoveryTelemetry()
            : PlayerScript(Acore::StringFormat("WaiEpisodeRecoveryTelemetry{}", Slot + 1).c_str(),
                           {PLAYERHOOK_ON_BEFORE_TELEPORT, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_LOGOUT,
                            PLAYERHOOK_CAN_REPOP_AT_GRAVEYARD, PLAYERHOOK_ON_PLAYER_RESURRECT})
        {
        }
        bool OnPlayerBeforeTeleport(Player* player, uint32 map, float x, float y, float z, float /*orientation*/,
                                    uint32 /*options*/, Unit* /*target*/) override
        {
            QueueTelemetry("teleport_requested", player,
                           Acore::StringFormat(
                               ",\"destination_map\":{},\"destination_x\":{},\"destination_y\":{},\"destination_z\":{}",
                               map, x, y, z));
            return true;
        }
        void OnPlayerMapChanged(Player* player) override
        {
            QueueTelemetry("map_changed", player);
        }
        void OnPlayerLogout(Player* player) override
        {
            QueueTelemetry("logout", player);
        }
        bool OnPlayerCanRepopAtGraveyard(Player* player) override
        {
            QueueTelemetry("graveyard_release", player);
            return true;
        }
        void OnPlayerResurrect(Player* player, float /*restore*/, bool& /*sickness*/) override
        {
            QueueTelemetry("resurrect_requested", player);
        }
    };

    class PlayerDeaths : public PlayerScript
    {
      public:
        PlayerDeaths()
            : PlayerScript(Acore::StringFormat("WaiEpisodePlayerDeaths{}", Slot + 1).c_str(),
                           {PLAYERHOOK_ON_PLAYER_JUST_DIED})
        {
        }
        void OnPlayerJustDied(Player* player) override
        {
            if (!activeInstance || player->GetMapId() != DungeonMap || player->GetInstanceId() != activeInstance)
                return;
            std::lock_guard lock(guard);
            if (phase != Phase::Running || std::none_of(members.begin(), members.end(), [&](Member const& member)
                                                        { return member.guid == player->GetGUID(); }))
                return;
            if (recoveryEnabled)
                WaiEpisodes::Integrity::PermitRecovery(player->GetGUID(), nullptr);
            deaths.push_back(Death{Wall(), Tick(), 0, player->GetGUID()});
        }
    };
};

template <typename Callback> bool VisitParty(unsigned slot, Callback&& callback)
{
    switch (slot)
    {
    case 1:
        return callback(static_cast<PartyRuntime<0>*>(nullptr));
    case 2:
        return callback(static_cast<PartyRuntime<1>*>(nullptr));
    case 3:
        return callback(static_cast<PartyRuntime<2>*>(nullptr));
    case 4:
        return callback(static_cast<PartyRuntime<3>*>(nullptr));
    default:
        return false;
    }
}

class PoolCommands : public CommandScript
{
  public:
    PoolCommands() : CommandScript("WaiEpisodePoolCommands") {}
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commands{{"waiepisode", Handle, SEC_ADMINISTRATOR, Console::Yes}};
        return commands;
    }

    static bool Dispatch(unsigned slot, ChatHandler* handler, std::string const& command)
    {
        return VisitParty(slot,
                          [&](auto* tag)
                          {
                              using Runtime = std::remove_pointer_t<decltype(tag)>;
                              Tail tail;
                              tail = std::string_view(command);
                              return Runtime::Commands::Handle(handler, tail);
                          });
    }

    static bool Handle(ChatHandler* handler, Tail text)
    {
        static std::mutex control;
        std::lock_guard lock(control);
        std::istringstream input{std::string(text)};
        std::string operation;
        input >> operation;
        if (operation == "party")
        {
            unsigned slot = 0;
            input >> slot;
            std::string command;
            std::getline(input >> std::ws, command);
            auto end = command.find(' ');
            std::string subcommand = command.substr(0, end);
            if (slot < 1 || slot > parallelParties ||
                (subcommand != "observe" && subcommand != "act" && subcommand != "training" && subcommand != "repeat" &&
                 subcommand != "status"))
            {
                handler->SendSysMessage(
                    "Use waiepisode party <active party number> observe|act|training|repeat|status.");
                return true;
            }
            return Dispatch(slot, handler, command);
        }
        if (operation == "watch")
        {
            if ((input >> std::ws).peek() == EOF)
            {
                QueueWaiObserverClients(handler);
                return true;
            }
            unsigned slot = 1;
            if (!(input >> slot) || slot < 1 || slot > parallelParties)
            {
                handler->SendSysMessage("Use waiepisode watch <active party number>.");
                return true;
            }
            return VisitParty(
                slot,
                [&](auto* tag)
                {
                    using Runtime = std::remove_pointer_t<decltype(tag)>;
                    std::lock_guard partyLock(Runtime::guard);
                    if (Runtime::phase != Phase::Running)
                    {
                        handler->SendSysMessage(Acore::StringFormat("PARTY {} is {}; no active observation target.",
                                                                    slot, Runtime::PhaseName()));
                        return true;
                    }
                    handler->SendSysMessage(Acore::StringFormat(
                        "PARTY {}: {} | INSTANCE {} | TANK {} | BOSSES {}/4 | DEATHS {}. "
                        "GM CHAT: .gm on THEN .gm visible off THEN .appear {}. "
                        "If binding conflicts: leave RFC, /cleartarget, .instance unbind 389, then appear again.",
                        slot, Runtime::PhaseName(), Runtime::instance, Runtime::members[0].name,
                        Runtime::progress.killed.size(), Runtime::progress.deaths, Runtime::members[0].name));
                    return true;
                });
        }
        if (operation == "start" || operation == "recover")
        {
            if (!WaiProcessAdapter::Available)
            {
                handler->SendSysMessage("Episode start unavailable: safe process cancellation API v1 missing. See mod-wai/compatibility.");
                return true;
            }
            unsigned count = 1;
            std::string countText, extra;
            if (input >> countText)
            {
                std::istringstream number(countText);
                if (!(number >> count) || number.peek() != EOF || (input >> extra))
                    count = 0;
            }
            if (count < 1 || count > MaxParallelParties)
            {
                handler->SendSysMessage("Use waiepisode start <1..4> (party count defaults to one).");
                return true;
            }
            if (count > 1 && sConfigMgr->GetOption<bool>("Wai.Episodes.MovementIntegrity.Enable", false))
            {
                handler->SendSysMessage(
                    "Parallel episodes require movement integrity disabled; its audit is single-party.");
                return true;
            }
            bool empty = true;
            for (unsigned slot = 1; slot <= MaxParallelParties; ++slot)
                VisitParty(slot,
                           [&](auto* tag)
                           {
                               using Runtime = std::remove_pointer_t<decltype(tag)>;
                               std::lock_guard partyLock(Runtime::guard);
                               empty = empty && Runtime::phase == Phase::Empty && !Runtime::accountSetup.Pending();
                               return true;
                           });
            if (!empty)
            {
                handler->SendSysMessage("Stop and clean up existing parties before starting a new batch.");
                return true;
            }
            std::set<std::string> accounts;
            bool distinct = true;
            for (unsigned slot = 1; slot <= count; ++slot)
                VisitParty(slot,
                           [&](auto* tag)
                           {
                               using Runtime = std::remove_pointer_t<decltype(tag)>;
                               std::lock_guard partyLock(Runtime::guard);
                               auto name = Runtime::configured[0];
                               std::transform(name.begin(), name.end(), name.begin(),
                                              [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                               distinct = distinct && Runtime::enabled && !name.empty() &&
                                          name.size() <= MAX_ACCOUNT_STR && accounts.insert(name).second;
                               return true;
                           });
            if (!distinct)
            {
                handler->SendSysMessage(
                    "Each party requires an enabled configuration and a distinct dedicated account name.");
                return true;
            }
            {
                std::lock_guard partyLock(PartyRuntime<0>::guard);
                auto process = PartyRuntime<0>::learnerProcess;
                if (process &&
                    process->GetFutureResult().wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                {
                    handler->SendSysMessage(
                        "The previous controller is still stopping; wait before starting a new batch.");
                    return true;
                }
            }
            parallelParties = count;
            sharedControllerActive = true;
            LOG_INFO("server.wai.events", "WAI_POOL Preparing {} independent parties for one shared model.", count);
            for (unsigned slot = 1; slot <= count; ++slot)
                Dispatch(slot, handler, operation);
            return true;
        }
        if (operation == "stop" || operation == "cleanup" || operation == "status")
        {
            std::string extra;
            if (input >> extra)
            {
                handler->SendSysMessage("Stop, cleanup and status apply to all parties and take no arguments.");
                return true;
            }
            if (operation == "stop")
                sharedControllerActive = false;
            for (unsigned slot = 1; slot <= parallelParties; ++slot)
                Dispatch(slot, handler, operation);
            return true;
        }
        if (parallelParties > 1)
        {
            handler->SendSysMessage("Select a party with waiepisode party <number> <operation>.");
            return true;
        }
        return Dispatch(1, handler, std::string(text));
    }
};

template <unsigned Slot> void RegisterParty()
{
    new typename PartyRuntime<Slot>::EpisodeWorld();
    new typename PartyRuntime<Slot>::Maps();
    new typename PartyRuntime<Slot>::Deaths();
    new typename PartyRuntime<Slot>::PlayerDeaths();
    new typename PartyRuntime<Slot>::RecoveryTelemetry();
}
} // namespace

static ObjectGuid WaiObserverTank(unsigned slot)
{
    ObjectGuid guid;
    VisitParty(slot,
               [&](auto* tag)
               {
                   using Runtime = std::remove_pointer_t<decltype(tag)>;
                   std::lock_guard lock(Runtime::guard);
                   if (Runtime::phase == Phase::Running)
                       guid = Runtime::members[0].guid;
                   return true;
               });
    return guid;
}
#include "ObserverClients.h"
#endif

namespace
{
char const* UnavailableReason = "WAI episode harness unavailable.";
class UnavailableEpisodes : public CommandScript
{
public:
    UnavailableEpisodes() : CommandScript("WaiUnavailableEpisodeCommands") { }
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        static ChatCommandTable commands{{"waiepisode", Handle, SEC_ADMINISTRATOR, Console::Yes}};
        return commands;
    }
    static bool Handle(ChatHandler* handler, char const*) { handler->SendSysMessage(UnavailableReason); return true; }
};
class UnavailableEpisodeWorld : public WorldScript
{
public:
    UnavailableEpisodeWorld() : WorldScript("WaiUnavailableEpisodeWorld") { }
    void OnStartup() override { LOG_ERROR("module.wai-episodes", "{}", UnavailableReason); }
};
void RegisterUnavailableEpisodes(char const* reason)
{
    UnavailableReason = reason;
    new UnavailableEpisodes();
    new UnavailableEpisodeWorld();
}
}

void AddWaiEpisodeScripts()
{
#ifdef MOD_PLAYERBOTS
    if (!WaiEpisodes::Integrity::Install())
    {
        RegisterUnavailableEpisodes("WAI episode harness unavailable: movement validation API v1 missing or registration failed. See mod-wai/compatibility.");
        return;
    }
    RegisterWaiObserverClients();
    new PoolCommands();
    RegisterParty<0>();
    RegisterParty<1>();
    RegisterParty<2>();
    RegisterParty<3>();
#else
    RegisterUnavailableEpisodes("WAI episode harness unavailable: PlayerBots support missing. Other modules remain available.");
#endif
}
