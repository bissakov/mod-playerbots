/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgTriggers.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"

bool NewRpgStatusTrigger::IsActive() { return status == botAI->rpgInfo.GetStatus(); }

bool StrandedInOpenWaterTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.openWaterRecovery || !botAI->IsAutonomousRandomBot())
        return false;

    if (!bot->IsAlive() || bot->IsInFlight() || bot->InBattleground() || bot->GetTransport())
        return false;

    // Cached terrain status: recomputed only when the bot moves, so a bot floating in place costs
    // nothing here. Movement flags are not usable as the gate because a bot that stopped moving
    // keeps whatever swim flag it last had.
    return (bot->GetLiquidData().Status & (LIQUID_MAP_WATER_WALK | LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)) != 0;
}
