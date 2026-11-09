/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#ifndef PLAYERBOTS_CONTROLMODECOMMAND_H
#define PLAYERBOTS_CONTROLMODECOMMAND_H

#include <string>

namespace ControlModeCommand
{
enum class Request
{
    Unrelated,
    Status,
    Observation,
    Action,
    Invalid
};

inline Request Parse(std::string const& command)
{
    if (command == "mode" || command == "mode ?")
        return Request::Status;
    if (command == "mode observation")
        return Request::Observation;
    if (command == "mode action")
        return Request::Action;
    if (command.compare(0, 5, "mode ") == 0)
        return Request::Invalid;
    return Request::Unrelated;
}
}  // namespace ControlModeCommand
#endif
