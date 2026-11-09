/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_EPISODE_JOURNAL_H
#define WAI_EPISODE_JOURNAL_H

#include <filesystem>
#include <fstream>
#include <string>

namespace WaiEpisodes
{
// Low-frequency outcome receipts only; called under the harness mutex.
class Journal
{
  public:
    bool Open(std::filesystem::path const& root, std::string const& episode)
    {
        Close();
        if (root.empty() || episode.empty() || episode.find_first_not_of("0123456789-") != std::string::npos)
            return false;
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
            return false;
        auto directory = root / ("RagefireChasm-" + episode);
        // Atomic directory reservation: never append to or overwrite another run.
        if (!std::filesystem::create_directory(directory, error) || error)
            return false;
        stream.clear();
        stream.open(directory / "events.jsonl", std::ios::out | std::ios::binary);
        return stream.is_open() && stream.good();
    }

    bool Write(std::string const& payload)
    {
        // Status/config errors before the first episode remain server-log-only.
        if (!stream.is_open())
            return true;
        stream << payload << '\n';
        stream.flush();
        return stream.good();
    }

    void Close()
    {
        if (stream.is_open())
            stream.close();
    }

  private:
    std::ofstream stream;
};
} // namespace WaiEpisodes
#endif
