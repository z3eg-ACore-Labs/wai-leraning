/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_NAVIGATION_SURFACE_H
#define WAI_NAVIGATION_SURFACE_H
#include "DetourNavMeshQuery.h"
#include <cmath>
#include <limits>

namespace WaiEpisodes::Integrity
{
inline bool FindNavigationSurface(dtNavMeshQuery const* query, dtQueryFilter const& filter, float const* location,
                                  dtPolyRef& poly, float* nearest)
{
    // Native PathGenerator searches five units vertically. Collision-floor checks
    // independently verify standing height; the simplified navmesh is not a floor.
    float extents[3]{0.75f, 5.0f, 0.75f};
    auto status = query->findNearestPoly(location, extents, &filter, &poly, nearest);
    if (dtStatusFailed(status) || !poly)
        return false;
    if (std::hypot(location[0] - nearest[0], location[2] - nearest[2]) > 0.001f)
    {
        // Nearest-by-3D-distance may select a neighbouring sloped polygon and
        // displace XY even when a polygon contains the actual standing position.
        dtPolyRef candidates[64];
        int count = 0;
        status = query->queryPolygons(location, extents, &filter, candidates, &count, 64);
        if (dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL))
            return false;
        float best = std::numeric_limits<float>::max();
        for (int i = 0; i < count; ++i)
        {
            float point[3];
            bool over = false;
            status = query->closestPointOnPoly(candidates[i], location, point, &over);
            float distance = dtStatusSucceed(status) ? std::abs(location[1] - point[1]) : best;
            if (dtStatusSucceed(status) && over && distance <= 5.0f && distance < best)
            {
                best = distance;
                poly = candidates[i];
                for (unsigned axis = 0; axis < 3; ++axis)
                    nearest[axis] = point[axis];
            }
        }
    }
    return std::abs(location[1] - nearest[1]) <= 5.0f &&
           std::hypot(location[0] - nearest[0], location[2] - nearest[2]) <= 0.75f;
}
} // namespace WaiEpisodes::Integrity
#endif
