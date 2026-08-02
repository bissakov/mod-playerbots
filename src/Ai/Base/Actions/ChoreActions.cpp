/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ChoreActions.h"

#include "ItemUsageValue.h"
#include "Playerbots.h"

bool SellChoreAction::Execute(Event /*event*/)
{
    bool sold = SellAction::Execute(Event("chores", "gray"));
    sold |= SellAction::Execute(Event("chores", "vendor"));
    return sold;
}

bool TrainChoreAction::Execute(Event /*event*/) { return TrainerAction::Execute(Event("chores", "learn")); }

bool TrainChoreAction::isUseful() { return AI_VALUE(uint32, "train cost") > 0 && TrainerAction::isUseful(); }

Unit* TrainChoreAction::GetTarget() { return botAI->GetUnit(AI_VALUE(ObjectGuid, "nearest class trainer")); }

bool BankChoreAction::Execute(Event /*event*/)
{
    bool deposited = false;
    for (ItemUsage usage : {ITEM_USAGE_KEEP, ITEM_USAGE_AH, ITEM_USAGE_SKILL})
        deposited |= BankAction::Execute(Event("chores", "usage " + std::to_string(usage)));

    return deposited;
}

bool BuyBagsChoreAction::Execute(Event /*event*/) { return BuyAction::Execute(Event("chores", "bags")); }
