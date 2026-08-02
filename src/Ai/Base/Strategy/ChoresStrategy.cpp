/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ChoresStrategy.h"

#include "Playerbots.h"

void ChoresStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    constexpr float choreRelevance = 4.5f;

    triggers.push_back(new TriggerNode("vendor in range", {NextAction("sell chore", choreRelevance)}));
    triggers.push_back(new TriggerNode("class trainer in range", {NextAction("train chore", choreRelevance)}));
    triggers.push_back(new TriggerNode("repairer in range", {NextAction("repair", choreRelevance)}));
    triggers.push_back(new TriggerNode("banker in range", {NextAction("bank chore", choreRelevance)}));
    triggers.push_back(new TriggerNode("should buy bags at vendor", {NextAction("buy bags chore", choreRelevance)}));
}
