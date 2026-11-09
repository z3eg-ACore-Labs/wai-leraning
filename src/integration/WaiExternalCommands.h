/* Optional WAI command discovery contract; GNU GPL v2 or later. Static linkage. */
#ifndef AC_LABS_WAI_EXTERNAL_COMMANDS_H
#define AC_LABS_WAI_EXTERNAL_COMMANDS_H
#define AC_LABS_WAI_EXTERNAL_COMMAND_API 1
#include <string>
#include <vector>
namespace WaiExternalCommands
{
// Filled during script registration only; consumers must not mutate after startup.
inline std::vector<std::string> names;
}
#endif
