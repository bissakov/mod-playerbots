/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ChoreTriggers.h"

#include "MaintenanceValues.h"
#include "Playerbots.h"

bool VendorInRangeTrigger::IsActive()
{
    return AI_VALUE(bool, "should sell") && AI_VALUE(bool, "can sell") && AI_VALUE(ObjectGuid, "nearest vendor");
}

bool ClassTrainerInRangeTrigger::IsActive()
{
    return AI_VALUE(bool, "can train") && AI_VALUE(ObjectGuid, "nearest class trainer");
}

bool RepairerInRangeTrigger::IsActive()
{
    if (!AI_VALUE(bool, "should repair") || !AI_VALUE(bool, "can repair"))
        return false;

    for (ObjectGuid const& guid : AI_VALUE(GuidVector, "possible new rpg targets"))
    {
        if (bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_REPAIR))
            return true;
    }

    return false;
}

bool BankerInRangeTrigger::IsActive()
{
    return AI_VALUE(bool, "should bank") && AI_VALUE(ObjectGuid, "nearest banker");
}

bool ShouldBuyBagsAtVendorTrigger::IsActive()
{
    if (!AI_VALUE(bool, "should buy bags"))
        return false;

    Unit* unit = botAI->GetUnit(AI_VALUE(ObjectGuid, "nearest vendor"));
    return CanBuyBagUpgradeAt(botAI, unit ? unit->ToCreature() : nullptr);
}
