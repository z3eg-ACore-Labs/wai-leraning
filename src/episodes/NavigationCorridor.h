/* AzerothCore module; GNU GPL v2 or later. */
#ifndef WAI_NAVIGATION_CORRIDOR_H
#define WAI_NAVIGATION_CORRIDOR_H
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "IntegrityMath.h"

namespace WaiEpisodes::Integrity
{
inline bool ClearNavigationSegment(dtNavMeshQuery const* query, dtQueryFilter const& filter, dtPolyRef startPoly,
                                   dtPolyRef endPoly, float const* start, float const* end)
{
    if (!startPoly || !endPoly)
        return false;
    // Use the same local traversal as native PathGenerator::FindSmoothPath.
    // Do not implement a second raycast/funnel navigation policy: vertex clipping
    // and 3D nearest-poly projection can disagree with the native walking corridor.
    float reached[3]{};
    dtPolyRef visited[64];
    int count = 0;
    auto status = query->moveAlongSurface(startPoly, start, end, &filter, reached, visited, &count, 64);
    if (dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL) || count == 0)
        return false;
    for (int i = 0; i < count; ++i)
    {
        dtMeshTile const* tile = nullptr;
        dtPoly const* poly = nullptr;
        if (dtStatusFailed(query->getAttachedNavMesh()->getTileAndPolyByRef(visited[i], &tile, &poly)) ||
            poly->getType() != DT_POLYTYPE_GROUND)
            return false;
    }
    float surface[3]{};
    if (dtStatusFailed(query->closestPointOnPoly(visited[count - 1], reached, surface, nullptr)) ||
        std::abs(surface[1] - end[1]) > 5.0f)
        return false;
    // Reaching a nearby/slid point is insufficient. The requested XY endpoint
    // must be reached within numerical precision. Collision and physical-floor
    // checks independently validate the actual 3D segment and standing layer.
    return std::hypot(reached[0] - end[0], reached[2] - end[2]) <= 0.001f;
}
} // namespace WaiEpisodes::Integrity
#endif
