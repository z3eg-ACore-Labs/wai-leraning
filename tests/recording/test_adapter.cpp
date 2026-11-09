#include "WaiPlayerbotsAdapter.h"
#include <cassert>
struct OldSession { bool IsBot() const { return false; } };
struct UnknownSession { };
struct ChangedSession { void IsHeadless() const { } };
int main()
{
    using namespace WaiPlayerbotsAdapter;
    Player player;
    OldSession old;
    UnknownSession unknown;
    ChangedSession changed;
    assert(HumanSession(&player.session) && HumanSession(&old));
    assert(!HumanSession<WorldSession>(nullptr) && !HumanSession(&unknown) && !HumanSession(&changed));
    player.session.headless = true;
    assert(!HumanSession(&player.session));
    assert(HeadlessSession(&player.session));
    assert(!HeadlessSession(&old) && !HeadlessSession(&unknown) && !HeadlessSession(&changed));
    assert(!HeadlessSession<WorldSession>(nullptr));
    player.session.headless = false;
    assert(!HasAI(&player) && GetMode(&player) == Mode::Disabled && !Available());
#if AC_LABS_WAI_CLIENT_CONTROL
    QolClientControl::reader = [](PlayerbotAI* ai)
    {
        return !ai || !ai->managed ? QolClientControl::Mode::Disabled :
            ai->manual ? QolClientControl::Mode::Manual : QolClientControl::Mode::Automatic;
    };
    static bool reject = false;
    QolClientControl::setter = [](PlayerbotAI* ai, QolClientControl::Mode mode, Player* controller)
    {
        if (reject || !ai || controller != ai->player) return false;
        ai->managed = true;
        ai->manual = mode == QolClientControl::Mode::Manual;
        return true;
    };
    assert(Available());
    sPlayerbotAIConfig.SelfBotLevel = 0;
    assert(!AttachManual(&player) && !HasAI(&player));
    sPlayerbotAIConfig.SelfBotLevel = 1;
    assert(!AttachManual(&player) && !HasAI(&player));
    player.gm = true;
    assert(AttachManual(&player) && GetMode(&player) == Mode::Manual);
    auto* ai = PlayerbotsMgr::instance().GetPlayerbotAI(&player);
    assert(AttachManual(&player) && PlayerbotsMgr::instance().GetPlayerbotAI(&player) == ai);
    assert(SetMode(&player, Mode::Automatic) && GetMode(&player) == Mode::Automatic);
    assert(Manual(&player) && GetMode(&player) == Mode::Manual);
    assert(!SetMode(&player, Mode::Disabled) && GetMode(&player) == Mode::Manual);
    Player rejected;
    rejected.gm = true;
    reject = true;
    assert(!AttachManual(&rejected) && !HasAI(&rejected)); // Failed new attachment is removed.
    assert(!AttachManual(&player) && PlayerbotsMgr::instance().GetPlayerbotAI(&player) == ai);
    reject = false;
    QolClientControl::setter = nullptr;
    assert(!Available() && !SetMode(&player, Mode::Automatic));
    delete ai;
#else
    assert(!AttachManual(&player) && !HasAI(&player) && !SetMode(&player, Mode::Automatic));
#endif
    assert(Manual(&player)); // Ordinary human recording needs no attached AI.
    assert(PlayerbotsMgr::instance().clients.empty());
}
