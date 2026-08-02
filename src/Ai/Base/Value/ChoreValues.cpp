/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ChoreValues.h"

#include "Playerbots.h"
#include "Trainer.h"

ObjectGuid NearestServiceNpcValue::Calculate()
{
    GuidVector const candidates = AI_VALUE(GuidVector, "possible new rpg targets");
    for (ObjectGuid const& guid : candidates)
    {
        Creature* creature = bot->GetNPCIfCanInteractWith(guid, npcFlag);
        if (creature && IsSuitable(creature))
            return guid;
    }

    return ObjectGuid::Empty;
}

bool NearestServiceNpcValue::IsSuitable(Creature* /*creature*/) const { return true; }

bool NearestClassTrainerValue::IsSuitable(Creature* creature) const
{
    Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(creature->GetEntry());
    return trainer && trainer->GetTrainerType() == Trainer::Type::Class && trainer->IsTrainerValidForPlayer(bot);
}
