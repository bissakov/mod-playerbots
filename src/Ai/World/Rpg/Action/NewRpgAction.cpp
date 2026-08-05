/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgAction.h"

#include <cmath>
#include <cstdlib>
#include <limits>

#include "AreaDefines.h"
#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "G3D/Vector2.h"
#include "GossipDef.h"
#include "IVMapMgr.h"
#include "MaintenanceValues.h"
#include "Map.h"
#include "MotionMaster.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "Trainer.h"
#include "TravelMgr.h"
#include "ZoneTravelPolicy.h"

bool TellRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;
    std::string out = botAI->rpgInfo.ToString();
    bot->Whisper(out.c_str(), LANG_UNIVERSAL, owner);
    return true;
}

bool StartRpgDoQuestAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string const text = event.getParam();
    PlayerbotChatHandler ch(owner);
    uint32 questId = ch.extractQuestId(text);
    const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
    if (quest)
    {
        botAI->rpgInfo.ChangeToDoQuest(questId, quest);
        bot->Whisper("Start to do quest " + std::to_string(questId), LANG_UNIVERSAL, owner);
        return true;
    }
    bot->Whisper("Invalid quest " + text, LANG_UNIVERSAL, owner);
    return false;
}

bool NewRpgStatusUpdateAction::NeedsErrandAt(ObjectGuid npcGuid)
{
    Unit* unit = botAI->GetUnit(npcGuid);
    Creature* npc = unit ? unit->ToCreature() : nullptr;
    if (!npc)
        return false;

    bool needsVendor = AI_VALUE(bool, "should sell") && AI_VALUE(bool, "can sell");
    if (needsVendor && npc->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
        return true;

    if (AI_VALUE(bool, "should repair") && AI_VALUE(bool, "can repair") &&
        npc->HasNpcFlag(UNIT_NPC_FLAG_REPAIR))
        return true;

    if (AI_VALUE(bool, "should buy bags") && CanBuyBagUpgradeAt(botAI, npc))
        return true;

    if (AI_VALUE(bool, "can train") && AI_VALUE(uint32, "train cost") > 0 &&
        npc->HasNpcFlag(UNIT_NPC_FLAG_TRAINER_CLASS))
    {
        Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(npc->GetEntry());
        if (trainer && trainer->GetTrainerType() == Trainer::Type::Class && trainer->IsTrainerValidForPlayer(bot))
            return true;
    }

    return AI_VALUE(bool, "should bank") && npc->HasNpcFlag(UNIT_NPC_FLAG_BANKER);
}

ObjectGuid NewRpgStatusUpdateAction::FindErrandNpc()
{
    for (ObjectGuid const& guid : AI_VALUE(GuidVector, "possible new rpg targets"))
    {
        if (NeedsErrandAt(guid))
            return guid;
    }

    return ObjectGuid::Empty;
}

bool NewRpgStatusUpdateAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    NewRpgStatus status = info.GetStatus();

    if (botAI->IsResurrectionSicknessRecoveryActive())
    {
        if (status == RPG_DO_ERRAND)
        {
            auto const& errand = std::get<NewRpgInfo::DoErrand>(info.data);
            if (NeedsErrandAt(errand.npc))
                return false;
        }

        if (ObjectGuid errandNpc = FindErrandNpc())
        {
            info.ChangeToDoErrand(errandNpc);
            botAI->LogObjectiveReplacement("Resurrection Sickness service errand");
            return true;
        }

        if (status == RPG_REST)
            return false;

        info.ChangeToRest();
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        botAI->LogObjectiveReplacement("Resurrection Sickness rest");
        return true;
    }

    if (botAI->IsInsideAvoidedArea())
    {
        if (auto const* grind = std::get_if<NewRpgInfo::GoGrind>(&info.data))
        {
            if (!botAI->IsPositionAvoided(FailedObjectiveType::Area, grind->pos))
                return false;
        }
        else if (auto const* camp = std::get_if<NewRpgInfo::GoCamp>(&info.data))
        {
            if (!botAI->IsPositionAvoided(FailedObjectiveType::Area, camp->pos))
                return false;
        }

        WorldPosition escape = SelectRandomGrindPos(bot);
        if (escape != WorldPosition() && !botAI->IsPositionAvoided(FailedObjectiveType::Area, escape) &&
            !botAI->IsPositionAvoided(FailedObjectiveType::Grind, escape))
        {
            info.ChangeToGoGrind(escape);
            botAI->LogObjectiveReplacement("failed-area evacuation");
            LOG_INFO("playerbots", "{} evacuating avoided area toward ({}, {:.1f}, {:.1f}, {:.1f})", bot->GetName(),
                     escape.GetMapId(), escape.GetPositionX(), escape.GetPositionY(), escape.GetPositionZ());
            return true;
        }

        escape = SelectRandomCampPos(bot);
        if (escape != WorldPosition() && !botAI->IsPositionAvoided(FailedObjectiveType::Area, escape) &&
            !botAI->IsPositionAvoided(FailedObjectiveType::Camp, escape))
        {
            info.ChangeToGoCamp(escape);
            botAI->LogObjectiveReplacement("failed-area evacuation");
            LOG_INFO("playerbots", "{} evacuating avoided area toward ({}, {:.1f}, {:.1f}, {:.1f})", bot->GetName(),
                     escape.GetMapId(), escape.GetPositionX(), escape.GetPositionY(), escape.GetPositionZ());
            return true;
        }

        if (status != RPG_WANDER_RANDOM)
        {
            info.ChangeToWanderRandom();
            LOG_WARN("playerbots", "{} found no planned destination outside an avoided area; wandering out",
                     bot->GetName());
            return true;
        }

        return false;
    }

    bool progressChecked = false;
    bool madeProgress = CheckProgress(progressChecked);

    // Only the no-progress fallback migration should be abandoned once the bot starts
    // progressing again: it exists purely to unstick a bot that had stopped advancing.
    // A breadcrumb migration is walking towards a quest objective in another zone, and
    // killing things on the way is the expected behaviour, not a reason to turn back.
    // Cancelling those meant almost every trip aborted within seconds of starting.
    // An advancement migration is the same case: the bot is still earning XP in the zone
    // it outgrew, so cancelling on progress would keep it there forever.
    bool cancellableTravel = false;
    if (status == RPG_TRAVEL_ZONE)
    {
        auto const& travelData = std::get<NewRpgInfo::TravelZone>(info.data);
        cancellableTravel = !travelData.breadcrumb && !travelData.advancementBracketHigh;
    }

    if (madeProgress && status == RPG_TRAVEL_ZONE && cancellableTravel)
    {
        LOG_INFO("playerbots", "[New RPG] {} cancelled zone travel after renewed XP or quest progress",
                 bot->GetName());
        info.ChangeToIdle();
        return true;
    }

    if (sPlayerbotAIConfig.zoneProgressionEnabled && progressChecked && status != RPG_TRAVEL_ZONE)
    {
        bool inRetryCooldown = info.migrationCooldownStartedAt &&
                               GetMSTimeDiffToNow(info.migrationCooldownStartedAt) <
                                   sPlayerbotAIConfig.zoneProgressionRetryCooldown * IN_MILLISECONDS;
        if (!inRetryCooldown)
        {
            WorldPosition breadcrumbDestination;
            uint32 breadcrumbQuest = 0;
            if (CanStartBreadcrumbTravel() && FindCrossZoneBreadcrumb(breadcrumbDestination, breadcrumbQuest))
            {
                if (StartZoneTravel(breadcrumbDestination, breadcrumbQuest, true))
                    return true;

                info.migrationCooldownStartedAt = getMSTime();
                botAI->rpgStatistic.zoneRouteFailures++;
                LOG_WARN("playerbots",
                         "[New RPG] {} cannot find a legitimate route for breadcrumb quest {} "
                         "(lvl {}, fromZone {}, toZone {}, map {}->{}, dist {:.0f})",
                         bot->GetName(), breadcrumbQuest, bot->GetLevel(), bot->GetZoneId(),
                         GetPositionZoneId(breadcrumbDestination), bot->GetMapId(),
                         breadcrumbDestination.GetMapId(),
                         bot->GetMapId() == breadcrumbDestination.GetMapId()
                             ? bot->GetExactDist(breadcrumbDestination)
                             : -1.0f);
            }
            // A bot that has grown out of its zone bracket moves on the way a player does,
            // without waiting for the no-progress fallback: it is still earning XP, just in a
            // zone whose content it has outlevelled, so that fallback would never fire.
            else if (StartZoneAdvancementTravel())
            {
                return true;
            }
            else
            {
                uint32 stableJitter = (bot->GetGUID().GetCounter() % 301) * IN_MILLISECONDS;
                uint32 timeout = sPlayerbotAIConfig.zoneProgressionNoProgressTimeout * IN_MILLISECONDS + stableJitter;
                if (info.lastProgressAt && GetMSTimeDiffToNow(info.lastProgressAt) >= timeout)
                {
                    if (StartZoneTravel())
                        return true;

                    info.migrationCooldownStartedAt = getMSTime();
                    botAI->rpgStatistic.zoneRouteFailures++;
                    LOG_WARN("playerbots",
                             "[New RPG] {} found no legitimate faction-safe route after {} seconds without progress",
                             bot->GetName(), timeout / IN_MILLISECONDS);
                }
            }
        }
    }

    status = info.GetStatus();
    if (status != RPG_DO_ERRAND && status != RPG_TRAVEL_FLIGHT && status != RPG_TRAVEL_ZONE)
    {
        ObjectGuid errandNpc = FindErrandNpc();
        if (errandNpc)
        {
            info.ChangeToDoErrand(errandNpc);
            return true;
        }
    }

    switch (status)
    {
        case RPG_IDLE:
            return RandomChangeStatus({RPG_GO_CAMP, RPG_GO_GRIND, RPG_WANDER_RANDOM, RPG_WANDER_NPC, RPG_DO_QUEST,
                                       RPG_TRAVEL_FLIGHT, RPG_REST, RPG_OUTDOOR_PVP});

        case RPG_GO_GRIND:
        {
            auto& data = std::get<NewRpgInfo::GoGrind>(info.data);
            WorldPosition& originalPos = data.pos;
            assert(data.pos != WorldPosition());
            // GO_GRIND -> WANDER_RANDOM
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderRandom();
                return true;
            }
            break;
        }
        case RPG_GO_CAMP:
        {
            auto& data = std::get<NewRpgInfo::GoCamp>(info.data);
            WorldPosition& originalPos = data.pos;
            assert(data.pos != WorldPosition());
            // GO_CAMP -> WANDER_NPC
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderNpc();
                return true;
            }
            break;
        }
        case RPG_WANDER_RANDOM:
        {
            // WANDER_RANDOM -> IDLE
            if (info.HasStatusPersisted(statusWanderRandomDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_WANDER_NPC:
        {
            if (info.HasStatusPersisted(statusWanderNpcDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_DO_ERRAND:
        {
            auto& data = std::get<NewRpgInfo::DoErrand>(info.data);
            if (!NeedsErrandAt(data.npc) || info.HasStatusPersisted(statusDoErrandDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_DO_QUEST:
        {
            // DO_QUEST -> IDLE
            if (info.HasStatusPersisted(statusDoQuestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            auto& data = std::get<NewRpgInfo::TravelFlight>(info.data);
            if (data.inFlight && !bot->IsInFlight())
            {
                // flight arrival
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_REST:
        {
            // REST -> IDLE
            if (info.HasStatusPersisted(statusRestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_OUTDOOR_PVP:
        {
            if (info.HasStatusPersisted(statusOutDoorPvPDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_TRAVEL_ZONE:
        {
            auto& data = std::get<NewRpgInfo::TravelZone>(info.data);
            if (bot->GetZoneId() == data.destinationZoneId && bot->GetExactDist(data.destination) < 80.0f)
            {
                uint32 resumeQuestId = data.resumeQuestId;
                NoteZoneArrival(data.destinationZoneId);
                if (resumeQuestId)
                {
                    if (Quest const* quest = sObjectMgr->GetQuestTemplate(resumeQuestId))
                        info.ChangeToDoQuest(resumeQuestId, quest);
                    else
                        info.ChangeToIdle();
                }
                else
                    info.ChangeToIdle();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return false;
}

bool NewRpgGoGrindAction::Execute(Event /*event*/)
{
    if (botAI->IsResurrectionSicknessRecoveryActive())
        return false;

    if (!botAI->IsInsideAvoidedArea() && SearchQuestGiverAndAcceptOrReward())
        return true;
    if (auto* data = std::get_if<NewRpgInfo::GoGrind>(&botAI->rpgInfo.data))
    {
        if (MoveFarTo(data->pos) != MoveFarOutcome::RouteFailed)
            return true;
        // Small nudge so the next tick's MoveFarTo starts from a
        // slightly different position. Kept small so it doesn't look
        // like the bot is abandoning its destination.
        return MoveRandomNear(10.0f);
    }

    return false;
}

bool NewRpgGoCampAction::Execute(Event /*event*/)
{
    if (botAI->IsResurrectionSicknessRecoveryActive())
        return false;

    if (!botAI->IsInsideAvoidedArea() && SearchQuestGiverAndAcceptOrReward())
        return true;

    if (auto* data = std::get_if<NewRpgInfo::GoCamp>(&botAI->rpgInfo.data))
    {
        if (MoveFarTo(data->pos) != MoveFarOutcome::RouteFailed)
            return true;
        return MoveRandomNear(10.0f);
    }

    return false;
}

bool NewRpgWanderRandomAction::Execute(Event /*event*/)
{
    if (botAI->IsResurrectionSicknessRecoveryActive())
        return false;

    if (botAI->IsInsideAvoidedArea())
        return MoveRandomNear();

    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveRandomNear();
}

bool NewRpgWanderNpcAction::Execute(Event /*event*/)
{
    if (botAI->IsResurrectionSicknessRecoveryActive())
        return false;

    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::WanderNpc>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;
    if (!data.npcOrGo)
    {
        // No npc can be found, switch to IDLE
        ObjectGuid npcOrGo = ChooseNpcOrGameObjectToInteract();
        if (npcOrGo.IsEmpty())
        {
            info.ChangeToIdle();
            return true;
        }
        data.npcOrGo = npcOrGo;
        data.lastReach = 0;
        return true;
    }

    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, data.npcOrGo);
    if (object && IsWithinInteractionDist(object))
    {
        if (!data.lastReach)
        {
            data.lastReach = getMSTime();
            if (bot->CanInteractWithQuestGiver(object))
                InteractWithNpcOrGameObjectForQuest(data.npcOrGo);
            return true;
        }

        if (data.lastReach && GetMSTimeDiffToNow(data.lastReach) < npcStayTime)
            return false;

        // has reached the npc for more than `npcStayTime`, select the next target
        data.npcOrGo = ObjectGuid();
        data.lastReach = 0;
    }
    else
    {
        if (MoveWorldObjectTo(data.npcOrGo))
            return true;
        // NPC pathing failed (random offset in a wall, mmap hiccup, etc).
        // Take a small random step so the next tick retries from a
        // different spot instead of staring at the NPC from afar.
        return MoveRandomNear(15.0f);
    }

    return true;
}

bool NewRpgDoErrandAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    auto* data = std::get_if<NewRpgInfo::DoErrand>(&info.data);
    if (!data || !data->npc)
        return false;

    WorldObject* npc = ObjectAccessor::GetWorldObject(*bot, data->npc);
    if (!npc)
    {
        info.ChangeToIdle();
        return true;
    }

    if (IsWithinInteractionDist(npc))
    {
        if (!data->lastReach)
            data->lastReach = getMSTime();

        // The chores strategy has higher relevance and performs the actual interaction.
        return false;
    }

    if (MoveWorldObjectTo(data->npc))
        return true;

    return MoveRandomNear(15.0f);
}

bool NewRpgDoQuestAction::Execute(Event /*event*/)
{
    if (botAI->IsResurrectionSicknessRecoveryActive())
        return false;

    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::DoQuest>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;
    uint32 questId = data.questId;
    uint8 questStatus = bot->GetQuestStatus(questId);
    switch (questStatus)
    {
        case QUEST_STATUS_INCOMPLETE:
            return DoIncompleteQuest(data);
        case QUEST_STATUS_COMPLETE:
            return DoCompletedQuest(data);
        default:
            break;
    }
    info.ChangeToIdle();
    return true;
}

bool NewRpgDoQuestAction::DoIncompleteQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = data.questId;
    if (data.pos != WorldPosition())
    {
        /// @TODO: extract to a new function
        int32 currentObjective = data.objectiveIdx;
        // check if the objective has completed
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
        bool completed = true;
        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] < quest->RequiredNpcOrGoCount[currentObjective])
                completed = false;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] <
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                completed = false;
        }
        // the current objective is completed, clear and find a new objective later
        if (completed)
        {
            data.lastReachPOI = 0;
            data.pos = WorldPosition();
            data.objectiveIdx = 0;
            data.siteId = 0;
        }
    }
    if (data.pos == WorldPosition())
    {
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
        {
            // can't find a poi pos to go, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        // An objective in the zone the bot is standing in is worked before one that needs a
        // zone crossing, so a quest log spanning two zones stops deciding by dice roll where
        // the bot walks next.
        std::vector<POIInfo> localPoi;
        std::vector<POIInfo> remotePoi;
        SplitWorkableObjectives(poiInfo, localPoi, remotePoi);

        std::vector<POIInfo> const& selectable = localPoi.empty() ? remotePoi : localPoi;
        if (selectable.empty())
        {
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }

        uint32 rndIdx = urand(0, selectable.size() - 1);
        int32 objectiveIdx = selectable[rndIdx].objectiveIdx;
        data.lastReachPOI = 0;
        data.pos = selectable[rndIdx].pos;
        data.objectiveIdx = objectiveIdx;
        data.siteId = selectable[rndIdx].siteId;

        // The same test that classified the objective decides whether reaching it is a walk or
        // a migration. Comparing zone ids here instead turned an objective just across a border
        // into a hub-to-hub journey, and that journey is what the bot then reversed.
        if (!IsLocalObjective(data.pos, GetPositionZoneId(data.pos)))
        {
            if (StartZoneTravel(data.pos, questId, true))
                return true;
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
    }

    if (bot->GetDistance(data.pos) > 10.0f && !data.lastReachPOI)
    {
        if (MoveFarTo(data.pos) != MoveFarOutcome::RouteFailed)
            return true;
        // Long-range sampler couldn't land a candidate — nudge the
        // bot a short distance so the next tick retries from a
        // different position instead of sitting idle.
        return MoveRandomNear(10.0f);
    }
    // Now we are near the quest objective
    // kill mobs and looting quest should be done automatically by grind strategy

    if (!data.lastReachPOI)
    {
        data.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(data.lastReachPOI) >= poiStayTime)
    {
        bool hasProgression = false;
        int32 currentObjective = data.objectiveIdx;
        // check if the objective has progression
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);
        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] != 0 && quest->RequiredNpcOrGoCount[currentObjective])
                hasProgression = true;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] != 0 &&
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                hasProgression = true;
        }
        if (!hasProgression)
        {
            // we has reach the poi for more than 5 mins but no progession
            // may not be able to complete this quest, marked as abandoned
            /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
            botAI->lowPriorityQuest.insert(questId);
            botAI->rpgStatistic.questAbandoned++;
            LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        // clear and select another poi later
        data.lastReachPOI = 0;
        data.pos = WorldPosition();
        data.objectiveIdx = 0;
        data.siteId = 0;
        return true;
    }

    // At the POI: keep the bot actively placed but avoid large
    // random 20yd hops that look like pacing back and forth. A small
    // ~8yd wander reads as the bot looking around while grind/loot
    // strategies do their work.
    return MoveRandomNear(8.0f);
}

bool NewRpgDoQuestAction::DoCompletedQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = data.questId;
    const Quest* quest = data.quest;

    if (data.objectiveIdx != -1)
    {
        // if quest is completed, back to poi with -1 idx to reward
        BroadcastHelper::BroadcastQuestUpdateComplete(botAI, bot, quest);
        botAI->rpgStatistic.questCompleted++;
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true))
        {
            // can't find a poi pos to reward, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return false;
        }
        assert(poiInfo.size() > 0);
        data.lastReachPOI = 0;
        data.pos = poiInfo[0].pos;
        data.objectiveIdx = -1;
        data.siteId = poiInfo[0].siteId;

        uint32 turnInZone = GetPositionZoneId(data.pos);
        if (!IsLocalObjective(data.pos, turnInZone))
        {
            if (!CanFollowCrossZoneObjective(turnInZone, -1))
            {
                data.pos = WorldPosition();
                botAI->rpgInfo.ChangeToIdle();
                return true;
            }

            if (StartZoneTravel(data.pos, questId, true))
                return true;
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
    }

    if (data.pos == WorldPosition())
        return false;

    if (bot->GetDistance(data.pos) > 10.0f && !data.lastReachPOI)
    {
        if (MoveFarTo(data.pos) != MoveFarOutcome::RouteFailed)
            return true;
        return MoveRandomNear(10.0f);
    }

    // Now we are near the qoi of reward
    // the quest should be rewarded by SearchQuestGiverAndAcceptOrReward
    if (!data.lastReachPOI)
    {
        data.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(data.lastReachPOI) >= poiStayTime)
    {
        // e.g. Can not reward quest to gameobjects
        /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questAbandoned++;
        LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    return false;
}

bool NewRpgTravelFlightAction::Execute(Event /*event*/)
{
    if (botAI->IsResurrectionSicknessRecoveryActive())
        return false;

    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::TravelFlight>(&info.data);
    if (!dataPtr)
        return false;

    auto& data = *dataPtr;
    if (bot->IsInFlight())
    {
        data.inFlight = true;
        return false;
    }

    if (bot->GetDistance(data.flightMasterPos) > INTERACTION_DISTANCE)
        return MoveFarTo(data.flightMasterPos) != MoveFarOutcome::RouteFailed;

    Creature* flightMaster = bot->FindNearestCreature(data.flightMasterEntry, INTERACTION_DISTANCE * 3);
    if (!flightMaster || !flightMaster->IsAlive())
    {
        info.ChangeToIdle();
        return true;
    }
    if (bot->GetDistance(flightMaster) > INTERACTION_DISTANCE)
        return MoveFarTo(flightMaster) != MoveFarOutcome::RouteFailed;

    std::vector<uint32> nodes = data.path;

    botAI->RemoveShapeshift();
    if (bot->IsMounted())
        bot->Dismount();

    for (uint32 node : nodes)
    {
        if (!bot->m_taxi.IsTaximaskNodeKnown(node))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} rejected taxi route through unknown node {}", bot->GetName(), node);
            info.ChangeToIdle();
            return true;
        }
    }

    if (!bot->ActivateTaxiPathTo(nodes, flightMaster, 0))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} active taxi path {} (from {} to {}) failed", bot->GetName(),
                  flightMaster->GetEntry(), nodes[0], nodes[nodes.size() - 1]);
        info.ChangeToIdle();
        return true;
    }
    return true;
}

bool NewRpgTravelZoneAction::Execute(Event /*event*/)
{
    if (botAI->IsResurrectionSicknessRecoveryActive())
        return false;

    auto* data = std::get_if<NewRpgInfo::TravelZone>(&botAI->rpgInfo.data);
    if (!data)
        return false;

    // Same rule as NewRpgStatusUpdateAction: abandoning the trip on renewed progress only
    // makes sense for the no-progress fallback migration. A breadcrumb trip is heading for
    // a quest objective, and an advancement trip is leaving a zone the bot has outgrown, so
    // earning XP on the way is the point, not a reason to stop.
    bool progressChecked = false;
    if (!data->breadcrumb && !data->advancementBracketHigh && CheckProgress(progressChecked))
    {
        LOG_INFO("playerbots", "[New RPG] {} cancelled zone travel after renewed XP or quest progress",
                 bot->GetName());
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }

    if (bot->GetZoneId() == data->destinationZoneId && bot->GetExactDist(data->destination) < 80.0f)
    {
        uint32 resumeQuestId = data->resumeQuestId;
        NoteZoneArrival(data->destinationZoneId);
        if (resumeQuestId)
        {
            if (Quest const* quest = sObjectMgr->GetQuestTemplate(resumeQuestId))
                botAI->rpgInfo.ChangeToDoQuest(resumeQuestId, quest);
            else
                botAI->rpgInfo.ChangeToIdle();
        }
        else
            botAI->rpgInfo.ChangeToIdle();
        return true;
    }

    auto routeFailed = [&]() -> bool
    {
        data->retryCount++;
        if (data->retryCount <= 3)
        {
            RebuildZoneTravelRoute(*data);
            return true;
        }

        if (SelectAlternateZoneHub(*data))
            return true;

        FinishZoneTravelFailure();
        return true;
    };

    WorldPosition current(bot);
    if (!data->lastMeaningfulPosition)
    {
        data->lastMeaningfulPosition = current;
        data->lastMeaningfulMovementAt = getMSTime();
    }
    else if (current.GetMapId() != data->lastMeaningfulPosition.GetMapId() ||
             current.GetExactDist(data->lastMeaningfulPosition) >= 10.0f)
    {
        data->lastMeaningfulPosition = current;
        data->lastMeaningfulMovementAt = getMSTime();
    }
    else if (GetMSTimeDiffToNow(data->lastMeaningfulMovementAt) >= 90 * IN_MILLISECONDS)
    {
        return routeFailed();
    }

    if (data->route.empty())
    {
        if (RebuildZoneTravelRoute(*data))
            return true;
        return routeFailed();
    }

    if (data->routeStage >= data->route.size())
    {
        if (bot->GetZoneId() == data->destinationZoneId && bot->GetExactDist(data->destination) < 80.0f)
            return false;
        return routeFailed();
    }

    ZoneTravelStep const& step = data->route[data->routeStage];
    if (data->loggedRouteStage != data->routeStage)
    {
        data->loggedRouteStage = data->routeStage;
        LOG_INFO("playerbots", "[New RPG] {} travel method: {} (stage {}/{})", bot->GetName(),
                 ZoneTravelRoutePolicy::GetMethodName(step.method), data->routeStage + 1, data->route.size());
    }
    float arrivalDistance = step.method == ZoneTravelMethod::Walk ? 12.0f : 40.0f;
    if (current.GetMapId() == step.to.GetMapId() && current.GetExactDist(step.to) <= arrivalDistance)
    {
        data->routeStage++;
        data->lastMeaningfulPosition = current;
        data->lastMeaningfulMovementAt = getMSTime();
        return true;
    }

    if (step.method == ZoneTravelMethod::Walk)
    {
        MoveFarOutcome outcome = MoveFarTo(step.to);
        if (outcome == MoveFarOutcome::RouteFailed)
            return routeFailed();
        return true;
    }

    bool mustReachSource = step.method == ZoneTravelMethod::Transport ||
                           step.method == ZoneTravelMethod::KnownTaxi ||
                           step.method == ZoneTravelMethod::AreaTriggerPortal;
    float sourceDistance = step.method == ZoneTravelMethod::Transport ? 10.0f : INTERACTION_DISTANCE;
    if (mustReachSource &&
        (current.GetMapId() != step.from.GetMapId() || current.GetExactDist(step.from) > sourceDistance))
    {
        MoveFarOutcome outcome = MoveFarTo(step.from);
        if (outcome == MoveFarOutcome::RouteFailed)
            return routeFailed();
        return true;
    }

    ZoneTravelStepResult result = ZoneTravelRoutePolicy::ExecuteNonWalkStep(botAI, step);
    if (result == ZoneTravelStepResult::Failed)
        return routeFailed();
    if (result == ZoneTravelStepResult::Complete)
        data->routeStage++;
    return true;
}

bool NewRpgLeaveOpenWaterAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    if (!sPlayerbotAIConfig.openWaterRecovery || !botAI->IsAutonomousRandomBot() || !bot->IsAlive() ||
        bot->IsInFlight() || bot->InBattleground() || bot->GetTransport() ||
        !TravelMgr::IsOpenWater(WorldPosition(bot)))
    {
        info.openWaterSince = 0;
        info.openWaterRecovering = false;
        return false;
    }

    // Underwater objectives are legitimate quest work. A bot standing on its own objective is not
    // stranded, so leave it alone until it either finishes or gives the objective up.
    if (auto const* quest = std::get_if<NewRpgInfo::DoQuest>(&info.data))
    {
        if (quest->pos != WorldPosition() && quest->pos.GetMapId() == bot->GetMapId() &&
            bot->GetExactDist(quest->pos) < 250.0f)
        {
            info.openWaterSince = 0;
            info.openWaterRecovering = false;
            return false;
        }
    }

    if (!info.openWaterSince)
    {
        info.openWaterSince = getMSTime();
        info.openWaterAnchor = WorldPosition(bot);
        return false;
    }

    // A bot that is genuinely crossing water keeps making headway. Only water it cannot get out of
    // counts as stranding, so restart the clock while it is still covering ground.
    if (!info.openWaterRecovering && info.openWaterAnchor.GetMapId() == bot->GetMapId() &&
        bot->GetExactDist(info.openWaterAnchor) > 200.0f)
    {
        info.openWaterSince = getMSTime();
        info.openWaterAnchor = WorldPosition(bot);
        return false;
    }

    uint32 const strandedFor = GetMSTimeDiffToNow(info.openWaterSince);
    if (strandedFor < strandedGrace)
        return false;

    WorldPosition anchor = FindLandAnchor();
    if (anchor == WorldPosition())
    {
        LOG_WARN("playerbots", "[New RPG] {} is stranded in open water in zone {} with no known land anchor",
                 bot->GetName(), bot->GetZoneId());
        return false;
    }

    if (!info.openWaterRecovering)
    {
        info.openWaterRecovering = true;
        botAI->rpgStatistic.openWaterSwims++;
        LOG_INFO("playerbots",
                 "[New RPG] {} swimming out of open water in zone {} towards ({}, {:.1f}, {:.1f}, {:.1f})",
                 bot->GetName(), bot->GetZoneId(), anchor.GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(),
                 anchor.GetPositionZ());
    }

    // Whatever the bot planned from open water is unreachable from here, and that plan is what
    // keeps it swimming in place. Drop it so normal behaviour restarts once it reaches land.
    if (info.GetStatus() != RPG_IDLE)
    {
        info.SetMoveFarTo(WorldPosition());
        info.ChangeToIdle();
    }

    uint32 const rescueTimeout = sPlayerbotAIConfig.openWaterRescueSeconds * IN_MILLISECONDS;
    if (rescueTimeout && strandedFor >= rescueTimeout && TeleportToLand(anchor))
        return true;

    return SwimToward(anchor);
}

WorldPosition NewRpgLeaveOpenWaterAction::FindLandAnchor()
{
    WorldPosition current(bot);
    WorldPosition nearest;
    float nearestDistance = std::numeric_limits<float>::max();

    // Level hubs are innkeepers, flight masters and race start points: land positions by
    // construction, and faction safe for this bot.
    for (TravelMgr::ZoneHub const& hub : sTravelMgr.GetFactionCompatibleLevelHubs(bot))
    {
        if (hub.position.GetMapId() != current.GetMapId())
            continue;

        float const distance = current.fDist(hub.position);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = hub.position;
        }
    }

    if (nearest != WorldPosition())
        return nearest;

    // No hub for this level bracket on this map: the bind point is still a bot-specific inn or
    // race start position, so it remains a safe place to stand.
    if (bot->m_homebindMapId == current.GetMapId())
        return WorldPosition(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ);

    return WorldPosition();
}

bool NewRpgLeaveOpenWaterAction::SwimToward(WorldPosition const& anchor)
{
    // Deep water has no navigation mesh, so committed movement must be allowed to finish rather
    // than being recomputed into a lower priority action every tick.
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
        return true;

    Map* map = bot->GetMap();
    if (!map)
        return false;

    float const x = bot->GetPositionX();
    float const y = bot->GetPositionY();
    float const z = bot->GetPositionZ();
    float const baseAngle = bot->GetAngle(anchor.GetPositionX(), anchor.GetPositionY());

    // Straight towards the anchor first, then wider offsets so an island or a cliff on the direct
    // line does not pin the bot against it.
    static constexpr float angleOffsets[] = {0.0f, static_cast<float>(M_PI) / 6.0f, -static_cast<float>(M_PI) / 6.0f,
                                             static_cast<float>(M_PI) / 3.0f, -static_cast<float>(M_PI) / 3.0f};
    for (float offset : angleOffsets)
    {
        float const angle = baseAngle + offset;
        float dx = x + std::cos(angle) * swimStep;
        float dy = y + std::sin(angle) * swimStep;
        float dz = z;

        // Swim along the surface, and step up onto the shore as soon as there is ground above the
        // water line to stand on.
        float const ground = map->GetHeight(bot->GetPhaseMask(), dx, dy, z + 2.0f);
        float const water = map->GetWaterLevel(dx, dy);
        if (ground > INVALID_HEIGHT && ground > water)
            dz = ground + 0.5f;
        else if (water > INVALID_HEIGHT)
            dz = water;

        if (!map->CheckCollisionAndGetValidCoords(bot, x, y, z, dx, dy, dz))
            continue;

        if (MoveTo(bot->GetMapId(), dx, dy, dz, false, false, false, true))
            return true;
    }

    return false;
}

bool NewRpgLeaveOpenWaterAction::TeleportToLand(WorldPosition const& anchor)
{
    // Keep the rescue invisible to real players and never yank a bot out of a fight.
    if (bot->IsInCombat() || botAI->HasPlayerNearby(150.0f))
        return false;

    NewRpgInfo& info = botAI->rpgInfo;
    LOG_WARN("playerbots",
             "[New RPG] {} could not swim out of open water in zone {} for {} seconds and was returned to land at "
             "({}, {:.1f}, {:.1f}, {:.1f})",
             bot->GetName(), bot->GetZoneId(), GetMSTimeDiffToNow(info.openWaterSince) / IN_MILLISECONDS,
             anchor.GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(), anchor.GetPositionZ());

    bot->GetMotionMaster()->Clear();
    botAI->Reset(true);
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
    bot->TeleportTo(anchor.GetMapId(), anchor.GetPositionX(), anchor.GetPositionY(), anchor.GetPositionZ(),
                    bot->GetOrientation());

    info.openWaterSince = 0;
    info.openWaterRecovering = false;
    info.SetMoveFarTo(WorldPosition());
    info.ChangeToIdle();
    botAI->rpgStatistic.openWaterRescues++;
    return true;
}
