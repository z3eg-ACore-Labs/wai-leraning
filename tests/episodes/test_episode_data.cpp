// Offline invariants for the episode harness; never contacts databases or a server.
#include "EpisodeData.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace WaiEpisodes;
    Progress progress;
    assert(!progress.Kill(1));
    assert(progress.Kill(11520));
    assert(!progress.Kill(11520));
    assert(!progress.Complete());
    assert(progress.Kill(11517));
    assert(progress.Kill(11518));
    assert(!progress.Complete());
    assert(progress.Kill(11519));
    assert(progress.Complete());

    auto escaped = DumpRow::Parse("INSERT INTO `item_instance` (`guid`, `text`) VALUES ('1', 'A\\'b, c\\\\d');");
    assert(escaped.Get("text") == "A\\'b, c\\\\d");
    assert(DumpRow::Parse(escaped.Sql()).Get("text") == escaped.Get("text"));

    std::string dump =
        "IMPORTANT NOTE: native dump\n"
        "INSERT INTO `characters` (`guid`, `level`, `map`, `xp`, `money`, `at_login`, `extra_flags`, `playerFlags`) "
        "VALUES ('7', '15', '43', '1000', '200', '32', '1', '16');\n"
        "INSERT INTO `character_inventory` (`guid`, `bag`, `slot`, `item`) VALUES ('7', '0', '15', '100');\n"
        "INSERT INTO `character_inventory` (`guid`, `bag`, `slot`, `item`) VALUES ('7', '0', '19', '101');\n"
        "INSERT INTO `character_inventory` (`guid`, `bag`, `slot`, `item`) VALUES ('7', '0', '23', '102');\n"
        "INSERT INTO `character_inventory` (`guid`, `bag`, `slot`, `item`) VALUES ('7', '101', '0', '103');\n"
        "INSERT INTO `character_inventory` (`guid`, `bag`, `slot`, `item`) VALUES ('7', '0', '39', '104');\n"
        "INSERT INTO `item_instance` (`guid`, `itemEntry`) VALUES ('100', '1000');\n"
        "INSERT INTO `item_instance` (`guid`, `itemEntry`) VALUES ('101', '1001');\n"
        "INSERT INTO `item_instance` (`guid`, `itemEntry`) VALUES ('102', '1002');\n"
        "INSERT INTO `item_instance` (`guid`, `itemEntry`) VALUES ('103', '1003');\n"
        "INSERT INTO `item_instance` (`guid`, `itemEntry`) VALUES ('104', '1004');\n"
        "INSERT INTO `item_instance` (`guid`, `itemEntry`) VALUES ('105', '1005');\n"
        "INSERT INTO `character_talent` (`guid`, `spell`, `specMask`) VALUES ('7', '123', '1');\n"
        "INSERT INTO `character_spell` (`guid`, `spell`) VALUES ('7', '456');\n"
        "INSERT INTO `character_skills` (`guid`, `skill`) VALUES ('7', '43');\n"
        "INSERT INTO `character_pet` (`owner`, `id`) VALUES ('7', '77');\n"
        "INSERT INTO `pet_spell` (`guid`, `spell`) VALUES ('77', '888');\n"
        "INSERT INTO `mail` (`receiver`, `id`) VALUES ('7', '2');\n"
        "INSERT INTO `mail_items` (`mail_id`, `item_guid`) VALUES ('2', '105');\n"
        "INSERT INTO `character_aura` (`guid`, `spell`) VALUES ('7', '999');\n"
        "INSERT INTO `character_spell_cooldown` (`guid`, `spell`) VALUES ('7', '444');\n"
        "INSERT INTO `character_account_data` (`guid`, `data`) VALUES ('7', 'private');\n";
    auto sanitized = SanitizeTemplate(dump);
    assert(sanitized.find("'102'") == std::string::npos);
    assert(sanitized.find("'103'") == std::string::npos);
    assert(sanitized.find("'104'") == std::string::npos);
    assert(sanitized.find("'105'") == std::string::npos);
    for (auto table : {"character_talent", "character_spell", "character_skills", "character_pet", "pet_spell"})
        assert(sanitized.find(std::string("`") + table + "`") != std::string::npos);
    for (auto table : {"mail", "mail_items", "character_aura", "character_spell_cooldown", "character_account_data"})
        assert(sanitized.find(std::string("`") + table + "`") == std::string::npos);
    std::istringstream lines(sanitized);
    std::string line;
    std::getline(lines, line);
    auto character = DumpRow::Parse(line);
    assert(character.Get("level") == "15");
    assert(character.Get("map") == "1");
    assert(character.Get("xp") == "0");
    assert(character.Get("money") == "0");
    assert(character.Get("at_login") == "0");
    assert(character.Get("extra_flags") == "0");
    assert(character.Get("playerFlags") == "33554432");
    std::istringstream allianceLines(SanitizeTemplate(dump, true));
    std::getline(allianceLines, line);
    assert(DumpRow::Parse(line).Get("map") == "0");
    bool rejected = false;
    try { SanitizeTemplate("bad dump"); } catch (std::exception const&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { SanitizeTemplate(""); } catch (std::exception const&) { rejected = true; }
    assert(rejected);
    std::cout << "Episode data invariants passed\n";
}
