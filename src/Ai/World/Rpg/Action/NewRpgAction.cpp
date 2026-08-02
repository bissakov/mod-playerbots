/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgAction.h"

#include <cmath>
#include <cstdlib>

#include "AreaDefines.h"
#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "G3D/Vector2.h"
#include "GossipDef.h"
#include "IVMapMgr.h"
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

    if (AI_VALUE(bool, "should buy bags") && npc->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
    {
        uint32 smallestBagSize = UINT32_MAX;
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            Bag const* equippedBag = static_cast<Bag const*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
            smallestBagSize = std::min(smallestBagSize, equippedBag ? equippedBag->GetBagSize() : 0u);
        }

        if (VendorItemData const* items = npc->GetVendorItems())
        {
            for (VendorItem const* item : items->m_items)
            {
                ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(item->item);
                if (itemTemplate && itemTemplate->Class == ITEM_CLASS_CONTAINER &&
                    itemTemplate->SubClass == ITEM_SUBCLASS_CONTAINER && itemTemplate->ContainerSlots > smallestBagSize)
                    return true;
            }
        }
    }

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

    bool progressChecked = false;
    bool madeProgress = CheckProgress(progressChecked);

    // Only the no-progress fallback migration should be abandoned once the bot starts
    // progressing again: it exists purely to unstick a bot that had stopped advancing.
    // A breadcrumb migration is walking towards a quest objective in another zone, and
    // killing things on the way is the expected behaviour, not a reason to turn back.
    // Cancelling those meant almost every trip aborted within seconds of starting.
    bool cancellableTravel = false;
    if (status == RPG_TRAVEL_ZONE)
    {
        auto const& travelData = std::get<NewRpgInfo::TravelZone>(info.data);
        cancellableTravel = !travelData.breadcrumb;
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
            if (FindCrossZoneBreadcrumb(breadcrumbDestination, breadcrumbQuest))
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
                info.lastProgressAt = getMSTime();
                botAI->rpgStatistic.zoneArrivals++;
                LOG_INFO("playerbots", "[New RPG] {} arrived in zone {}", bot->GetName(), data.destinationZoneId);
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
    if (SearchQuestGiverAndAcceptOrReward())
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
    if (SearchQuestGiverAndAcceptOrReward())
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
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveRandomNear();
}

bool NewRpgWanderNpcAction::Execute(Event /*event*/)
{
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
        uint32 rndIdx = urand(0, poiInfo.size() - 1);
        int32 objectiveIdx = poiInfo[rndIdx].objectiveIdx;
        data.lastReachPOI = 0;
        data.pos = poiInfo[rndIdx].pos;
        data.objectiveIdx = objectiveIdx;

        if (data.pos.GetMapId() != bot->GetMapId() || GetPositionZoneId(data.pos) != bot->GetZoneId())
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

        if (data.pos.GetMapId() != bot->GetMapId() || GetPositionZoneId(data.pos) != bot->GetZoneId())
        {
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
    auto* data = std::get_if<NewRpgInfo::TravelZone>(&botAI->rpgInfo.data);
    if (!data)
        return false;

    // Same rule as NewRpgStatusUpdateAction: abandoning the trip on renewed progress only
    // makes sense for the no-progress fallback migration. A breadcrumb trip is heading for
    // a quest objective, so earning XP on the way is the point, not a reason to stop.
    bool progressChecked = false;
    if (!data->breadcrumb && CheckProgress(progressChecked))
    {
        LOG_INFO("playerbots", "[New RPG] {} cancelled zone travel after renewed XP or quest progress",
                 bot->GetName());
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }

    if (bot->GetZoneId() == data->destinationZoneId && bot->GetExactDist(data->destination) < 80.0f)
    {
        uint32 resumeQuestId = data->resumeQuestId;
        botAI->rpgInfo.lastProgressAt = getMSTime();
        botAI->rpgStatistic.zoneArrivals++;
        LOG_INFO("playerbots", "[New RPG] {} arrived in zone {}", bot->GetName(), data->destinationZoneId);
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
