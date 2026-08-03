/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MaintenanceValues.h"

#include <cmath>

#include "BudgetValues.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"

bool CanBuyBagUpgradeAt(PlayerbotAI* botAI, Creature* vendor)
{
    if (!vendor || !vendor->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
        return false;

    Player* bot = botAI->GetBot();
    uint32 smallestBagSize = UINT32_MAX;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag const* equippedBag = static_cast<Bag const*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
        smallestBagSize = std::min(smallestBagSize, equippedBag ? equippedBag->GetBagSize() : 0u);
    }

    VendorItemData const* items = vendor->GetVendorItems();
    if (!items)
        return false;

    uint32 availableMoney =
        botAI->GetAiObjectContext()
            ->GetValue<uint32>("free money for", std::to_string(static_cast<uint32>(NeedMoneyFor::gear)))
            ->Get();
    float discount = bot->GetReputationPriceDiscount(vendor);
    for (VendorItem const* item : items->m_items)
    {
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(item->item);
        if (!itemTemplate || itemTemplate->Class != ITEM_CLASS_CONTAINER ||
            itemTemplate->SubClass != ITEM_SUBCLASS_CONTAINER || itemTemplate->ContainerSlots <= smallestBagSize)
            continue;

        uint32 price = static_cast<uint32>(std::floor(itemTemplate->BuyPrice * discount));
        if (price <= availableMoney)
            return true;
    }

    return false;
}

bool CanMoveAroundValue::Calculate()
{
    if (bot->IsInCombat())
        return false;

    if (bot->GetTradeData())
        return false;

    if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT))
        return false;

    if (!AI_VALUE(bool, "group ready"))
        return false;

    return true;
}

bool ShouldHomeBindValue::Calculate() { return AI_VALUE2(float, "distance", "home bind") > 1000.0f; }

bool ShouldRepairValue::Calculate() { return AI_VALUE(uint8, "durability") < 80; }

bool CanRepairValue::Calculate()
{
    return AI_VALUE(uint8, "durability") < 100 &&
           AI_VALUE(uint32, "repair cost") < AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::repair);
}

bool ShouldSellValue::Calculate() { return AI_VALUE(uint8, "bag space") > 80; }

bool CanSellValue::Calculate()
{
    return (AI_VALUE2(uint32, "item count", "usage " + std::to_string(ITEM_USAGE_VENDOR)) +
            AI_VALUE2(uint32, "item count", "usage " + std::to_string(ITEM_USAGE_AH))) > 1;
}

bool CanTrainValue::Calculate()
{
    uint32 trainCost = AI_VALUE(uint32, "train cost");
    return trainCost > 0 && AI_VALUE2(uint32, "free money for", static_cast<uint32>(NeedMoneyFor::spells)) >= trainCost;
}

bool ShouldBankValue::Calculate()
{
    // At desperate bag pressure these items have to remain available to the eviction path instead of making
    // banking a reason to hold on to them indefinitely.
    if (AI_VALUE(uint8, "bag space") >= 90)
        return false;

    for (ItemUsage usage : {ITEM_USAGE_KEEP, ITEM_USAGE_AH, ITEM_USAGE_SKILL})
    {
        if (AI_VALUE2(uint32, "item count", "usage " + std::to_string(usage)) > 0)
            return true;
    }

    return false;
}

bool ShouldBuyBagsValue::Calculate()
{
    if (AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::gear) == 0)
        return false;

    constexpr uint32 desiredBagSize = 14;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag const* equippedBag = static_cast<Bag const*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
        if (!equippedBag || equippedBag->GetBagSize() < desiredBagSize)
            return true;
    }

    return false;
}

bool CanFightEqualValue::Calculate() { return AI_VALUE(uint8, "durability") > 20; }

bool CanFightEliteValue::Calculate()
{
    return bot->GetGroup() && AI_VALUE2(bool, "group and", "can fight equal") &&
           AI_VALUE2(bool, "group and", "following party") && !AI_VALUE2(bool, "group or", "should sell,can sell");
}

bool CanFightBossValue::Calculate()
{
    return bot->GetGroup() && bot->GetGroup()->GetMembersCount() > 3 &&
           AI_VALUE2(bool, "group and", "can fight equal") && AI_VALUE2(bool, "group and", "following party") &&
           !AI_VALUE2(bool, "group or", "should sell,can sell");
}
