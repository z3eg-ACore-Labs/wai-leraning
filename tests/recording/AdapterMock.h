#pragma once
#include <unordered_map>
class WorldSession
{
public:
    bool headless = false;
    bool IsHeadless() const { return headless; }
};
class Player
{
public:
    WorldSession session;
    bool gm = false;
    WorldSession* GetSession() { return &session; }
    bool CanBeGameMaster() const { return gm; }
};
class PlayerbotAI
{
public:
    Player* player;
    bool managed = false, manual = false;
    explicit PlayerbotAI(Player* value) : player(value) { }
    ~PlayerbotAI();
};
class PlayerbotsMgr
{
public:
    std::unordered_map<Player*, PlayerbotAI*> clients;
    static PlayerbotsMgr& instance() { static PlayerbotsMgr manager; return manager; }
    PlayerbotAI* GetPlayerbotAI(Player* player)
    {
        auto it = clients.find(player);
        return it == clients.end() ? nullptr : it->second;
    }
    void AddPlayerbotData(Player* player, bool) { clients[player] = new PlayerbotAI(player); }
};
inline PlayerbotAI::~PlayerbotAI() { PlayerbotsMgr::instance().clients.erase(player); }
inline struct { int SelfBotLevel = 2; } sPlayerbotAIConfig;
