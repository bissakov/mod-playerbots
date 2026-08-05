/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NEWRPGTRIGGERS_H
#define PLAYERBOTS_NEWRPGTRIGGERS_H

#include "NewRpgStrategy.h"
#include "Trigger.h"

class NewRpgStatusTrigger : public Trigger
{
public:
    NewRpgStatusTrigger(PlayerbotAI* botAI, NewRpgStatus status = RPG_IDLE)
        : Trigger(botAI, "new rpg status"), status(status)
    {
    }
    bool IsActive() override;

protected:
    NewRpgStatus status;
};

/// Cheap liquid gate for the open water recovery, using the terrain status the bot already caches.
/// The action behind it confirms the actual water depth, so this must stay free of fresh terrain
/// queries: it runs for every bot on every tick.
class StrandedInOpenWaterTrigger : public Trigger
{
public:
    StrandedInOpenWaterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "stranded in open water") {}
    bool IsActive() override;
};

#endif
