#include "StartProcess.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

// Only console conversion/logging are stubbed. Process creation, pipe draining and cancellation
// are the actual core implementation compiled alongside this offline executable.
bool consoleToUtf8(std::string_view source, std::string& destination)
{
    destination = source;
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 4)
        return 2;
    using namespace std::chrono_literals;
    auto wait = [](std::shared_ptr<Acore::AsyncProcessResult> const& process)
    {
        if (process->GetFutureResult().wait_for(5s) != std::future_status::ready)
        {
            process->Terminate();
            return false;
        }
        return true;
    };
    std::filesystem::path marker(argv[3]);
    std::filesystem::remove(marker);
    auto running = Acore::StartAsyncProcess(argv[1], {argv[2], "hold", marker.string()}, "test", "", true);
    for (unsigned i = 0; i < 500 && !std::filesystem::exists(marker); ++i)
        std::this_thread::sleep_for(10ms);
    bool ready = std::filesystem::exists(marker);
    running->Terminate();
    if (!wait(running) || !ready || running->GetFutureResult().get() == 0)
        return 3;
    std::filesystem::remove(marker);
    for (unsigned i = 0; i < 10; ++i)
    {
        auto cancelled = Acore::StartAsyncProcess(argv[1], {argv[2], "hold", marker.string()}, "test", "", true);
        cancelled->Terminate();
        if (!wait(cancelled) || cancelled->GetFutureResult().get() == 0)
            return 4;
    }
    std::filesystem::remove(marker);
    auto flood = Acore::StartAsyncProcess(argv[1], {argv[2], "flood"}, "test", "", true);
    if (!wait(flood) || flood->GetFutureResult().get() != 0)
        return 5;
    if (Acore::StartProcess(argv[1], {argv[2], "exit"}, "test", "", true) != 7)
        return 6;
    auto missing = (marker.parent_path() / "nonexistent-fixture-executable").string();
    auto failed = Acore::StartAsyncProcess(missing, {}, "test", "", true);
    if (!wait(failed) || failed->GetFutureResult().get() == 0)
        return 7;
    if (Acore::StartProcess(missing, {}, "test", "", true) == 0)
        return 8;
    std::cout << "Process checks passed: live stop, ten startup cancellations, stderr/stdout flood, exit status.\n";
}
