/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_BODY_CLEARANCE_H
#define WAI_BODY_CLEARANCE_H
#include "IntegrityMath.h"

namespace WaiEpisodes::Integrity
{
inline bool WalkableFloorContact(Sample const& a, Sample const& b, Sample const& hit, float floor, float climb)
{
    return Finite(a) && Finite(b) && Finite(hit) && std::isfinite(floor) && std::isfinite(climb) && climb >= 0 &&
           std::abs(floor - hit.z) <= 0.05f && floor >= std::min(a.z, b.z) - 0.2f &&
           floor <= std::max(a.z, b.z) + climb;
}

// All callers (live and offline) share this algorithm. Dynamic obstacles and
// upper-body hits are never excused as steps. Static foot hits require a matching
// local collision floor and the navigation tile's native climb allowance.
template <class StaticClear, class DynamicClear, class FloorContact>
bool CheckBodyClearance(Sample const& a, Sample const& b, float radius, float collisionHeight, StaticClear staticClear,
                        DynamicClear dynamicClear, FloorContact floorContact)
{
    if (!Finite(a) || !Finite(b) || !std::isfinite(radius) || !std::isfinite(collisionHeight))
        return false;
    radius = std::clamp(radius, 0.2f, 1.0f);
    float length = std::hypot(b.x - a.x, b.y - a.y);
    float dx = length > 0.001f ? -(b.y - a.y) / length * radius : radius;
    float dy = length > 0.001f ? (b.x - a.x) / length * radius : 0.0f;
    for (float height : {0.25f, std::max(0.5f, collisionHeight - 0.1f)})
        for (float side : {-1.0f, 0.0f, 1.0f})
        {
            Sample start{a.x + side * dx, a.y + side * dy, a.z + height, 0};
            Sample end{b.x + side * dx, b.y + side * dy, b.z + height, 0};
            if (!dynamicClear(start, end))
                return false;
            if (!staticClear(start, end) && (height != 0.25f || !floorContact(start, end)))
                return false;
        }
    return true;
}
} // namespace WaiEpisodes::Integrity
#endif
