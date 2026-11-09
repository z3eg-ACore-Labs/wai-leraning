/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_OBSERVER_CLIENTS_H
#define WAI_OBSERVER_CLIENTS_H
#include "InstanceSaveMgr.h"
#include "World.h"

namespace WaiObserverClients
{
std::mutex mutex;
bool requested = false;
std::shared_ptr<Acore::AsyncProcessResult> process;
std::string executable, script, client, credentials, loginFile, url;
std::array<std::string, 4> Names{}, Accounts{};

// Called only on the world thread. No account password changes or existing-character resets.
bool EnsureCharacter(unsigned slot)
{
    auto name = Names[slot - 1];
    if (name.empty() || Accounts[slot - 1].empty() || !normalizePlayerName(name))
    {
        LOG_ERROR("server.wai.events", "WAI WATCH: configure private Watch.Account{} and Watch.Character{} first.", slot, slot);
        return false;
    }
    uint32 account = sAccountMgr->GetId(Accounts[slot - 1]);
    Names[slot - 1] = name;
    if (!account || sAccountMgr->GetSecurity(account) < SEC_GAMEMASTER)
    {
        LOG_ERROR("server.wai.events", "WAI WATCH: observer account {} must exist with GM security first.", slot);
        return false;
    }
    auto countStatement = CharacterDatabase.GetPreparedStatement(CHAR_SEL_SUM_CHARS);
    countStatement->SetData(0, account);
    auto countResult = CharacterDatabase.Query(countStatement);
    if (!countResult)
        return false;
    auto characterCount = countResult->Fetch()[0].Get<uint64>();
    if (auto existing = sCharacterCache->GetCharacterCacheByName(name))
    {
        bool valid = characterCount == 1 && existing->AccountId == account && existing->Race == RACE_UNDEAD_PLAYER &&
                     existing->Class == CLASS_MAGE && existing->Level == 80;
        if (!valid)
            LOG_ERROR("server.wai.events", "WAI WATCH: observer {} conflicts with the requested setup; not modified.",
                      slot);
        return valid;
    }
    if (sWorldSessionMgr->FindSession(account) || characterCount != 0)
    {
        LOG_ERROR("server.wai.events", "WAI WATCH: observer {} account must be offline and empty for creation.", slot);
        return false;
    }
    WorldSession session(account, "", 0, nullptr, SEC_GAMEMASTER, EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0),
                         LOCALE_enUS, 0, false, false, 0);
    Player* player = new Player(&session);
    player->GetMotionMaster()->Initialize();
    CharacterCreateInfo info(name, RACE_UNDEAD_PLAYER, CLASS_MAGE, GENDER_MALE, 0, 0, 0, 0, 0);
    if (!player->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &info))
    {
        player->CleanupsBeforeDelete();
        delete player;
        LOG_ERROR("server.wai.events", "WAI WATCH: observer {} creation failed.", slot);
        return false;
    }
    player->setCinematic(2);
    player->SetAtLoginFlag(AT_LOGIN_NONE);
    player->GiveLevel(80);
    player->SetGameMaster(true);
    player->SetGMVisible(false);
    auto transaction = CharacterDatabase.BeginTransaction();
    player->SaveToDB(transaction, true, false);
    CharacterDatabase.DirectCommitTransaction(transaction);
    auto savedStatement = CharacterDatabase.GetPreparedStatement(CHAR_SEL_DATA_BY_NAME);
    savedStatement->SetData(0, std::string(name));
    auto saved = CharacterDatabase.Query(savedStatement);
    if (!saved || saved->Fetch()[1].Get<uint32>() != account || saved->Fetch()[6].Get<uint8>() != 80)
    {
        player->CleanupsBeforeDelete();
        delete player;
        LOG_ERROR("server.wai.events", "WAI WATCH: observer {} persistence verification failed.", slot);
        return false;
    }
    sCharacterCache->AddCharacterCacheEntry(player->GetGUID(), account, player->GetName(), player->getGender(),
                                            player->getRace(), player->getClass(), player->GetLevel());
    player->CleanupsBeforeDelete();
    delete player;
    sWorld->UpdateRealmCharCount(account);
    LOG_INFO("server.wai.events", "WAI WATCH: observer {} created as a level 80 undead mage.", slot);
    return true;
}

class Commands : public CommandScript
{
  public:
    Commands() : CommandScript("WaiObserverClientCommands") {}
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commands{{"waiobserver", Handle, SEC_GAMEMASTER, Console::Yes}};
        return commands;
    }
    static bool Handle(ChatHandler* handler, Acore::ChatCommands::Tail text)
    {
        std::istringstream input{std::string(text)};
        std::string operation, extra;
        unsigned slot = 0;
        input >> operation >> slot;
        if (!input || (input >> extra) || slot < 1 || slot > 4 || (operation != "status" && operation != "join"))
        {
            handler->SendSysMessage("Use waiobserver status|join <1..4>.");
            return true;
        }
        std::lock_guard lock(mutex);
        if (Accounts[slot - 1].empty() || Names[slot - 1].empty())
        {
            handler->SendSysMessage("Observer slot unavailable: configure its private Watch.Account and Watch.Character settings.");
            return true;
        }
        auto account = sAccountMgr->GetId(Accounts[slot - 1]);
        if (handler->GetSession() && handler->GetSession()->GetAccountId() != account)
        {
            handler->SendSysMessage("This observer slot belongs to another account.");
            return true;
        }
        auto player = ObjectAccessor::FindPlayer(sCharacterCache->GetCharacterGuidByName(Names[slot - 1]));
        auto tank = ObjectAccessor::FindPlayer(WaiObserverTank(slot));
        if (operation == "status")
        {
            handler->SendSysMessage(player && player->IsInWorld() && tank && player->GetMap() == tank->GetMap() &&
                                            !player->IsBeingTeleported() && player->IsGameMaster() &&
                                            !player->isGMVisible() && player->InSamePhase(tank)
                                        ? "WAI_OBSERVER_READY"
                                    : player && player->IsInWorld() ? "WAI_OBSERVER_ONLINE"
                                                                    : "WAI_OBSERVER_OFFLINE");
            return true;
        }
        if (!player || !tank || !tank->IsInWorld() || !player->IsInWorld() || player->IsBeingTeleported() ||
            player->GetGroup() || player->IsInCombat() || tank->GetMapId() != 389)
        {
            handler->SendSysMessage(
                "WAI_OBSERVER_WAIT: observer must be online, ungrouped, idle; party must be running.");
            return true;
        }
        player->SetGameMaster(true);
        player->SetGMVisible(false);
        auto difficulty = tank->GetDungeonDifficulty();
        // Only this observer's RFC binding is removed, never a bot's binding or party membership.
        sInstanceSaveMgr->PlayerUnbindInstance(player->GetGUID(), 389, player->GetDungeonDifficulty(), true, player);
        player->SetDungeonDifficulty(difficulty);
        if (auto save = sInstanceSaveMgr->GetInstanceSave(tank->GetInstanceId()))
            sInstanceSaveMgr->PlayerBindToInstance(player->GetGUID(), save, !save->CanReset(), player);
        else
        {
            handler->SendSysMessage("WAI_OBSERVER_WAIT: target instance save unavailable.");
            return true;
        }
        bool switched = player->GetMap() != tank->GetMap();
        bool accepted =
            player->TeleportTo(tank->GetMapId(), tank->GetPositionX(), tank->GetPositionY(),
                               tank->GetPositionZ() + 0.25f, tank->GetOrientation(), TELE_TO_GM_MODE, tank, switched);
        if (accepted)
            player->SetPhaseMask(tank->GetPhaseMask() | 1, false);
        handler->SendSysMessage(accepted ? "WAI_OBSERVER_JOINING" : "WAI_OBSERVER_TELEPORT_FAILED");
        return true;
    }
};

class World : public WorldScript
{
  public:
    World()
        : WorldScript("WaiObserverClientsWorld",
                      {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN})
    {
    }
    void OnAfterConfigLoad(bool) override
    {
        std::lock_guard lock(mutex);
        executable = sConfigMgr->GetOption<std::string>("Wai.Episodes.Watch.Executable", "");
        script = sConfigMgr->GetOption<std::string>("Wai.Episodes.Watch.Script", "Watch-Wai.ps1");
        client = sConfigMgr->GetOption<std::string>("Wai.Episodes.Watch.Client", "");
        loginFile = sConfigMgr->GetOption<std::string>("Wai.Episodes.Watch.LoginFile", "wai-watch-login.xml");
        credentials = sConfigMgr->GetOption<std::string>("Wai.Episodes.Learner.Credentials", "");
        url = sConfigMgr->GetOption<std::string>("Wai.Episodes.Learner.Url", "http://127.0.0.1:7878/");
        for (unsigned slot = 1; slot <= 4; ++slot)
        {
            Accounts[slot - 1] = sConfigMgr->GetOption<std::string>("Wai.Episodes.Watch.Account" + std::to_string(slot), "");
            Names[slot - 1] = sConfigMgr->GetOption<std::string>("Wai.Episodes.Watch.Character" + std::to_string(slot), "");
        }
    }
    void OnShutdown() override
    {
        std::lock_guard lock(mutex);
        requested = false;
        if (process && process->GetFutureResult().wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            process->Terminate();
    }
    void OnUpdate(uint32) override
    {
        std::lock_guard lock(mutex);
        if (!requested)
            return;
        requested = false;
        try
        {
            Launch();
        }
        catch (std::exception const&)
        {
            LOG_ERROR("server.wai.events", "WAI WATCH: setup/launch failed; no training state was changed.");
        }
    }
    void Launch()
    {
        if (!WaiProcessAdapter::Available)
        {
            LOG_ERROR("server.wai.events", "Observer launcher unavailable: safe process cancellation API v1 missing.");
            return;
        }
        if (process && process->GetFutureResult().wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            LOG_INFO("server.wai.events", "WAI WATCH: client launcher already running.");
            return;
        }
        if (executable.empty() || client.empty() || credentials.empty() ||
            !std::filesystem::is_regular_file(executable) || !std::filesystem::is_regular_file(script) ||
            !std::filesystem::is_regular_file(client) || !std::filesystem::is_regular_file(credentials))
        {
            LOG_ERROR("server.wai.events",
                      "WAI WATCH: configure Watch.Executable, Watch.Script, Watch.Client and learner credentials.");
            return;
        }
        unsigned count = parallelParties.load();
        std::string slots;
        for (unsigned slot = 1; slot <= count; ++slot)
        {
            if (!WaiObserverTank(slot) || !EnsureCharacter(slot))
            {
                LOG_INFO("server.wai.events", "WAI WATCH: party/observer {} is not ready; skipping this slot.", slot);
                continue;
            }
            if (!slots.empty())
                slots += ",";
            slots += std::to_string(slot);
        }
        if (slots.empty())
        {
            LOG_INFO("server.wai.events", "WAI WATCH: no running parties with ready observers; try again after start.");
            return;
        }
        std::string accountList;
        for (auto const& account : Accounts)
        {
            if (account.find_first_of(",\r\n") != std::string::npos)
            {
                LOG_ERROR("server.wai.events", "Observer launcher unavailable: private account settings must not contain commas/newlines.");
                return;
            }
            if (&account != &Accounts.front()) accountList += ',';
            accountList += account;
        }
        std::vector<std::string> arguments{"-NoProfile",
                                           "-ExecutionPolicy",
                                           "Bypass",
                                           "-File",
                                           std::filesystem::absolute(script).string(),
                                           "-Client",
                                           client,
                                           "-Slots",
                                           slots,
                                           "-ObserverAccounts",
                                           accountList,
                                           "-Credentials",
                                           credentials,
                                           "-LoginFile",
                                           loginFile,
                                           "-Url",
                                           url};
        process = WaiProcessAdapter::Start(executable, arguments, "server.wai.events", "", true);
        LOG_INFO("server.wai.events",
                 "WAI WATCH: opening observer slots {}; first use asks for the shared observer password.", slots);
    }
};
} // namespace WaiObserverClients

void QueueWaiObserverClients(ChatHandler* handler)
{
    if (handler->GetSession())
    {
        handler->SendSysMessage("Launch observer clients from the local server console.");
        return;
    }
    std::lock_guard lock(WaiObserverClients::mutex);
    WaiObserverClients::requested = true;
    handler->SendSysMessage("WAI WATCH requested; inspect the green event messages.");
}
static void RegisterWaiObserverClients()
{
    new WaiObserverClients::Commands();
    new WaiObserverClients::World();
}
#endif
