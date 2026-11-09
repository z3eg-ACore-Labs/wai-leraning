/* Bot observation-mode replies extracted from mod-playerbots; GNU GPL v2 or later. */
#include "ControlModeCommand.h"
#include "Config.h"
#include "Log.h"
#include "WaiExternalCommands.h"
#include "WorldScript.h"
#if __has_include("PlayerbotExtension.h")
#include "PlayerbotExtension.h"
#endif
#if defined(PLAYERBOT_EXTENSION_API_VERSION) && PLAYERBOT_EXTENSION_API_VERSION == 1
#include "Playerbots.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
namespace
{
bool Enabled = true;
bool Command(PlayerbotAI* ai, Player* owner, std::uint32_t, std::string const& text)
{
    auto const request = ControlModeCommand::Parse(text);
    if (request == ControlModeCommand::Request::Unrelated) return false;
    if (!ai || !owner || !ai->GetBot() ||
        !ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, true, owner)) return true;
    if (!Enabled)
    {
        ai->GetBot()->Whisper("WAI bot mode commands are disabled in wai.conf.", LANG_UNIVERSAL, owner);
        return true;
    }
    std::string key = "control_mode_observation";
    std::string message = "Mode: observation. Native PlayerBots AI controls this bot. No trainer, recording, "
                          "or feedback collection is connected.";
    if (request == ControlModeCommand::Request::Action)
    {
        key = "control_mode_action_unavailable";
        message = "Action mode unavailable: no external controller or safe handoff is implemented. "
                  "Request rejected; mode remains observation and native PlayerBots AI stays in control.";
    }
    else if (request == ControlModeCommand::Request::Invalid)
    {
        key = "control_mode_usage";
        message = "Use mode, mode observation, or mode action. Action mode is currently unavailable.";
    }
    ai->GetBot()->Whisper(PlayerbotTextMgr::instance().GetBotTextOrDefault(key, message, {}), LANG_UNIVERSAL, owner);
    return true;
}
PlayerbotExtension::Hooks const hooks{.command = Command};
}
void RegisterWaiBotModes()
{
    Enabled = sConfigMgr->GetOption<bool>("Wai.Action.BotModeCommands", true);
    PlayerbotExtension::Register(&hooks);
    WaiExternalCommands::names.push_back("mode");
}
#else
namespace
{
class UnavailableBotModes : public WorldScript
{
public:
    UnavailableBotModes() : WorldScript("WaiUnavailableBotModes") { }
    void OnStartup() override
    {
        LOG_WARN("module.wai-action", "Bot mode replies unavailable: PlayerBots extension API v1 missing. "
                 "Character .wai commands and telemetry remain separate. See mod-wai documentation.");
    }
};
}
void RegisterWaiBotModes() { new UnavailableBotModes(); }
#endif
