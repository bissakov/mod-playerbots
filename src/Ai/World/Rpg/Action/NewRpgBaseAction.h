/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NEWRPGBASEACTION_H
#define PLAYERBOTS_NEWRPGBASEACTION_H

#include "Duration.h"
#include "LastMovementValue.h"
#include "MovementActions.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "Object.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "PlayerbotAI.h"
#include "QuestDef.h"
#include "TravelMgr.h"

struct POIInfo
{
    WorldPosition pos;
    int32 objectiveIdx;
    uint32 siteId;
};

enum class MoveFarOutcome : uint8
{
    Moving,
    Waiting,
    Recovered,
    RouteFailed
};

/// A base (composition) class for all new rpg actions
/// All functions that may be shared by multiple actions should be declared here
/// And we should make all actions composable instead of inheritable
class NewRpgBaseAction : public MovementAction
{
public:
    NewRpgBaseAction(PlayerbotAI* botAI, std::string name) : MovementAction(botAI, name) {}

protected:
    /* MOVEMENT RELATED */
    MoveFarOutcome MoveFarTo(WorldPosition dest);
    bool MoveWorldObjectTo(ObjectGuid guid, float distance = INTERACTION_DISTANCE);
    bool MoveRandomNear(float moveStep = 50.0f, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL, WorldObject* center = nullptr);
    bool ForceToWait(uint32 duration, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);

    /* QUEST RELATED CHECK */
    ObjectGuid ChooseNpcOrGameObjectToInteract(bool questgiverOnly = false, float distanceLimit = 0.0f);
    bool HasQuestToAcceptOrReward(WorldObject* object);
    bool InteractWithNpcOrGameObjectForQuest(ObjectGuid guid);
    bool CanInteractWithQuestGiver(Object* questGiver);
    bool IsWithinInteractionDist(Object* object);
    uint32 BestRewardIndex(Quest const* quest);
    bool IsQuestWorthDoing(Quest const* quest);
    bool IsQuestCapableDoing(Quest const* quest);

    /* QUEST RELATED ACTION */
    bool SearchQuestGiverAndAcceptOrReward();
    bool AcceptQuest(Quest const* quest, ObjectGuid guid);
    bool TurnInQuest(Quest const* quest, ObjectGuid guid);
    bool OrganizeQuestLog();

protected:
    bool GetQuestPOIPosAndObjectiveIdx(uint32 questId, std::vector<POIInfo>& poiInfo, bool toComplete = false);
    bool CheckProgress(bool& checked);
    bool FindCrossZoneBreadcrumb(WorldPosition& destination, uint32& questId);
    bool IsLocalObjective(WorldPosition const& pos, uint32 poiZone) const;
    bool CanFollowCrossZoneObjective(uint32 poiZone, int32 objectiveIdx) const;
    void SplitWorkableObjectives(std::vector<POIInfo> const& poiInfo, std::vector<POIInfo>& local,
                                 std::vector<POIInfo>& remote) const;
    bool StartZoneTravel(WorldPosition requestedDestination = WorldPosition(), uint32 resumeQuestId = 0,
                         bool breadcrumb = false, std::vector<uint32> failedHubExclusions = {},
                         uint32 advancementBracketHigh = 0);
    bool HasOutgrownCurrentZone(uint32& bracketHigh) const;
    bool HasOutgrownZone(uint32 zoneId, uint32& bracketHigh) const;
    bool StartZoneAdvancementTravel();
    void NoteZoneArrival(uint32 destinationZoneId);
    bool CanStartBreadcrumbTravel() const;
    bool RebuildZoneTravelRoute(NewRpgInfo::TravelZone& data);
    bool SelectAlternateZoneHub(NewRpgInfo::TravelZone& data);
    void FinishZoneTravelFailure();
    uint32 GetPositionZoneId(WorldPosition const& position) const;
    static WorldPosition SelectRandomGrindPos(Player* bot);
    static WorldPosition SelectRandomCampPos(Player* bot);
    bool SelectRandomFlightTaxiNode(uint32& flightMasterEntry, WorldPosition& flightMasterPos, std::vector<uint32>& path);
    bool RandomChangeStatus(std::vector<NewRpgStatus> candidateStatus);
    bool CheckRpgStatusAvailable(NewRpgStatus status);

protected:
    /* FOR MOVE FAR */
    const float pathFinderDis = 70.0f;
    // Time without meaningful movement before reporting a route failure.
    const uint32 stuckTime = 90 * 1000;
};

#endif
