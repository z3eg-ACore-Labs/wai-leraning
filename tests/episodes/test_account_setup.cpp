#include "AccountSetup.h"
#include <cassert>

int main() {
  using WaiEpisodes::AccountSetupState;
  WaiEpisodes::AccountSetupWait first, second;
  assert(first.Poll(false, 100) == AccountSetupState::Idle);
  first.Begin(100);
  // The asynchronous write is not visible on the immediate lookup.
  assert(first.Pending());
  assert(first.Poll(false, 100) == AccountSetupState::Waiting);
  assert(first.Poll(false, 30100 - 1) == AccountSetupState::Waiting);
  assert(first.Poll(true, 1100) == AccountSetupState::Ready);
  // Timeout is bounded and does not change another party's pending setup.
  second.Begin(2000);
  assert(first.Poll(false, 30100) == AccountSetupState::TimedOut);
  assert(second.Poll(false, 30100) == AccountSetupState::Waiting);
  // Stop/cleanup cancellation cannot later resume when the write becomes
  // visible.
  first.Clear();
  assert(!first.Pending());
  assert(first.Poll(true, 30200) == AccountSetupState::Idle);
  assert(second.Pending());
  second.Clear();
  second.Begin(40000);
  assert(second.Poll(true, 41000) == AccountSetupState::Ready);
}
