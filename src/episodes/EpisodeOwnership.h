/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_EPISODE_OWNERSHIP_H
#define WAI_EPISODE_OWNERSHIP_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace WaiEpisodes
{
inline bool IsCloneName(std::string const& name)
{
    if (name.size() != 11 || name.substr(0, 3) != "Wai")
        return false;
    for (std::size_t i = 3; i < name.size(); ++i)
        if (name[i] < 'a' || name[i] > 'z')
            return false;
    return true;
}

// Append-only, private account-scoped ledger. Keep this alongside the shared server home.
// GUID AND name must match; never infer automatic ownership from a naming prefix.
class Ownership
{
  public:
    bool Load(std::filesystem::path const& file)
    {
        path = file;
        owned.clear();
        std::error_code error;
        bool exists = std::filesystem::exists(file, error);
        if (error)
            return false;
        if (!exists)
            return true;
        std::ifstream input(file);
        if (!input.is_open())
            return false;
        std::string line;
        while (std::getline(input, line))
        {
            std::istringstream record(line);
            std::uint64_t guid = 0;
            std::string name, extra;
            if (!(record >> guid >> name) || (record >> extra) || !guid || guid > UINT32_MAX || !IsCloneName(name))
                return false;
            owned[static_cast<std::uint32_t>(guid)] = name;
        }
        return input.eof();
    }

    bool Owns(std::uint32_t guid, std::string const& name) const
    {
        auto it = owned.find(guid);
        return it != owned.end() && it->second == name;
    }

    bool Remember(std::uint32_t guid, std::string const& name)
    {
        if (!guid || !IsCloneName(name) || path.empty())
            return false;
        if (Owns(guid, name))
            return true;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;
        std::ofstream output(path, std::ios::app | std::ios::binary);
        output << guid << ' ' << name << '\n';
        output.flush();
        if (!output.good())
            return false;
        owned[guid] = name;
        return true;
    }

  private:
    std::filesystem::path path;
    std::map<std::uint32_t, std::string> owned;
};
} // namespace WaiEpisodes
#endif
