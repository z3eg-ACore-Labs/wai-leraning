/* AzerothCore module; GNU GPL v2 or later. */
#include "EpisodeMovement.h"
#include "WaiSplineAdapter.h"
#if WAI_SPLINE_VALIDATION_AVAILABLE
#include "AllMapScript.h"
#include "BodyClearance.h"
#include "Config.h"
#include "Creature.h"
#include "DetourNavMeshQuery.h"
#include "IntegrityMath.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MoveSplineInitArgs.h"
#include "NavigationCorridor.h"
#include "NavigationSurface.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerScript.h"
#include "StringFormat.h"
#include "UnitScript.h"
#include "VMapFactory.h"
#include "VMapMgr2.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>

namespace WaiEpisodes::Integrity
{
namespace
{
constexpr uint32 Dungeon = 389;
constexpr uint32 ExitTrigger = 2226;
constexpr unsigned MaxSamples = 4096;
constexpr unsigned MaxActors = 32;
std::mutex integrityMutex;
std::atomic<bool> scoped{false};
std::array<ObjectGuid, 5> members;
std::map<ObjectGuid, uint64> entryPermits;
std::set<ObjectGuid> readyOwners;
struct RecoveryPermit
{
    uint64 expires;
    bool release;
    Sample destination;
    float radius;
};
std::map<ObjectGuid, RecoveryPermit> recoveryPermits;
std::string violation;
bool failed = false;

struct Audit
{
    Sample previous{};
    uint64 previousTime = 0;
    uint32 spline = 0;
    float peakSpeed = 0.0f;
    bool airborne = false;
    std::vector<Sample> curve;
};
std::map<ObjectGuid, Audit> audits;

uint64 Now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

Sample PositionOf(Unit* unit, unsigned time = 0)
{
    return {unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), time};
}

bool Participant(Unit* unit)
{
    ObjectGuid actor = unit->IsPlayer() ? unit->GetGUID() : unit->GetCharmerOrOwnerGUID();
    return !actor.IsEmpty() && std::find(members.begin(), members.end(), actor) != members.end();
}

bool EntryReady(Unit* unit)
{
    return readyOwners.count(unit->IsPlayer() ? unit->GetGUID() : unit->GetCharmerOrOwnerGUID()) != 0;
}

bool Reject(Unit* unit, char const* reason)
{
    if (!failed)
    {
        ObjectGuid owner = unit->IsPlayer() ? unit->GetGUID() : unit->GetCharmerOrOwnerGUID();
        auto slot = std::distance(members.begin(), std::find(members.begin(), members.end(), owner)) + 1;
        violation = std::string(reason) + ":member_slot=" + std::to_string(slot) +
                    (unit->IsPlayer() ? ":player" : ":owned_unit");
    }
    failed = true;
    unit->StopMoving();
    return false;
}

bool DataAvailable(Unit* unit)
{
    auto& data = unit->GetMap()->GetMapCollisionData();
    auto const tree = data.GetStaticTreeSharedPtr();
    auto manager = VMAP::VMapFactory::createOrGetVMapMgr();
    return manager->isHeightCalcEnabled() && manager->isLineOfSightCalcEnabled() && tree && tree->numLoadedTiles() &&
           data.GetMMapData().GetNavMesh() && data.GetMMapData().GetNavMeshQuery();
}

dtQueryFilter Filter()
{
    dtQueryFilter filter;
    filter.setIncludeFlags(NAV_GROUND | NAV_WATER | NAV_MAGMA | NAV_SLIME | NAV_GROUND_STEEP);
    return filter;
}

bool Water(Unit* unit, Sample const& point, LiquidData& liquid)
{
    if (!unit->CanSwim() || !Finite(point))
        return false;
    liquid =
        unit->GetMap()->GetLiquidData(unit->GetPhaseMask(), point.x, point.y, point.z, unit->GetCollisionHeight(), {});
    return (liquid.Status & MAP_LIQUID_STATUS_SWIMMING) && std::isfinite(liquid.Level) &&
           std::isfinite(liquid.DepthLevel) && point.z >= liquid.DepthLevel - 0.2f && point.z <= liquid.Level + 0.5f;
}

bool NavPoint(Unit* unit, Sample const& point, dtPolyRef& poly, float* nearest)
{
    if (!Finite(point))
        return false;
    float location[3]{point.y, point.z - unit->GetHoverHeight(), point.x};
    LiquidData liquid;
    bool swimming = Water(unit, point, liquid);
    if (swimming)
        location[1] = liquid.Level;
    auto filter = Filter();
    auto query = unit->GetMap()->GetMapCollisionData().GetMMapData().GetNavMeshQuery();
    bool found = FindNavigationSurface(query, filter, location, poly, nearest);
    if (swimming && poly)
    {
        unsigned short flags = 0;
        unit->GetMap()->GetMapCollisionData().GetMMapData().GetNavMesh()->getPolyFlags(poly, &flags);
        if (!(flags & (NAV_WATER | NAV_MAGMA | NAV_SLIME)))
            return false;
    }
    return found;
}

bool Ground(Unit* unit, Sample const& point)
{
    dtPolyRef poly = 0;
    float nearest[3];
    if (!NavPoint(unit, point, poly, nearest))
        return false;
    LiquidData liquid;
    if (Water(unit, point, liquid))
        return true;
    // A local downward query selects this floor, not the highest floor in a multi-level dungeon.
    float feet = point.z - unit->GetHoverHeight();
    float floor = unit->GetMap()->GetMapCollisionData().GetStaticTree().getHeight(point.x, point.y, feet + 0.6f, 2.0f);
    return std::isfinite(floor) && floor > INVALID_HEIGHT && feet >= floor - 0.2f && feet <= floor + 1.25f;
}

std::string SurfaceDetails(Unit* unit)
{
    auto point = PositionOf(unit);
    dtPolyRef poly = 0;
    float nearest[3]{};
    bool nav = NavPoint(unit, point, poly, nearest);
    float feet = point.z - unit->GetHoverHeight();
    float floor = unit->GetMap()->GetMapCollisionData().GetStaticTree().getHeight(point.x, point.y, feet + 0.6f, 2.0f);
    return Acore::StringFormat(":x={}:y={}:z={}:hover={}:nav_ok={}:nav_z={}:floor={}", point.x, point.y, point.z,
                               unit->GetHoverHeight(), nav ? 1 : 0, nearest[1], floor);
}

bool BodyClear(Unit* unit, Sample const& a, Sample const& b)
{
    auto const& data = unit->GetMap()->GetMapCollisionData();
    auto staticClear = [&](Sample const& start, Sample const& end)
    {
        return data.GetStaticTree().isInLineOfSight(start.x, start.y, start.z, end.x, end.y, end.z,
                                                    VMAP::ModelIgnoreFlags::Nothing);
    };
    auto dynamicClear = [&](Sample const& start, Sample const& end)
    {
        return data.GetDynamicTree().isInLineOfSight(start.x, start.y, start.z, end.x, end.y, end.z,
                                                     unit->GetPhaseMask(), VMAP::ModelIgnoreFlags::Nothing);
    };
    auto floorContact = [&](Sample const& start, Sample const& end)
    {
        Sample hit{};
        if (!data.GetStaticTree().GetObjectHitPos(start.x, start.y, start.z, end.x, end.y, end.z, hit.x, hit.y, hit.z,
                                                  0))
            return false;
        float floor = data.GetStaticTree().getHeight(hit.x, hit.y, hit.z + 0.1f, 0.2f);
        dtPolyRef poly = 0;
        float nearest[3]{};
        if (!NavPoint(unit, a, poly, nearest))
            return false;
        dtMeshTile const* tile = nullptr;
        dtPoly const* navPoly = nullptr;
        auto mesh = data.GetMMapData().GetNavMesh();
        if (dtStatusFailed(mesh->getTileAndPolyByRef(poly, &tile, &navPoly)))
            return false;
        return WalkableFloorContact(a, b, hit, floor, tile->header->walkableClimb);
    };
    return CheckBodyClearance(a, b, unit->GetFloatValue(UNIT_FIELD_BOUNDINGRADIUS), unit->GetCollisionHeight(),
                              staticClear, dynamicClear, floorContact);
}

bool Corridor(Unit* unit, Sample const& a, Sample const& b)
{
    dtPolyRef poly = 0, endPoly = 0;
    float start[3], end[3];
    if (!NavPoint(unit, a, poly, start) || !NavPoint(unit, b, endPoly, end))
        return false;
    auto query = unit->GetMap()->GetMapCollisionData().GetMMapData().GetNavMeshQuery();
    auto filter = Filter();
    float actualStart[3]{a.y, a.z - unit->GetHoverHeight(), a.x};
    float actualEnd[3]{b.y, b.z - unit->GetHoverHeight(), b.x};
    if (ClearNavigationSegment(query, filter, poly, endPoly, actualStart, actualEnd))
        return true;
    // Native path initialization projects a near-edge starting point onto its
    // associated polygon. NavPoint bounded that association; physical body/floor
    // checks still validate the actual, unprojected segment.
    return ClearNavigationSegment(query, filter, poly, endPoly, start, end);
}

bool Validate(Unit* unit, Movement::MoveSplineInitArgs const& args)
{
    if (!scoped || unit->GetMapId() != Dungeon)
        return true;
    std::lock_guard lock(integrityMutex);
    if (!Participant(unit))
        return true;
    if (failed)
        return false;
    // Entry teleport is separately permitted. Native AI must not move this actor
    // until setup has validated its actual destination and initialized the audit.
    if (!EntryReady(unit))
        return false;
    if (!DataAvailable(unit))
        return Reject(unit, "missing_navigation_or_collision_data");
    if (args.path.size() < 2 || args.path.size() > MaxSamples)
        return Reject(unit, "invalid_spline_size");
    if (unit->GetTransport() || args.flags.flying || args.flags.cyclic || args.flags.animation)
        return Reject(unit, "unsupported_movement_mode");
    bool airborne = args.flags.parabolic || args.flags.falling;
    float normalSpeed = std::max({unit->GetSpeed(MOVE_RUN), unit->GetSpeed(MOVE_SWIM), unit->GetSpeed(MOVE_WALK)});
    if (!std::isfinite(args.velocity) || args.velocity <= 0.0f ||
        (!airborne && unit->IsPlayer() && !unit->HasUnitState(UNIT_STATE_CHARGING) &&
         args.velocity > normalSpeed * 1.05f + 0.1f))
        return Reject(unit, "unapproved_movement_speed");
    auto const& first = args.path.front();
    auto const& last = args.path.back();
    if (!airborne && (last - first).length() > 0.01f)
    {
        PathGenerator path(unit);
        if (!path.CalculatePath(first.x, first.y, first.z, last.x, last.y, last.z, false) ||
            !IsNormalNavigation(path.GetPathType(), PATHFIND_NORMAL))
            return Reject(unit, "non_walkable_path_or_shortcut");
    }
    Movement::MoveSpline simulation;
    simulation.Initialize(args);
    unsigned duration = simulation.Duration();
    unsigned step = std::clamp(static_cast<unsigned>(500.0f / args.velocity), 5u, 50u);
    if (duration / step + 2 > MaxSamples)
        return Reject(unit, "movement_validation_budget_exceeded");
    std::vector<Sample> curve;
    auto start = simulation.ComputePosition();
    curve.push_back({start.x, start.y, start.z, 0});
    for (unsigned elapsed = 0; elapsed < duration;)
    {
        unsigned advance = std::min(step, duration - elapsed);
        simulation.updateState(advance);
        elapsed += advance;
        auto point = simulation.ComputePosition();
        Sample sample{point.x, point.y, point.z, elapsed};
        bool bodyClear = BodyClear(unit, curve.back(), sample);
        bool grounded = airborne || Ground(unit, sample);
        bool corridorClear = airborne || Corridor(unit, curve.back(), sample);
        if (!bodyClear || !grounded || !corridorClear)
        {
            auto reason = Acore::StringFormat(
                "spline_crosses_collision_or_navigation_boundary:body={}:ground={}:corridor={}:sample_ms={}"
                ":from={},{},{}:to={},{},{}:radius={}:height={}",
                bodyClear ? 1 : 0, grounded ? 1 : 0, corridorClear ? 1 : 0, elapsed, curve.back().x, curve.back().y,
                curve.back().z, sample.x, sample.y, sample.z, unit->GetFloatValue(UNIT_FIELD_BOUNDINGRADIUS),
                unit->GetCollisionHeight());
            return Reject(unit, reason.c_str());
        }
        curve.push_back(sample);
    }
    if (curve.empty() || !Ground(unit, curve.back()) || (!airborne && !Ground(unit, curve.front())))
        return Reject(unit, "spline_endpoint_off_walkable_surface");
    if (audits.size() >= MaxActors && !audits.count(unit->GetGUID()))
        return Reject(unit, "movement_actor_budget_exceeded");
    auto& audit = audits[unit->GetGUID()];
    audit.spline = args.splineId;
    audit.curve = std::move(curve);
    audit.airborne = airborne;
    audit.peakSpeed = args.velocity;
    for (std::size_t i = 1; i < audit.curve.size(); ++i)
    {
        auto const& a = audit.curve[i - 1];
        auto const& b = audit.curve[i];
        audit.peakSpeed = std::max(audit.peakSpeed, Distance(a, b) * 1000.0f / (b.milliseconds - a.milliseconds));
    }
    return true;
}

void Observe(Unit* unit)
{
    if (!scoped || !unit->IsInWorld() || unit->GetMapId() != Dungeon || !unit->IsAlive())
        return;
    std::lock_guard lock(integrityMutex);
    if (!Participant(unit) || failed || !EntryReady(unit))
        return;
    if (!DataAvailable(unit))
    {
        Reject(unit, "collision_data_unavailable_during_audit");
        return;
    }
    if (audits.size() >= MaxActors && !audits.count(unit->GetGUID()))
    {
        Reject(unit, "movement_actor_budget_exceeded");
        return;
    }
    auto& audit = audits[unit->GetGUID()];
    uint64 now = Now();
    if (audit.previousTime && now - audit.previousTime < 200)
        return;
    auto actual = PositionOf(unit);
    float speed = std::max(unit->GetSpeed(MOVE_RUN), unit->GetSpeed(MOVE_SWIM));
    bool approved =
        unit->movespline->Initialized() && !unit->movespline->Finalized() && unit->movespline->GetId() == audit.spline;
    if (approved && !NearCurve(audit.curve, actual, unit->movespline->timePassed()))
    {
        Reject(unit, "position_does_not_match_approved_spline");
        return;
    }
    if (audit.previousTime &&
        (!WithinTravelBudget(audit.previous, actual, static_cast<unsigned>(now - audit.previousTime),
                             std::max(speed, audit.peakSpeed)) ||
         !BodyClear(unit, audit.previous, actual)))
    {
        Reject(unit, "impossible_displacement_or_collision_crossing");
        return;
    }
    if (!(approved && audit.airborne) && !Ground(unit, actual))
    {
        auto reason = "position_off_navigation_surface" + SurfaceDetails(unit);
        Reject(unit, reason.c_str());
        return;
    }
    audit.previous = actual;
    audit.previousTime = now;
    if (!approved)
        audit.peakSpeed = speed;
}

class AuditUpdates : public UnitScript
{
  public:
    AuditUpdates() : UnitScript("WaiEpisodeMovementAudit", true, {UNITHOOK_ON_UNIT_UPDATE}) {}
    void OnUnitUpdate(Unit* unit, uint32 /*diff*/) override
    {
        Observe(unit);
    }
};

class Teleports : public PlayerScript
{
  public:
    Teleports() : PlayerScript("WaiEpisodeTeleportGate", {PLAYERHOOK_ON_BEFORE_TELEPORT}) {}
    bool OnPlayerBeforeTeleport(Player* player, uint32 map, float x, float y, float z, float /*orientation*/,
                                uint32 /*options*/, Unit* /*target*/) override
    {
        if (!scoped)
            return true;
        std::lock_guard lock(integrityMutex);
        if (!Participant(player))
            return true;
        auto recovery = recoveryPermits.find(player->GetGUID());
        if (recovery != recoveryPermits.end() && Now() <= recovery->second.expires &&
            ((recovery->second.release && !player->IsAlive()) ||
             (!recovery->second.release && map == Dungeon &&
              Distance({x, y, z, 0}, recovery->second.destination) <= recovery->second.radius)))
        {
            recoveryPermits.erase(recovery);
            audits.erase(player->GetGUID());
            readyOwners.erase(player->GetGUID());
            return true;
        }
        auto permit = entryPermits.find(player->GetGUID());
        if (permit != entryPermits.end() && Now() <= permit->second && map == Dungeon &&
            Distance({x, y, z, 0}, {3.81f, -14.82f, -17.84f, 0}) < 0.1f)
        {
            entryPermits.erase(permit);
            audits.erase(player->GetGUID());
            readyOwners.erase(player->GetGUID());
            return true;
        }
        auto trigger = sObjectMgr->GetAreaTrigger(ExitTrigger);
        auto destination = sObjectMgr->GetAreaTriggerTeleport(ExitTrigger);
        if (trigger && destination && player->GetMapId() == Dungeon && player->IsInAreaTriggerRadius(trigger) &&
            map == destination->target_mapId &&
            Distance({x, y, z, 0}, {destination->target_X, destination->target_Y, destination->target_Z, 0}) < 0.1f)
            return true;
        return Reject(player, "unauthorized_teleport");
    }
};
} // namespace

bool Install()
{
    if (!Movement::RegisterSplineValidator(Validate))
        return false;
    new AuditUpdates();
    new Teleports();
    return true;
}

void Shutdown()
{
    ClearRoster();
    Movement::UnregisterSplineValidator(Validate);
}

void SetRoster(std::array<ObjectGuid, 5> const& roster)
{
    std::lock_guard lock(integrityMutex);
    members = roster;
    audits.clear();
    entryPermits.clear();
    recoveryPermits.clear();
    readyOwners.clear();
    failed = false;
    violation.clear();
    scoped = sConfigMgr->GetOption<bool>("Wai.Episodes.MovementIntegrity.Enable", false);
}

void ClearRoster()
{
    std::lock_guard lock(integrityMutex);
    scoped = false;
    members = {};
    audits.clear();
    entryPermits.clear();
    recoveryPermits.clear();
    readyOwners.clear();
    violation.clear();
    failed = false;
}

void PermitEntry(ObjectGuid player)
{
    std::lock_guard lock(integrityMutex);
    entryPermits[player] = Now() + 5000;
}

void PermitRecovery(ObjectGuid player, Player* anchor, float radius)
{
    std::lock_guard lock(integrityMutex);
    recoveryPermits[player] = {Now() + (anchor ? 5000 : 300000), anchor == nullptr,
                               anchor ? PositionOf(anchor) : Sample{}, std::max(1.0f, radius)};
}

bool Ready(Player* player)
{
    std::lock_guard lock(integrityMutex);
    if (!scoped)
        return true;
    if (failed || !scoped || !player->IsInWorld() || player->IsBeingTeleported() || player->GetMapId() != Dungeon ||
        !DataAvailable(player))
        return Reject(player, "invalid_initial_navigation_or_collision_surface");
    if (!Ground(player, PositionOf(player)))
    {
        auto reason = "invalid_initial_navigation_or_collision_surface" + SurfaceDetails(player);
        return Reject(player, reason.c_str());
    }
    auto& audit = audits[player->GetGUID()];
    audit.previous = PositionOf(player);
    audit.previousTime = Now();
    readyOwners.insert(player->GetGUID());
    return true;
}

std::string TakeViolation()
{
    std::lock_guard lock(integrityMutex);
    auto result = std::move(violation);
    violation.clear();
    return result;
}

BoundaryReading InspectBoundary(Player* player, float tolerance)
{
    // This read-only, per-party monitor is independent of the experimental spline gate.
    if (!player || !player->IsInWorld() || player->GetMapId() != Dungeon || player->IsBeingTeleported() ||
        !player->IsAlive() || player->GetTransport())
        return {};
    if (!DataAvailable(player))
        return {BoundaryState::Unavailable, 0};
    auto point = PositionOf(player);
    if (!Finite(point))
        return {BoundaryState::Unavailable, 0};
    LiquidData liquid;
    if (Water(player, point, liquid))
        return {BoundaryState::Inside, 0};
    float location[3]{point.y, point.z - player->GetHoverHeight(), point.x};
    auto& data = player->GetMap()->GetMapCollisionData();
    auto mesh = data.GetMMapData().GetNavMesh();
    int tileX = 0, tileY = 0;
    mesh->calcTileLoc(location, &tileX, &tileY);
    // An absent tile is not evidence of a bad policy action.
    if (!mesh->getTileAt(tileX, tileY, 0))
        return {BoundaryState::Unavailable, 0};
    float radius = tolerance + 2.0f;
    float extents[3]{radius, radius + 5.0f, radius};
    float nearest[3]{};
    dtPolyRef poly = 0;
    auto filter = Filter();
    // Prefer a polygon containing actual XY before the wider search, which may
    // otherwise select a neighbouring slope or a different floor by 3D distance.
    if (!NavPoint(player, point, poly, nearest))
    {
        poly = 0;
        auto status = data.GetMMapData().GetNavMeshQuery()->findNearestPoly(location, extents, &filter, &poly, nearest);
        if (dtStatusFailed(status))
            return {};
    }
    if (!poly)
        return {BoundaryState::Outside, radius};
    float horizontal = std::hypot(location[0] - nearest[0], location[2] - nearest[2]);
    float vertical = std::abs(location[1] - nearest[1]);
    float floor = data.GetStaticTree().getHeight(point.x, point.y, location[1] + tolerance, tolerance * 2.0f);
    bool nearFloor = std::isfinite(floor) && floor > INVALID_HEIGHT && std::abs(location[1] - floor) <= tolerance;
    if (horizontal <= tolerance && (vertical <= tolerance || nearFloor))
        return {BoundaryState::Inside, 0};
    return {BoundaryState::Outside, std::max(horizontal, vertical)};
}
} // namespace WaiEpisodes::Integrity
#else
namespace WaiEpisodes::Integrity
{
bool Install() { return false; }
void Shutdown() { }
void SetRoster(std::array<ObjectGuid, 5> const&) { }
void ClearRoster() { }
void PermitEntry(ObjectGuid) { }
void PermitRecovery(ObjectGuid, Player*, float) { }
bool Ready(Player*) { return false; }
BoundaryReading InspectBoundary(Player*, float) { return {BoundaryState::Unavailable, 0}; }
std::string TakeViolation() { return "movement_integrity_unavailable"; }
}
#endif
