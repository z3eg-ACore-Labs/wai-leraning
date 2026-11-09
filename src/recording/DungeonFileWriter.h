/* AzerothCore module; GNU GPL v2 or later. */
#pragma once
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace GameplayObservation
{
enum class CommandKind
{
    Open,
    Line,
    Close
};

struct WriterCommand
{
    CommandKind kind;
    std::string text;
    unsigned long long dropped = 0;
};

inline std::string DungeonSlug(std::string const& name)
{
    std::string result;
    for (unsigned char c : name)
    {
        if (result.size() >= 80)
            break;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            result += char(c);
        else if (c >= 'A' && c <= 'Z')
            result += char(c - 'A' + 'a');
        else if (!result.empty() && result.back() != '-')
            result += '-';
    }
    while (!result.empty() && result.back() == '-')
        result.pop_back();
    return result.empty() ? "dungeon" : result;
}

class DungeonFileWriter
{
  public:
    explicit DungeonFileWriter(std::filesystem::path directory) : _directory(std::move(directory)) {}

    void Apply(WriterCommand const& command)
    {
        if (command.kind == CommandKind::Open)
            Open(command.text);
        else if (command.kind == CommandKind::Close)
            Close(command.dropped);
        else
        {
            if (!_file.is_open())
                throw std::runtime_error("Observation line outside a dungeon file");
            _file << command.text << '\n';
            if (!_file)
                throw std::runtime_error("Observation write failed");
            Flush(false);
        }
    }

    void Flush(bool force)
    {
        auto now = std::chrono::steady_clock::now();
        if (_file.is_open() && (force || now >= _nextFlush))
        {
            _file.flush();
            if (!_file)
                throw std::runtime_error("Observation flush failed");
            _nextFlush = now + std::chrono::seconds(1);
        }
    }

    bool IsOpen() const
    {
        return _file.is_open();
    }

  private:
    void Open(std::string const& dungeon)
    {
        if (_file.is_open())
            throw std::runtime_error("Previous dungeon file was not closed");
        std::filesystem::create_directories(_directory);
        auto now = std::chrono::system_clock::now();
        std::time_t stamp = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &stamp);
#else
        gmtime_r(&stamp, &utc);
#endif
        std::ostringstream prefix;
        prefix << DungeonSlug(dungeon) << '-' << std::put_time(&utc, "%Y%m%dT%H%M%SZ") << '-'
               << now.time_since_epoch().count();
        // Atomic directory allocation avoids overwriting another visit/server's file.
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            std::string name = prefix.str() + '-' + std::to_string(attempt);
            auto run = _directory / name;
            if (!std::filesystem::create_directory(run))
                continue;
            _file.open(run / (name + ".jsonl"));
            if (!_file)
                throw std::runtime_error("Cannot open dungeon observation file");
            _nextFlush = std::chrono::steady_clock::time_point{};
            return;
        }
        throw std::runtime_error("Cannot allocate dungeon observation directory");
    }

    void Close(unsigned long long dropped)
    {
        if (!_file.is_open())
            throw std::runtime_error("Dungeon close without an open file");
        _file << "{\"schema\":1,\"kind\":\"writer_summary\",\"dropped\":" << dropped << "}\n";
        Flush(true);
        _file.close();
        if (!_file)
            throw std::runtime_error("Observation close failed");
    }

    std::filesystem::path _directory;
    std::ofstream _file;
    std::chrono::steady_clock::time_point _nextFlush{};
};
} // namespace GameplayObservation
