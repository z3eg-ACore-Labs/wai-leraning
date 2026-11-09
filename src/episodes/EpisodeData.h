/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_EPISODE_DATA_H
#define WAI_EPISODE_DATA_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace WaiEpisodes
{
constexpr std::array<std::uint32_t, 4> Bosses{11517, 11520, 11518, 11519};
constexpr std::array<char const*, 4> BossNames{"Oggleflint", "Taragaman the Hungerer", "Jergosh the Invoker",
                                               "Bazzalan"};

struct Progress
{
    std::set<std::uint32_t> killed;
    std::uint32_t deaths = 0;

    bool Kill(std::uint32_t entry)
    {
        return std::find(Bosses.begin(), Bosses.end(), entry) != Bosses.end() && killed.insert(entry).second;
    }
    bool Complete() const
    {
        return killed.size() == Bosses.size();
    }
};

// Only parse dumps produced by this core; preserve SQL quoting for the native importer's GUID remapping.
struct DumpRow
{
    std::string table;
    std::vector<std::string> columns;
    std::vector<std::string> values;

    std::string Get(std::string const& column) const
    {
        auto it = std::find(columns.begin(), columns.end(), column);
        if (it == columns.end())
            throw std::runtime_error("Required dump column missing: " + column);
        auto value = values.at(std::distance(columns.begin(), it));
        if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
            value = value.substr(1, value.size() - 2);
        return value;
    }

    void Set(std::string const& column, std::string const& value)
    {
        auto it = std::find(columns.begin(), columns.end(), column);
        if (it != columns.end())
            values.at(std::distance(columns.begin(), it)) = "'" + value + "'";
    }

    std::string Sql() const
    {
        std::string sql = "INSERT INTO `" + table + "` (";
        for (std::size_t i = 0; i < columns.size(); ++i)
            sql += (i ? ", " : "") + std::string("`") + columns[i] + "`";
        sql += ") VALUES (";
        for (std::size_t i = 0; i < values.size(); ++i)
            sql += (i ? ", " : "") + values[i];
        return sql + ");\n";
    }

    static DumpRow Parse(std::string const& line)
    {
        DumpRow row;
        std::string const prefix = "INSERT INTO `";
        if (line.compare(0, prefix.size(), prefix) != 0)
            throw std::runtime_error("Unexpected native dump line");
        auto end = line.find('`', prefix.size());
        auto split = line.find(") VALUES (", end);
        if (end == std::string::npos || split == std::string::npos)
            throw std::runtime_error("Malformed native dump row");
        row.table = line.substr(prefix.size(), end - prefix.size());
        auto pos = end + 1;
        while ((pos = line.find('`', pos)) < split)
        {
            auto close = line.find('`', pos + 1);
            if (close == std::string::npos || close >= split)
                throw std::runtime_error("Malformed dump columns");
            row.columns.push_back(line.substr(pos + 1, close - pos - 1));
            pos = close + 1;
        }
        pos = split + 10;
        while (pos < line.size())
        {
            while (pos < line.size() && line[pos] == ' ')
                ++pos;
            auto begin = pos;
            if (line[pos] == '\'')
            {
                ++pos;
                bool closed = false;
                while (pos < line.size())
                {
                    if (line[pos] == '\\')
                        pos += 2;
                    else if (line[pos++] == '\'')
                    {
                        closed = true;
                        break;
                    }
                }
                if (!closed)
                    throw std::runtime_error("Unterminated dump string");
            }
            else
                while (pos < line.size() && line[pos] != ',' && line[pos] != ')')
                    ++pos;
            row.values.push_back(line.substr(begin, pos - begin));
            while (pos < line.size() && line[pos] == ' ')
                ++pos;
            if (pos < line.size() && line[pos] == ')')
                break;
            if (pos >= line.size() || line[pos++] != ',')
                throw std::runtime_error("Malformed dump values");
        }
        if (row.columns.empty() || row.columns.size() != row.values.size())
            throw std::runtime_error("Native dump column count mismatch");
        return row;
    }
};

inline std::string SanitizeTemplate(std::string const& dump, bool alliance = false)
{
    std::vector<DumpRow> rows;
    std::istringstream input(dump);
    std::string line;
    std::set<std::string> items;
    while (std::getline(input, line))
    {
        if (line.empty() || line.compare(0, 15, "IMPORTANT NOTE:") == 0)
            continue;
        auto row = DumpRow::Parse(line);
        if (row.table == "character_inventory" && row.Get("bag") == "0" && std::stoul(row.Get("slot")) < 23)
            items.insert(row.Get("item"));
        rows.push_back(std::move(row));
    }
    std::set<std::string> const allowed{"characters",           "character_action",    "character_glyphs",
                                        "character_homebind",   "character_inventory", "character_pet",
                                        "character_reputation", "character_skills",    "character_spell",
                                        "character_talent",     "pet_spell",           "item_instance"};
    std::string result;
    unsigned characters = 0;
    for (auto& row : rows)
    {
        if (!allowed.count(row.table))
            continue;
        if (row.table == "character_inventory" && (row.Get("bag") != "0" || std::stoul(row.Get("slot")) >= 23))
            continue;
        if (row.table == "item_instance" && !items.count(row.Get("guid")))
            continue;
        if (row.table == "characters")
        {
            ++characters;
            row.Set("map", alliance ? "0" : "1");
            row.Set("instance_id", "0");
            row.Set("instance_mode_mask", "0");
            row.Set("position_x", alliance ? "-8949.95" : "1813.49");
            row.Set("position_y", alliance ? "-132.493" : "-4418.58");
            row.Set("position_z", alliance ? "83.5312" : "-18.57");
            row.Set("orientation", "1.78");
            row.Set("at_login", "0");
            row.Set("online", "0");
            row.Set("money", "0");
            row.Set("xp", "0");
            row.Set("rest_bonus", "0");
            row.Set("taxi_path", "");
            row.Set("transguid", "0");
            row.Set("zone", "1637");
            row.Set("death_expire_time", "0");
            row.Set("extra_flags", "0");
            // Reset transient player flags and lock XP before the clone's first login.
            row.Set("playerFlags", "33554432");
        }
        result += row.Sql();
    }
    if (characters != 1)
        throw std::runtime_error("Template requires exactly one character record");
    return result;
}
} // namespace WaiEpisodes
#endif
