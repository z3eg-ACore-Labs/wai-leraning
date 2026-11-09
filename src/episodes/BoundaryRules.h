/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_EPISODE_BOUNDARY_RULES_H
#define WAI_EPISODE_BOUNDARY_RULES_H
#include <cmath>
#include <cstdint>

namespace WaiEpisodes
{
enum class BoundaryState
{
    Inside,
    Outside,
    Unknown,
    Unavailable
};
struct BoundaryReading
{
    BoundaryState state = BoundaryState::Unknown;
    float distance = 0;
};
class BoundaryWatch
{
  public:
    bool Update(BoundaryReading reading, float tolerance, std::uint64_t now)
    {
        if (reading.state != BoundaryState::Outside || !std::isfinite(reading.distance) ||
            reading.distance <= tolerance)
        {
            Clear();
            return false;
        }
        if (!suspect)
        {
            suspect = true;
            since = now;
        }
        return now >= since && now - since >= 3000;
    }
    void Clear()
    {
        suspect = false;
        since = 0;
    }

  private:
    bool suspect = false;
    std::uint64_t since = 0;
};
} // namespace WaiEpisodes
#endif
