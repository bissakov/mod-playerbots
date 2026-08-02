/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ChoreTriggers.h"

#include "Playerbots.h"

bool VendorInRangeTrigger::IsActive()
{
    return AI_VALUE(bool, "should sell") && AI_VALUE(bool, "can sell") && AI_VALUE(ObjectGuid, "nearest vendor");
}

bool ClassTrainerInRangeTrigger::IsActive()
{
    return AI_VALUE(bool, "can train") && AI_VALUE(ObjectGuid, "nearest class trainer");
}

bool BankerInRangeTrigger::IsActive()
{
    return AI_VALUE(bool, "should bank") && AI_VALUE(ObjectGuid, "nearest banker");
}

bool ShouldBuyBagsAtVendorTrigger::IsActive()
{
    return AI_VALUE(bool, "should buy bags") && AI_VALUE(ObjectGuid, "nearest vendor");
}
