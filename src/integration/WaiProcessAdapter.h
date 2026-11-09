/* Safe native helper lifecycle boundary; GNU GPL v2 or later. */
#ifndef AC_LABS_WAI_PROCESS_ADAPTER_H
#define AC_LABS_WAI_PROCESS_ADAPTER_H
#include "StartProcess.h"
namespace WaiProcessAdapter
{
#if defined(AC_ASYNC_PROCESS_CANCELLATION_API) && AC_ASYNC_PROCESS_CANCELLATION_API == 1
inline constexpr bool Available = true;
#else
inline constexpr bool Available = false;
#endif
inline std::shared_ptr<Acore::AsyncProcessResult> Start(std::string const& executable,
    std::vector<std::string> const& arguments, std::string const& logger, std::string const& input, bool secure)
{
#if defined(AC_ASYNC_PROCESS_CANCELLATION_API) && AC_ASYNC_PROCESS_CANCELLATION_API == 1
    return Acore::StartAsyncProcess(executable, arguments, logger, input, secure);
#else
    return {};
#endif
}
}
#endif
