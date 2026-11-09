#include "BoundaryRules.h"
#include <cassert>
#include <limits>

int main() {
  using namespace WaiEpisodes;
  BoundaryWatch first, second;
  assert(!first.Update({BoundaryState::Outside, 2.9f}, 3, 100));
  assert(!first.Update({BoundaryState::Outside, 3.0f}, 3, 4000));
  assert(!first.Update({BoundaryState::Outside, 3.1f}, 3, 5000));
  assert(!first.Update({BoundaryState::Outside, 5}, 3, 7999));
  assert(first.Update({BoundaryState::Outside, 5}, 3, 8000));
  assert(!second.Update({BoundaryState::Outside, 5}, 3, 8000));
  // A brief jump/contact error and uncertain data cannot accumulate an escape
  // verdict.
  assert(!first.Update({BoundaryState::Inside, 0}, 3, 9000));
  assert(!first.Update({BoundaryState::Outside, 5}, 3, 10000));
  assert(!first.Update({BoundaryState::Unknown, 0}, 3, 12000));
  assert(!first.Update({BoundaryState::Outside, 5}, 3, 13000));
  assert(!first.Update({BoundaryState::Unavailable, 0}, 3, 16000));
  assert(!first.Update(
      {BoundaryState::Outside, std::numeric_limits<float>::quiet_NaN()}, 3,
      17000));
  assert(!first.Update({BoundaryState::Outside, 5}, 3, 20000));
  first.Clear();
  assert(!first.Update({BoundaryState::Outside, 5}, 3, 25000));
}
