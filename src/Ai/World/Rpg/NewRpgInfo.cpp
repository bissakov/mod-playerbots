/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgInfo.h"

#include <algorithm>
#include <cmath>

#include "Timer.h"

void NewRpgInfo::ChangeToGoGrind(WorldPosition pos)
{
    startT = getMSTime();
    data = GoGrind{pos};
}

void NewRpgInfo::ChangeToGoCamp(WorldPosition pos)
{
    startT = getMSTime();
    data = GoCamp{pos};
}

void NewRpgInfo::ChangeToWanderNpc()
{
    startT = getMSTime();
    data = WanderNpc{};
}

void NewRpgInfo::ChangeToDoErrand(ObjectGuid npc)
{
    startT = getMSTime();
    data = DoErrand{npc};
}

void NewRpgInfo::ChangeToWanderRandom()
{
    startT = getMSTime();
    data = WanderRandom{};
}

void NewRpgInfo::ChangeToDoQuest(uint32 questId, const Quest* quest)
{
    startT = getMSTime();
    DoQuest do_quest;
    do_quest.questId = questId;
    do_quest.quest = quest;
    data = do_quest;
}

void NewRpgInfo::ChangeToTravelFlight(uint32 flightMasterEntry, WorldPosition flightMasterPos, std::vector<uint32> path)
{
    startT = getMSTime();
    TravelFlight flight;
    flight.flightMasterEntry = flightMasterEntry;
    flight.flightMasterPos = flightMasterPos;
    flight.path = std::move(path);
    flight.inFlight = false;
    data = flight;
}

void NewRpgInfo::ChangeToTravelZone(WorldPosition destination, uint32 destinationZoneId, uint32 resumeQuestId,
                                    bool breadcrumb, std::vector<ZoneTravelStep> route, float routeCost,
                                    std::vector<uint32> failedHubExclusions, uint32 advancementBracketHigh)
{
    startT = getMSTime();
    TravelZone travel;
    travel.destination = destination;
    travel.destinationZoneId = destinationZoneId;
    travel.resumeQuestId = resumeQuestId;
    travel.failedHubExclusions = std::move(failedHubExclusions);
    travel.route = std::move(route);
    travel.routeCost = routeCost;
    travel.lastMeaningfulPosition = WorldPosition();
    travel.lastMeaningfulMovementAt = startT;
    travel.breadcrumb = breadcrumb;
    travel.advancementBracketHigh = advancementBracketHigh;
    data = std::move(travel);
}

void NewRpgInfo::ChangeToOutdoorPvp(ObjectGuid::LowType capturePointSpawnId)
{
    startT = getMSTime();
    OutdoorPvP pvp;
    pvp.capturePointSpawnId = capturePointSpawnId;
    data = pvp;
}

void NewRpgInfo::ChangeToRest()
{
    startT = getMSTime();
    data = Rest{};
}

void NewRpgInfo::ChangeToIdle()
{
    startT = getMSTime();
    data = Idle{};
}

bool NewRpgInfo::CanChangeTo(NewRpgStatus)
{
    return true;
}

void NewRpgInfo::Reset()
{
    data = Idle{};
    startT = getMSTime();
}

void NewRpgInfo::ResetKeepingZoneMemory()
{
    std::vector<ZoneDeparture> departures = std::move(recentZoneDepartures);
    uint32 arrivalAt = lastZoneArrivalAt;
    *this = NewRpgInfo();
    recentZoneDepartures = std::move(departures);
    lastZoneArrivalAt = arrivalAt;
}

void NewRpgInfo::SetMoveFarTo(WorldPosition pos)
{
    nearestMoveFarDis = FLT_MAX;
    stuckTs = 0;
    stuckAttempts = 0;
    moveFarPos = pos;
}

void NewRpgInfo::ResetLocalRouteFailures()
{
    localRouteFailureAnchor = WorldPosition();
    consecutiveLocalRouteFailures = 0;
}

void NewRpgInfo::NoteZoneDeparture(uint32 zoneId)
{
    if (!zoneId)
        return;

    uint32 now = getMSTime();
    for (ZoneDeparture& departure : recentZoneDepartures)
    {
        if (departure.zoneId != zoneId)
            continue;

        departure.leftAt = now;
        return;
    }

    // A handful of zones is enough to cover the neighbours a bot can bounce between, and the
    // oldest entry is the one whose cooldown is closest to expiring anyway.
    constexpr std::size_t maxRememberedZones = 8;
    if (recentZoneDepartures.size() >= maxRememberedZones)
    {
        auto oldest = std::min_element(recentZoneDepartures.begin(), recentZoneDepartures.end(),
                                       [](ZoneDeparture const& left, ZoneDeparture const& right)
                                       {
                                           return GetMSTimeDiffToNow(left.leftAt) >
                                                  GetMSTimeDiffToNow(right.leftAt);
                                       });
        recentZoneDepartures.erase(oldest);
    }

    recentZoneDepartures.push_back({zoneId, now});
}

bool NewRpgInfo::LeftZoneRecently(uint32 zoneId, uint32 withinMs) const
{
    if (!zoneId || !withinMs)
        return false;

    for (ZoneDeparture const& departure : recentZoneDepartures)
        if (departure.zoneId == zoneId && GetMSTimeDiffToNow(departure.leftAt) < withinMs)
            return true;

    return false;
}

NewRpgStatus NewRpgInfo::GetStatus()
{
    return std::visit([](auto&& arg) -> NewRpgStatus {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Idle>) return RPG_IDLE;
        if constexpr (std::is_same_v<T, GoGrind>) return RPG_GO_GRIND;
        if constexpr (std::is_same_v<T, GoCamp>) return RPG_GO_CAMP;
        if constexpr (std::is_same_v<T, WanderNpc>) return RPG_WANDER_NPC;
        if constexpr (std::is_same_v<T, WanderRandom>) return RPG_WANDER_RANDOM;
        if constexpr (std::is_same_v<T, Rest>) return RPG_REST;
        if constexpr (std::is_same_v<T, DoQuest>) return RPG_DO_QUEST;
        if constexpr (std::is_same_v<T, TravelFlight>) return RPG_TRAVEL_FLIGHT;
        if constexpr (std::is_same_v<T, OutdoorPvP>) return RPG_OUTDOOR_PVP;
        if constexpr (std::is_same_v<T, TravelZone>) return RPG_TRAVEL_ZONE;
        if constexpr (std::is_same_v<T, DoErrand>) return RPG_DO_ERRAND;
        return RPG_IDLE;
    }, data);
}

std::string NewRpgInfo::ToString()
{
    std::stringstream out;
    out << "Status: ";
    std::visit([&out, this](auto&& arg)
    {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, GoGrind>)
        {
            out << "GO_GRIND";
            out << "\nGrindPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastGoGrind: " << startT;
        }
        else if constexpr (std::is_same_v<T, GoCamp>)
        {
            out << "GO_CAMP";
            out << "\nCampPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastGoCamp: " << startT;
        }
        else if constexpr (std::is_same_v<T, WanderNpc>)
        {
            out << "WANDER_NPC";
            out << "\nnpcOrGoEntry: " << arg.npcOrGo.GetCounter();
            out << "\nlastWanderNpc: " << startT;
            out << "\nlastReachNpcOrGo: " << arg.lastReach;
        }
        else if constexpr (std::is_same_v<T, WanderRandom>)
        {
            out << "WANDER_RANDOM";
            out << "\nlastWanderRandom: " << startT;
        }
        else if constexpr (std::is_same_v<T, DoErrand>)
        {
            out << "DO_ERRAND";
            out << "\nnpc: " << arg.npc.ToString();
            out << "\nlastErrand: " << startT;
            out << "\nlastReachNpc: " << arg.lastReach;
        }
        else if constexpr (std::is_same_v<T, Idle>)
        {
            out << "IDLE";
        }
        else if constexpr (std::is_same_v<T, Rest>)
        {
            out << "REST";
            out << "\nlastRest: " << startT;
        }
        else if constexpr (std::is_same_v<T, DoQuest>)
        {
            out << "DO_QUEST";
            out << "\nquestId: " << arg.questId;
            out << "\nobjectiveIdx: " << arg.objectiveIdx;
            out << "\npoiPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastReachPOI: " << (arg.lastReachPOI ? GetMSTimeDiffToNow(arg.lastReachPOI) : 0);
        }
        else if constexpr (std::is_same_v<T, TravelFlight>)
        {
            out << "TRAVEL_FLIGHT";
            out << "\nflightMasterEntry: " << arg.flightMasterEntry;
            out << "\nfromNode: " << arg.path[0];
            out << "\ntoNode: " << arg.path[arg.path.size() - 1];
            out << "\ninFlight: " << arg.inFlight;
        }
        else if constexpr (std::is_same_v<T, OutdoorPvP>)
        {
            out << "OUTDOOR_PVP";
            if (!arg.capturePointSpawnId)
                out << "\nNo capture point assigned.";
            else
                out << "\ncapturePointSpawnId: " << arg.capturePointSpawnId;
        }
        else if constexpr (std::is_same_v<T, TravelZone>)
        {
            out << "TRAVEL_ZONE";
            out << "\ndestinationZone: " << arg.destinationZoneId;
            out << "\ndestination: " << arg.destination.GetMapId() << " " << arg.destination.GetPositionX() << " "
                << arg.destination.GetPositionY() << " " << arg.destination.GetPositionZ();
            out << "\nrouteStage: " << arg.routeStage << "/" << arg.route.size();
            out << "\nretryCount: " << arg.retryCount;
            out << "\nbreadcrumbQuest: " << arg.resumeQuestId;
            out << "\nadvancementBracketHigh: " << arg.advancementBracketHigh;
        }
        else
            out << "UNKNOWN";
    }, data);
    return out.str();
}
