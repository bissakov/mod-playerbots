/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CHOREVALUES_H
#define PLAYERBOTS_CHOREVALUES_H

#include "Value.h"

class PlayerbotAI;

class NearestServiceNpcValue : public ObjectGuidCalculatedValue
{
public:
    NearestServiceNpcValue(PlayerbotAI* botAI, std::string const name, NPCFlags npcFlag)
        : ObjectGuidCalculatedValue(botAI, name, 2), npcFlag(npcFlag)
    {
    }

    ObjectGuid Calculate() override;

protected:
    virtual bool IsSuitable(Creature* creature) const;

    NPCFlags npcFlag;
};

class NearestVendorValue : public NearestServiceNpcValue
{
public:
    NearestVendorValue(PlayerbotAI* botAI) : NearestServiceNpcValue(botAI, "nearest vendor", UNIT_NPC_FLAG_VENDOR) {}
};

class NearestClassTrainerValue : public NearestServiceNpcValue
{
public:
    NearestClassTrainerValue(PlayerbotAI* botAI)
        : NearestServiceNpcValue(botAI, "nearest class trainer", UNIT_NPC_FLAG_TRAINER_CLASS)
    {
    }

protected:
    bool IsSuitable(Creature* creature) const override;
};

class NearestBankerValue : public NearestServiceNpcValue
{
public:
    NearestBankerValue(PlayerbotAI* botAI) : NearestServiceNpcValue(botAI, "nearest banker", UNIT_NPC_FLAG_BANKER) {}
};

#endif
