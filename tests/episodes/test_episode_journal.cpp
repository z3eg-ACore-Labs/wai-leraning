#include "EpisodeJournal.h"
#include <cassert>
#include <iostream>

int main()
{
    namespace fs = std::filesystem;
    auto root = fs::current_path() / "journal-fixture";
    // Each test invocation reserves a fresh temporary directory without deletion.
    unsigned run = 0;
    while (!fs::create_directory(root.string() + "-" + std::to_string(run)))
        ++run;
    root = root.string() + "-" + std::to_string(run);
    WaiEpisodes::Journal journal;
    assert(journal.Write("ignored-before-episode"));
    assert(!journal.Open(root, "../escape"));
    assert(journal.Open(root, "123-1"));
    assert(journal.Write("{\"event\":\"start\"}"));
    std::ifstream live(root / "RagefireChasm-123-1/events.jsonl");
    std::string line;
    assert(std::getline(live, line) && line == "{\"event\":\"start\"}");
    assert(journal.Write("{\"event\":\"end\"}"));
    journal.Close();
    assert(!journal.Open(root, "123-1"));
    assert(journal.Open(root, "124-2"));
    assert(journal.Write("{\"event\":\"start\"}"));
    journal.Close();
    std::ofstream blocked(root / "file-not-directory");
    blocked.close();
    assert(!journal.Open(root / "file-not-directory", "125-3"));
    std::cout << "Episode JSONL creation, flush, collision and failure checks passed\n";
}
