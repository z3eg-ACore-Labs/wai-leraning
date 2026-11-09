/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_INTEGRITY_MATH_H
#define WAI_INTEGRITY_MATH_H
#include <algorithm>
#include <cmath>
#include <vector>

namespace WaiEpisodes::Integrity
{
inline bool IsNormalNavigation(unsigned flags, unsigned normal)
{
    return flags == normal;
}

struct Sample
{
    float x, y, z;
    unsigned milliseconds;
};

inline bool Finite(Sample const& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

inline float Distance(Sample const& a, Sample const& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

inline bool SameNavigationEndpoint(Sample const& expected, Sample const& reached)
{
    return Finite(expected) && Finite(reached) &&
           std::hypot(expected.x - reached.x, expected.y - reached.y) <= 0.001f &&
           std::abs(expected.z - reached.z) <= 0.05f;
}

inline bool OnNavigationSegment(Sample const& a, Sample const& b, Sample const& point, float& progress)
{
    if (!Finite(a) || !Finite(b) || !Finite(point))
        return false;
    float dx = b.x - a.x, dy = b.y - a.y;
    float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared < 0.000001f)
    {
        progress = 0;
        return std::hypot(point.x - a.x, point.y - a.y) <= 0.001f;
    }
    progress = ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared;
    float clipped = std::clamp(progress, 0.0f, 1.0f);
    return std::hypot(point.x - a.x - clipped * dx, point.y - a.y - clipped * dy) <= 0.001f;
}

inline bool WithinTravelBudget(Sample const& previous, Sample const& actual, unsigned elapsed, float speed)
{
    return Finite(previous) && Finite(actual) && std::isfinite(speed) && speed >= 0.0f &&
           Distance(previous, actual) <= speed * (elapsed / 1000.0f) + 1.0f;
}

inline bool NearCurve(std::vector<Sample> const& points, Sample const& actual, unsigned time)
{
    if (!Finite(actual))
        return false;
    for (auto const& point : points)
        if (point.milliseconds + 500 >= time && point.milliseconds <= time + 200 && Distance(point, actual) <= 1.0f)
            return true;
    return false;
}
} // namespace WaiEpisodes::Integrity
#endif
