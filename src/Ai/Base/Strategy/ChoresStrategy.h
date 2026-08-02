/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CHORESSTRATEGY_H
#define PLAYERBOTS_CHORESSTRATEGY_H

#include "NonCombatStrategy.h"

class PlayerbotAI;

class ChoresStrategy : public NonCombatStrategy
{
public:
    ChoresStrategy(PlayerbotAI* botAI) : NonCombatStrategy(botAI) {}

    std::string const getName() override { return "chores"; }
    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }
    void InitTriggers(std::vector<TriggerNode*>& triggers) override;
};

#endif
