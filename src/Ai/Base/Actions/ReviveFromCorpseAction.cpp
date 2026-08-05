/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ReviveFromCorpseAction.h"

#include <limits>

#include "Corpse.h"
#include "Event.h"
#include "FleeManager.h"
#include "GameGraveyard.h"
#include "GameTime.h"
#include "MapMgr.h"
#include "ObjectDefines.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ServerFacade.h"

namespace
{
// A ghost that has spent this long failing to reach or activate a Spirit
// Healer is resurrected directly, so recovery always terminates.
constexpr int64 SPIRIT_HEALER_DEADLINE = 20 * MINUTE;
}  // namespace

bool ReviveFromCorpseAction::Execute(Event event)
{
    Player* groupLeader = botAI->GetGroupLeader();
    Corpse* corpse = bot->GetCorpse();

    // follow group Leader when group Leader revives
    WorldPacket& p = event.getPacket();
    if (!p.empty() && p.GetOpcode() == CMSG_RECLAIM_CORPSE && groupLeader && !corpse && bot->IsAlive())
    {
        if (ServerFacade::instance().IsDistanceLessThan(AI_VALUE2(float, "distance", "group leader"),
                                              sPlayerbotAIConfig.farDistance))
        {
            if (!botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT))
            {
                botAI->TellMasterNoFacing("Welcome back!");
                botAI->ChangeStrategy("+follow,-stay", BOT_STATE_NON_COMBAT);
                return true;
            }
        }
    }

    if (!corpse)
        return false;

    // The reclaim opcode is dropped without any reply until the reclaim delay has
    // passed, so attempting it earlier only interrupts the corpse run. Use the same
    // clock as the handler so both agree on when the delay is over.
    if (time_t(corpse->GetGhostTime() + bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP)) >
        time_t(GameTime::GetGameTime().count()))
        return false;

    if (groupLeader)
    {
        if (!GET_PLAYERBOT_AI(groupLeader) && groupLeader->isDead() && groupLeader->GetCorpse() &&
            ServerFacade::instance().IsDistanceLessThan(AI_VALUE2(float, "distance", "group leader"),
                                              sPlayerbotAIConfig.farDistance))
            return false;
    }

    WorldPacket packet(CMSG_RECLAIM_CORPSE);
    packet << bot->GetGUID();
    bot->GetSession()->HandleReclaimCorpseOpcode(packet);

    // The handler also drops a rejected request silently, so read the resurrection
    // state back instead of assuming the reclaim went through.
    if (!bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        LOG_DEBUG("playerbots", "Bot {} {}:{} <{}> was refused corpse reclaim", bot->GetGUID().ToString(),
                  bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
        return false;
    }

    LOG_DEBUG("playerbots", "Bot {} {}:{} <{}> revives at body", bot->GetGUID().ToString(),
              bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());

    bot->GetMotionMaster()->Clear();
    bot->StopMoving();

    return true;
}

bool FindCorpseAction::Execute(Event /*event*/)
{
    if (bot->InBattleground())
        return false;

    Player* groupLeader = botAI->GetGroupLeader();
    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
        return false;

    // if (groupLeader)
    // {
    //     if (!GET_PLAYERBOT_AI(groupLeader) &&
    //         ServerFacade::instance().IsDistanceLessThan(AI_VALUE2(float, "distance", "group leader"),
    //         sPlayerbotAIConfig.farDistance)) return false;
    // }

    uint32 dCount = AI_VALUE(uint32, "death count");

    // A death streak must not divert the ghost to the Spirit Healer: a player reclaims the
    // corpse unless the run is hopeless, and the run is already time-bounded below. Only
    // non-organic bots get the cheap teleport revive to limit skeleton piles.
    if (!botAI->HasRealPlayerMaster() && dCount >= 5 && !sPlayerbotAIConfig.organicProgression)
    {
        context->GetValue<uint32>("death count")->Set(0);
        sRandomPlayerbotMgr.Revive(bot);
        return true;
    }

    WorldPosition botPos(bot);
    WorldPosition corpsePos(corpse);
    WorldPosition moveToPos = corpsePos;
    WorldPosition leaderPos(groupLeader);

    float reclaimDist = CORPSE_RECLAIM_RADIUS - 5.0f;
    float corpseDist = botPos.distance(corpsePos);
    int64 deadTime = time(nullptr) - corpse->GetGhostTime();

    // A movement generator can remain active even when the ghost has made no
    // useful progress. Bound corpse runs and use the normal Spirit Healer path
    // instead of treating "is moving" as success forever.
    if (deadTime >= 10 * MINUTE)
    {
        bot->StopMoving();
        bot->GetMotionMaster()->Clear();
        return botAI->DoSpecificAction("spirit healer", Event(), true);
    }

    bool moveToLeader = groupLeader && groupLeader != bot && leaderPos.fDist(corpsePos) < reclaimDist;

    // Should we ressurect? If so, return false.
    if (corpseDist < reclaimDist)
    {
        if (moveToLeader)  // We are near group leader.
        {
            if (botPos.fDist(leaderPos) < sPlayerbotAIConfig.spellDistance)
                return false;
        }
        else if (deadTime > 8 * MINUTE)  // We have walked too long already.
            return false;
        else
        {
            GuidVector units = AI_VALUE(GuidVector, "possible targets no los");

            if (botPos.getUnitsAggro(units, bot) == 0)  // There are no mobs near.
                return false;
        }
    }

    // If we are getting close move to a save ressurrection spot instead of just the corpse.
    if (corpseDist < sPlayerbotAIConfig.reactDistance)
    {
        if (moveToLeader)
            moveToPos = leaderPos;
        else
        {
            FleeManager manager(bot, reclaimDist, 0.0, urand(0, 1), moveToPos);

            if (manager.isUseful())
            {
                float rx, ry, rz;
                if (manager.CalculateDestination(&rx, &ry, &rz))
                    moveToPos = WorldPosition(moveToPos.GetMapId(), rx, ry, rz, 0.0);
                else if (!moveToPos.GetReachableRandomPointOnGround(bot, reclaimDist, urand(0, 1)))
                    moveToPos = corpsePos;
            }
        }
    }

    // Actual mobing part.
    bool moved = false;

    if (!botAI->AllowActivity(ALL_ACTIVITY))
    {
        uint32 delay = ServerFacade::instance().GetDistance2d(bot, corpse) /
                       bot->GetSpeed(MOVE_RUN);        // Time a bot would take to travel to it's corpse.
        delay = std::min(delay, uint32(10 * MINUTE));  // Cap time to get to corpse at 10 minutes.

        if (deadTime > delay)
        {
            bot->GetMotionMaster()->Clear();
            bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
            bot->TeleportTo(moveToPos.GetMapId(), moveToPos.GetPositionX(), moveToPos.GetPositionY(), moveToPos.GetPositionZ(), 0);
        }

        moved = true;
    }
    else
    {
        if (bot->isMoving())
            moved = true;
        else
        {
            moved = MoveTo(moveToPos.GetMapId(), moveToPos.GetPositionX(), moveToPos.GetPositionY(),
                           moveToPos.GetPositionZ(), false, false);

            if (!moved)
            {
                moved = botAI->DoSpecificAction("spirit healer", Event(), true);
            }
        }
    }

    return moved;
}

bool FindCorpseAction::isUseful()
{
    if (bot->InBattleground())
        return false;

    return bot->GetCorpse();
}

GraveyardStruct const* SpiritHealerAction::GetGrave(bool startZone)
{
    GraveyardStruct const* ClosestGrave = nullptr;
    GraveyardStruct const* NewGrave = nullptr;

    ClosestGrave = sGraveyard->GetClosestGraveyard(bot, bot->GetTeamId());

    if (!startZone && ClosestGrave)
        return ClosestGrave;

    if (botAI->HasStrategy("follow", BOT_STATE_NON_COMBAT) && botAI->GetGroupLeader() && botAI->GetGroupLeader() != bot)
    {
        Player* groupLeader = botAI->GetGroupLeader();
        if (groupLeader && groupLeader != bot)
        {
            ClosestGrave = sGraveyard->GetClosestGraveyard(groupLeader, bot->GetTeamId());

            if (ClosestGrave)
                return ClosestGrave;
        }
    }
    else if (startZone && AI_VALUE(uint8, "durability"))
    {
        TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

        if (travelTarget->getPosition())
        {
            WorldPosition travelPos = *travelTarget->getPosition();
            if (travelPos.GetMapId() != uint32(-1))
            {
                uint32 areaId = 0;
                uint32 zoneId = 0;
                sMapMgr->GetZoneAndAreaId(bot->GetPhaseMask(), zoneId, areaId, travelPos.GetMapId(), travelPos.GetPositionX(),
                                          travelPos.GetPositionY(), travelPos.GetPositionZ());
                ClosestGrave = sGraveyard->GetClosestGraveyard(travelPos.GetMapId(), travelPos.GetPositionX(), travelPos.GetPositionY(),
                                                               travelPos.GetPositionZ(), bot->GetTeamId(), areaId, zoneId,
                                                               bot->getClass() == CLASS_DEATH_KNIGHT);

                if (ClosestGrave)
                    return ClosestGrave;
            }
        }
    }

    std::vector<uint32> races;

    if (bot->GetTeamId() == TEAM_ALLIANCE)
        races = {RACE_HUMAN, RACE_DWARF, RACE_GNOME, RACE_NIGHTELF, RACE_DRAENEI};
    else
        races = {RACE_ORC, RACE_TROLL, RACE_TAUREN, RACE_UNDEAD_PLAYER, RACE_BLOODELF};

    float graveDistance = -1;

    WorldPosition botPos(bot);

    for (auto race : races)
    {
        for (uint32 cls = 0; cls < MAX_CLASSES; cls++)
        {
            PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, cls);
            if (!info)
                continue;

            uint32 areaId = 0;
            uint32 zoneId = 0;
            sMapMgr->GetZoneAndAreaId(bot->GetPhaseMask(), zoneId, areaId, info->mapId, info->positionX,
                                      info->positionY, info->positionZ);

            NewGrave = sGraveyard->GetClosestGraveyard(info->mapId, info->positionX, info->positionY, info->positionZ,
                                                       bot->GetTeamId(), areaId, zoneId, cls == CLASS_DEATH_KNIGHT);
            if (!NewGrave)
                continue;

            WorldPosition gravePos(NewGrave->Map, NewGrave->x, NewGrave->y, NewGrave->z);

            float newDist = botPos.fDist(gravePos);

            if (graveDistance < 0 || newDist < graveDistance)
            {
                ClosestGrave = NewGrave;
                graveDistance = newDist;
            }
        }
    }

    return ClosestGrave;
}

bool SpiritHealerAction::Execute(Event /*event*/)
{
    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        // A ghost whose corpse is not on the current map, because it expired or
        // because a graveyard teleport crossed maps, can never reclaim it and
        // has no path back to a Spirit Healer either.
        if (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) && ResurrectAtGraveyard("its corpse is gone"))
            return true;

        botAI->TellError("I am not a spirit");
        return false;
    }

    uint32 dCount = AI_VALUE(uint32, "death count");
    int64 deadTime = time(nullptr) - corpse->GetGhostTime();

    if (deadTime >= SPIRIT_HEALER_DEADLINE && ResurrectAtGraveyard("no Spirit Healer could be reached"))
        return true;

    GraveyardStruct const* ClosestGrave =
        GetGrave(dCount > 10 || deadTime > 15 * MINUTE || AI_VALUE(uint8, "durability") < 10);
    if (!ClosestGrave)
        return false;

    // A graveyard on another map is not "nearby" no matter what its coordinates
    // compare to, so never let a cross-map grave pass the proximity checks.
    float graveDistance = ClosestGrave->Map == bot->GetMapId() ? bot->GetDistance2d(ClosestGrave->x, ClosestGrave->y)
                                                               : std::numeric_limits<float>::max();
    if (deadTime >= 10 * MINUTE && graveDistance >= sPlayerbotAIConfig.sightDistance)
    {
        bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
        return bot->TeleportTo(ClosestGrave->Map, ClosestGrave->x, ClosestGrave->y, ClosestGrave->z, 0.f);
    }

    if (graveDistance < sPlayerbotAIConfig.sightDistance)
    {
        if (Unit* healer = FindSpiritHealer())
        {
            // The activation opcode is silently dropped unless the healer is
            // within the same interaction range a player needs, so close the
            // remaining distance instead of talking to it from across the
            // graveyard.
            if (healer->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
                return ActivateSpiritHealer(healer);

            if (bot->isMoving() || MoveNear(healer, sPlayerbotAIConfig.contactDistance))
                return true;

            // Walking the last few yards has been failing long enough that the
            // ghost has to be put next to the healer instead.
            if (deadTime >= 15 * MINUTE)
            {
                bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
                return bot->TeleportTo(healer->GetMapId(), healer->GetPositionX(), healer->GetPositionY(),
                                       healer->GetPositionZ(), 0.f);
            }

            return false;
        }
    }

    bool moved = false;

    if (bot->IsWithinLOS(ClosestGrave->x, ClosestGrave->y, ClosestGrave->z))
        moved = MoveNear(ClosestGrave->Map, ClosestGrave->x, ClosestGrave->y, ClosestGrave->z, 0.0);
    else
        moved = MoveTo(ClosestGrave->Map, ClosestGrave->x, ClosestGrave->y, ClosestGrave->z, false, false);

    if (moved)
        return true;

    // if (!botAI->HasActivePlayerMaster())
    // {
    context->GetValue<uint32>("death count")->Set(dCount + 1);
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
    return bot->TeleportTo(ClosestGrave->Map, ClosestGrave->x, ClosestGrave->y, ClosestGrave->z, 0.f);
    // }

    // LOG_INFO("playerbots", "Bot {} {}:{} <{}> can't find a spirit healer", bot->GetGUID().ToString().c_str(),
    //          bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName().c_str());

    // botAI->TellError("Cannot find any spirit healer nearby");
    return false;
}

Unit* SpiritHealerAction::FindSpiritHealer()
{
    Unit* closest = nullptr;
    float closestDistance = 0.f;

    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    for (ObjectGuid const& guid : npcs)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->HasNpcFlag(UNIT_NPC_FLAG_SPIRITHEALER))
            continue;

        // Interaction is refused for anything the bot is unfriendly with, so
        // the other faction's healer must not become the target the bot waits
        // in front of forever.
        if (unit->GetReactionTo(bot) <= REP_UNFRIENDLY)
            continue;

        float distance = bot->GetDistance(unit);
        if (!closest || distance < closestDistance)
        {
            closest = unit;
            closestDistance = distance;
        }
    }

    return closest;
}

bool SpiritHealerAction::ActivateSpiritHealer(Unit* healer)
{
    // Use the same opcode as a player speaking to the spirit healer. The normal
    // handler applies durability loss and resurrection sickness; calling
    // ResurrectPlayer directly bypasses both.
    WorldPacket packet(CMSG_SPIRIT_HEALER_ACTIVATE);
    packet << healer->GetGUID();
    bot->GetSession()->HandleSpiritHealerActivateOpcode(packet);

    // The handler drops the request without any reply when it rejects the
    // healer, so verify the explicit resurrection state rather than inferring
    // success from the ghost's nonzero health.
    if (!bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        LOG_DEBUG("playerbots", "Bot {} {}:{} <{}> was refused by the spirit healer", bot->GetGUID().ToString(),
                  bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
        return false;
    }

    LOG_DEBUG("playerbots", "Bot {} {}:{} <{}> revives at spirit healer", bot->GetGUID().ToString(),
              bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());

    FinishResurrection();
    return true;
}

bool SpiritHealerAction::ResurrectAtGraveyard(std::string const& reason)
{
    // Battlegrounds resurrect their ghosts on the spirit healer's own timer.
    if (bot->InBattleground())
        return false;

    // Same lifecycle as talking to a spirit healer: resurrection sickness,
    // durability loss, corpse bones and the graveyard teleport.
    bot->GetSession()->SendSpiritResurrect();

    if (!bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        LOG_ERROR("playerbots", "Bot {} {}:{} <{}> stays a ghost after a forced spirit healer resurrection",
                  bot->GetGUID().ToString(), bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(),
                  bot->GetName());
        return false;
    }

    LOG_INFO("playerbots", "Bot {} {}:{} <{}> revives without a spirit healer: {}", bot->GetGUID().ToString(),
             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(), reason);

    FinishResurrection();
    return true;
}

void SpiritHealerAction::FinishResurrection()
{
    context->GetValue<Unit*>("current target")->Set(nullptr);
    bot->SetTarget();
    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault("hello", "Hello", {}));

    if (AI_VALUE(uint32, "death count") > 20)
        context->GetValue<uint32>("death count")->Set(0);
}

bool SpiritHealerAction::isUseful() { return bot->HasPlayerFlag(PLAYER_FLAGS_GHOST); }
