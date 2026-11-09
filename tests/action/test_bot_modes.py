"""Compile the actual bot-mode handler with authorization/reply doubles; no gameplay commands."""
from pathlib import Path
import argparse
import os
from compile_fixture import compile_and_run

MOCK = r'''
#include "ControlModeCommand.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
constexpr int PLAYERBOT_SECURITY_ALLOW_ALL = 1, LANG_UNIVERSAL = 0;
struct Player {
    std::string reply;
    void Whisper(std::string const& message, int, Player*) { reply = message; }
};
struct Security {
    bool allowed = true;
    bool CheckLevelFor(int, bool, Player*) { return allowed; }
};
struct PlayerbotAI {
    Player* bot;
    Security security;
    Player* GetBot() { return bot; }
    Security* GetSecurity() { return &security; }
};
struct PlayerbotTextMgr {
    static PlayerbotTextMgr& instance() { static PlayerbotTextMgr text; return text; }
    std::string GetBotTextOrDefault(std::string const&, std::string const& text,
                                   std::map<std::string, std::string> const&) { return text; }
};
bool Enabled = true;
'''
CHECKS = r'''
int main() {
    Player bot, owner; PlayerbotAI ai{&bot};
    assert(!Command(&ai, &owner, 0, "follow") && bot.reply.empty());
    assert(Command(&ai, &owner, 0, "mode") && bot.reply.find("Mode: observation") == 0);
    assert(Command(&ai, &owner, 0, "mode observation") && bot.reply.find("Mode: observation") == 0);
    assert(Command(&ai, &owner, 0, "mode action") && bot.reply.find("Action mode unavailable") == 0);
    assert(Command(&ai, &owner, 0, "mode invalid") && bot.reply.find("Use mode") == 0);
    bot.reply.clear(); ai.security.allowed = false;
    assert(Command(&ai, &owner, 0, "mode") && bot.reply.empty());
    ai.security.allowed = true; Enabled = false;
    assert(Command(&ai, &owner, 0, "mode action") && bot.reply.find("disabled") != std::string::npos);
    bot.reply.clear(); Enabled = true;
    assert(Command(nullptr, &owner, 0, "mode") && Command(&ai, nullptr, 0, "mode") && bot.reply.empty());
    ai.bot = nullptr; assert(Command(&ai, &owner, 0, "mode"));
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--output', type=Path, required=True)
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/action/BotModeCommands.cpp').read_text()
    body = source[source.index('bool Command('):source.index('PlayerbotExtension::Hooks const hooks')]
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output / 'bot-modes.cpp'
    fixture.write_text(MOCK + body + CHECKS)
    compile_and_run(options.compiler, root, fixture)
    print('Actual bot-mode security, observation, rejected action and disabled integration passed')


if __name__ == '__main__':
    main()
