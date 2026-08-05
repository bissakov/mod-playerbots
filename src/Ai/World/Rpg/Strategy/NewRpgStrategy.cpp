/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgStrategy.h"

NewRpgStrategy::NewRpgStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

std::vector<NextAction> NewRpgStrategy::getDefaultActions()
{
    // the releavance should be greater than grind
    return {
        NextAction("new rpg status update", 11.0f)
    };
}

void NewRpgStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "go grind status",
            {
                NextAction("new rpg go grind", 3.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "go camp status",
            {
                NextAction("new rpg go camp", 3.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "wander random status",
            {
                NextAction("new rpg wander random", 3.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "wander npc status",
            {
                NextAction("new rpg wander npc", 3.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "do errand status",
            {
                NextAction("new rpg do errand", 3.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "do quest status",
            {
                NextAction("new rpg do quest", 3.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "travel flight status",
            {
                NextAction("new rpg travel flight", 3.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "outdoor pvp status",
            {
                NextAction("new rpg outdoor pvp", 3.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "travel zone status",
            {
                NextAction("new rpg travel zone", 12.0f)
            }
        )
    );
    // Above every other rpg action: a bot floating in open water cannot reach any objective it
    // could plan there, so getting back to land has to come first.
    triggers.push_back(
        new TriggerNode(
            "stranded in open water",
            {
                NextAction("new rpg leave open water", 13.0f)
            }
        )
    );
}

void NewRpgStrategy::InitMultipliers(std::vector<Multiplier*>&)
{
}
