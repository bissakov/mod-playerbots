/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CHORETRIGGERS_H
#define PLAYERBOTS_CHORETRIGGERS_H

#include "Trigger.h"

class PlayerbotAI;

class VendorInRangeTrigger : public Trigger
{
public:
    VendorInRangeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "vendor in range", 2) {}

    bool IsActive() override;
};

class ClassTrainerInRangeTrigger : public Trigger
{
public:
    ClassTrainerInRangeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "class trainer in range", 2) {}

    bool IsActive() override;
};

class RepairerInRangeTrigger : public Trigger
{
public:
    RepairerInRangeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "repairer in range", 2) {}

    bool IsActive() override;
};

class BankerInRangeTrigger : public Trigger
{
public:
    BankerInRangeTrigger(PlayerbotAI* botAI) : Trigger(botAI, "banker in range", 2) {}

    bool IsActive() override;
};

class ShouldBuyBagsAtVendorTrigger : public Trigger
{
public:
    ShouldBuyBagsAtVendorTrigger(PlayerbotAI* botAI) : Trigger(botAI, "should buy bags at vendor", 2) {}

    bool IsActive() override;
};

#endif
