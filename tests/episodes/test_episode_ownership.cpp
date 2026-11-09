#include "EpisodeOwnership.h"
#include <cassert>
#include <iostream>

int main()
{
    namespace fs = std::filesystem;
    auto root = fs::current_path() / "ownership-fixture";
    unsigned run = 0;
    while (!fs::create_directory(root.string() + "-" + std::to_string(run)))
        ++run;
    root = root.string() + "-" + std::to_string(run);
    auto path = root / "1.txt";
    WaiEpisodes::Ownership first;
    assert(first.Load(path));
    assert(!first.Owns(42, "Waiabcdefgh"));
    assert(first.Remember(42, "Waiabcdefgh"));
    assert(!first.Remember(0, "Waiabcdefgh"));
    assert(!first.Remember(43, "Unrelated"));
    WaiEpisodes::Ownership restarted;
    assert(restarted.Load(path));
    assert(restarted.Owns(42, "Waiabcdefgh"));
    assert(!restarted.Owns(42, "Waiabcdefgi"));
    assert(!restarted.Owns(43, "Waiabcdefgh"));
    assert(restarted.Remember(43, "Waiabcdefgi"));
    WaiEpisodes::Ownership otherAccount;
    assert(otherAccount.Load(root / "2.txt"));
    assert(!otherAccount.Owns(42, "Waiabcdefgh"));
    std::ofstream corrupt(path, std::ios::app);
    corrupt << "44 truncated\n";
    corrupt.close();
    assert(!restarted.Load(path));
    std::cout << "Ownership restart, account separation, GUID/name and corrupt-ledger checks passed\n";
}
