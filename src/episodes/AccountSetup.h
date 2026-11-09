/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_EPISODE_ACCOUNT_SETUP_H
#define WAI_EPISODE_ACCOUNT_SETUP_H
#include <cstdint>

namespace WaiEpisodes
{
enum class AccountSetupState
{
    Idle,
    Waiting,
    Ready,
    TimedOut
};

class AccountSetupWait
{
  public:
    void Begin(std::uint64_t now)
    {
        deadline = now + 30000;
    }
    void Clear()
    {
        deadline = 0;
    }
    bool Pending() const
    {
        return deadline != 0;
    }
    AccountSetupState Poll(bool visible, std::uint64_t now) const
    {
        if (!Pending())
            return AccountSetupState::Idle;
        if (visible)
            return AccountSetupState::Ready;
        return now >= deadline ? AccountSetupState::TimedOut : AccountSetupState::Waiting;
    }

  private:
    std::uint64_t deadline = 0;
};
} // namespace WaiEpisodes
#endif
