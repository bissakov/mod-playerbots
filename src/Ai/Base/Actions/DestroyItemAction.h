/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_DESTROYITEMACTION_H
#define PLAYERBOTS_DESTROYITEMACTION_H

#include "InventoryAction.h"
#include "ItemUsageValue.h"

class FindItemVisitor;
class Item;
class PlayerbotAI;

class DestroyItemAction : public InventoryAction
{
public:
    DestroyItemAction(PlayerbotAI* botAI, std::string const name = "destroy") : InventoryAction(botAI, name) {}

    bool Execute(Event event) override;
    void DestroyItem(Item* item);

protected:
    void DestroyItem(FindItemVisitor* visitor);
};

class SmartDestroyItemAction : public DestroyItemAction
{
public:
    SmartDestroyItemAction(PlayerbotAI* botAI) : DestroyItemAction(botAI, "smart destroy") {}

    bool Execute(Event event) override;
    bool isUseful() override;

    static std::vector<ItemUsage> GetEvictionOrder(PlayerbotAI* botAI);
    static Item* FindItemToEvict(PlayerbotAI* botAI, ItemUsage incomingUsage);
};

#endif
