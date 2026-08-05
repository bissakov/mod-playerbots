/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NEWRPGINFO_H
#define PLAYERBOTS_NEWRPGINFO_H

#include "Define.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Strategy.h"
#include "Timer.h"
#include "TravelMgr.h"

using NewRpgStatusTransitionProb = std::vector<std::vector<int>>;

enum class ZoneTravelMethod : uint8
{
    Walk,
    Transport,
    KnownTaxi,
    AreaTriggerPortal,
    KnownSpell,
    OwnedItem
};

struct ZoneTravelStep
{
    ZoneTravelMethod method{ZoneTravelMethod::Walk};
    WorldPosition from{};
    WorldPosition to{};
    uint32 objectId{0};
    ObjectGuid itemGuid{};
};

struct NewRpgInfo
{
    NewRpgInfo() : data(Idle{}) {}
    ~NewRpgInfo() = default;

    // RPG_GO_GRIND
    struct GoGrind
    {
        WorldPosition pos{};
    };
    // RPG_GO_CAMP
    struct GoCamp
    {
        WorldPosition pos{};
    };
    // RPG_WANDER_NPC
    struct WanderNpc
    {
        ObjectGuid npcOrGo{};
        uint32 lastReach{0};
    };
    // RPG_DO_ERRAND
    struct DoErrand
    {
        ObjectGuid npc{};
        uint32 lastReach{0};
    };
    // RPG_WANDER_RANDOM
    struct WanderRandom
    {
        WanderRandom() = default;
    };
    // RPG_DO_QUEST
    struct DoQuest
    {
        const Quest* quest{nullptr};
        uint32 questId{0};
        int32 objectiveIdx{0};
        uint32 siteId{0};
        WorldPosition pos{};
        uint32 lastReachPOI{0};
    };
    // RPG_TRAVEL_FLIGHT
    struct TravelFlight
    {
        uint32 flightMasterEntry{0};
        WorldPosition flightMasterPos{};
        std::vector<uint32> path;
        bool inFlight{false};
    };
    // RPG_REST
    struct Rest
    {
        Rest() = default;
    };
    // RPG_OUTDOOR_PVP
    struct OutdoorPvP
    {
        ObjectGuid::LowType capturePointSpawnId{0};
    };
    // RPG_TRAVEL_ZONE
    struct TravelZone
    {
        WorldPosition destination{};
        uint32 destinationZoneId{0};
        uint32 resumeQuestId{0};
        uint32 routeStage{0};
        uint32 loggedRouteStage{UINT32_MAX};
        uint32 retryCount{0};
        std::vector<uint32> failedHubExclusions;
        std::vector<ZoneTravelStep> route;
        float routeCost{0.0f};
        WorldPosition lastMeaningfulPosition{};
        uint32 lastMeaningfulMovementAt{0};
        bool breadcrumb{false};
        // Bracket maximum of the zone the bot grew out of. Non-zero marks the trip as level
        // bracket advancement, which every destination must beat.
        uint32 advancementBracketHigh{0};
    };
    struct Idle
    {
    };

    uint32 startT{0};  // start timestamp of the current status

    // MOVE_FAR
    float nearestMoveFarDis{FLT_MAX};
    uint32 stuckTs{0};
    uint32 stuckAttempts{0};
    WorldPosition moveFarPos;
    WorldPosition localRouteFailureAnchor;
    uint32 consecutiveLocalRouteFailures{0};
    uint32 lastPlayerUnstuckAt{0};
    // END MOVE_FAR

    // In-memory only. A fresh PlayerbotAI instance recalculates these after login.
    uint64 progressSignature{0};
    uint32 lastProgressCheckAt{0};
    uint32 lastProgressAt{0};
    uint32 migrationCooldownStartedAt{0};
    // Kept apart from migrationCooldownStartedAt: a bot that has outgrown its bracket is
    // usually still earning XP, and renewed progress must not make it retry every minute.
    uint32 advancementAttemptedAt{0};
    bool progressInitialized{false};

    // Timestamp of the first tick the bot was found in water too deep to stand in, cleared as soon
    // as it reaches water it can stand in or land.
    uint32 openWaterSince{0};
    WorldPosition openWaterAnchor{};
    bool openWaterRecovering{false};

    using RpgData = std::variant<
        Idle,
        GoGrind,
        GoCamp,
        WanderNpc,
        WanderRandom,
        DoQuest,
        Rest,
        TravelFlight,
        OutdoorPvP,
        TravelZone,
        DoErrand
    >;
    RpgData data;

    NewRpgStatus GetStatus();
    bool HasStatusPersisted(uint32 maxDuration) { return GetMSTimeDiffToNow(startT) > maxDuration; }
    void ChangeToGoGrind(WorldPosition pos);
    void ChangeToGoCamp(WorldPosition pos);
    void ChangeToWanderNpc();
    void ChangeToDoErrand(ObjectGuid npc);
    void ChangeToWanderRandom();
    void ChangeToDoQuest(uint32 questId, const Quest* quest);
    void ChangeToTravelFlight(uint32 flightMasterEntry, WorldPosition flightMasterPos, std::vector<uint32> path);
    void ChangeToTravelZone(WorldPosition destination, uint32 destinationZoneId, uint32 resumeQuestId, bool breadcrumb,
                            std::vector<ZoneTravelStep> route = {}, float routeCost = 0.0f,
                            std::vector<uint32> failedHubExclusions = {}, uint32 advancementBracketHigh = 0);
    void ChangeToOutdoorPvp(ObjectGuid::LowType capturePointSpawnId = 0);
    void ChangeToRest();
    void ChangeToIdle();
    bool CanChangeTo(NewRpgStatus status);
    void Reset();
    void SetMoveFarTo(WorldPosition pos);
    void ResetLocalRouteFailures();
    std::string ToString();
};

struct NewRpgStatistic
{
    uint32 questAccepted{0};
    uint32 questCompleted{0};
    uint32 questAbandoned{0};
    uint32 questRewarded{0};
    uint32 questDropped{0};
    uint32 zoneTransitionsStarted{0};
    uint32 zoneAdvancementsStarted{0};
    uint32 zoneArrivals{0};
    uint32 zoneReplans{0};
    uint32 zoneRouteFailures{0};
    uint32 playerUnstuckNudges{0};
    uint32 playerUnstuckHearths{0};
    uint32 openWaterSwims{0};
    uint32 openWaterRescues{0};
    NewRpgStatistic operator+(const NewRpgStatistic& other) const
    {
        NewRpgStatistic result;
        result.questAccepted = this->questAccepted + other.questAccepted;
        result.questCompleted = this->questCompleted + other.questCompleted;
        result.questAbandoned = this->questAbandoned + other.questAbandoned;
        result.questRewarded = this->questRewarded + other.questRewarded;
        result.questDropped = this->questDropped + other.questDropped;
        result.zoneTransitionsStarted = this->zoneTransitionsStarted + other.zoneTransitionsStarted;
        result.zoneAdvancementsStarted = this->zoneAdvancementsStarted + other.zoneAdvancementsStarted;
        result.zoneArrivals = this->zoneArrivals + other.zoneArrivals;
        result.zoneReplans = this->zoneReplans + other.zoneReplans;
        result.zoneRouteFailures = this->zoneRouteFailures + other.zoneRouteFailures;
        result.playerUnstuckNudges = this->playerUnstuckNudges + other.playerUnstuckNudges;
        result.playerUnstuckHearths = this->playerUnstuckHearths + other.playerUnstuckHearths;
        result.openWaterSwims = this->openWaterSwims + other.openWaterSwims;
        result.openWaterRescues = this->openWaterRescues + other.openWaterRescues;
        return result;
    }
    NewRpgStatistic& operator+=(const NewRpgStatistic& other)
    {
        this->questAccepted += other.questAccepted;
        this->questCompleted += other.questCompleted;
        this->questAbandoned += other.questAbandoned;
        this->questRewarded += other.questRewarded;
        this->questDropped += other.questDropped;
        this->zoneTransitionsStarted += other.zoneTransitionsStarted;
        this->zoneAdvancementsStarted += other.zoneAdvancementsStarted;
        this->zoneArrivals += other.zoneArrivals;
        this->zoneReplans += other.zoneReplans;
        this->zoneRouteFailures += other.zoneRouteFailures;
        this->playerUnstuckNudges += other.playerUnstuckNudges;
        this->playerUnstuckHearths += other.playerUnstuckHearths;
        this->openWaterSwims += other.openWaterSwims;
        this->openWaterRescues += other.openWaterRescues;
        return *this;
    }
};

#endif
