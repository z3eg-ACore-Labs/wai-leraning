// Offline regression checks; no game server, map or database is contacted.
#include "IntegrityMath.h"
#include <cassert>
#include <limits>
#include <iostream>

int main()
{
    using namespace WaiEpisodes::Integrity;
    assert(IsNormalNavigation(1, 1));
    assert(!IsNormalNavigation(1 | 16, 1)); // NORMAL combined with NOT_USING_PATH is not safe.
    for (unsigned bad : {0u, 2u, 4u, 8u, 16u, 32u, 64u, 128u})
        assert(!IsNormalNavigation(bad, 1));
    Sample origin{0, 0, 0, 0};
    assert(SameNavigationEndpoint(origin, origin));
    assert(SameNavigationEndpoint(origin, {0.0001f, 0, 0.01f, 0}));
    assert(!SameNavigationEndpoint(origin, {0.1f, 0, 0, 0}));
    assert(!SameNavigationEndpoint(origin, {0, 0, 1, 0})); // Another floor is not a shared edge.
    assert(!SameNavigationEndpoint(origin, {std::numeric_limits<float>::quiet_NaN(), 0, 0, 0}));
    float segmentProgress = 0;
    assert(OnNavigationSegment(origin, {2, 0, 0, 0}, {1, 0, 0, 0}, segmentProgress));
    assert(segmentProgress == 0.5f);
    assert(!OnNavigationSegment(origin, {2, 0, 0, 0}, {1, 0.1f, 0, 0}, segmentProgress));
    assert(!OnNavigationSegment(origin, {2, 0, 0, 0}, {3, 0, 0, 0}, segmentProgress));
    assert(WithinTravelBudget(origin, {1.4f, 0, 0, 200}, 200, 7));
    assert(!WithinTravelBudget(origin, {20, 0, 0, 200}, 200, 7));
    assert(!WithinTravelBudget(origin, {0, 0, -20, 200}, 200, 7));
    assert(!WithinTravelBudget(origin, {std::numeric_limits<float>::quiet_NaN(), 0, 0, 0}, 200, 7));
    assert(!WithinTravelBudget(origin, origin, 200, -1));
    std::vector<Sample> curve{{0, 0, 0, 0}, {1, 0, 0, 200}, {2, 0, 1, 400}, {3, 0, 0, 600}};
    assert(NearCurve(curve, {2, 0, 1, 0}, 400));
    assert(!NearCurve(curve, {2, 0, -3, 0}, 400));
    assert(!NearCurve(curve, {0, 0, 0, 0}, 2000));
    assert(!NearCurve({}, origin, 0));
    assert(Distance(origin, {0, 0, 3, 0}) == 3);
    std::cout << "Integrity rule regressions passed\n";
}
