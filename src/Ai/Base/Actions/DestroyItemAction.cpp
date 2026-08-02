/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "DestroyItemAction.h"

#include "Event.h"
#include "ItemCountValue.h"
#include "Playerbots.h"

bool DestroyItemAction::Execute(Event event)
{
    std::string const text = event.getParam();
    ItemIds ids = chat->parseItems(text);

    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        DestroyItem(&visitor);
    }

    return true;
}

void DestroyItemAction::DestroyItem(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    std::vector<Item*> items = visitor->GetResult();
    for (Item* item : items)
        DestroyItem(item);
}

void DestroyItemAction::DestroyItem(Item* item)
{
    std::ostringstream out;
    out << chat->FormatItem(item->GetTemplate()) << " destroyed";
    botAI->TellMaster(out);

    bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
}

bool SmartDestroyItemAction::isUseful() { return !botAI->HasActivePlayerMaster(); }

std::vector<ItemUsage> SmartDestroyItemAction::GetEvictionOrder(PlayerbotAI* botAI)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    std::vector<ItemUsage> order = {ITEM_USAGE_NONE};

    if (!context->GetValue<bool>("can sell")->Get() && context->GetValue<bool>("should get money")->Get())
        order.push_back(ITEM_USAGE_QUEST);
    else
    {
        order.push_back(ITEM_USAGE_VENDOR);
        order.push_back(ITEM_USAGE_AH);
    }

    order.push_back(ITEM_USAGE_SKILL);
    order.push_back(ITEM_USAGE_USE);
    return order;
}

Item* SmartDestroyItemAction::FindItemToEvict(PlayerbotAI* botAI, ItemUsage incomingUsage)
{
    if (incomingUsage == ITEM_USAGE_NONE)
        return nullptr;

    AiObjectContext* context = botAI->GetAiObjectContext();
    std::vector<ItemUsage> const order = GetEvictionOrder(botAI);
    auto const incomingPosition = std::find(order.begin(), order.end(), incomingUsage);

    for (auto carriedPosition = order.begin(); carriedPosition != order.end(); ++carriedPosition)
    {
        std::vector<Item*> items = context->GetValue<std::vector<Item*>>(
            "inventory items", "usage " + std::to_string(*carriedPosition))->Get();
        if (items.empty())
            continue;

        // Vendor trash may replace useless items, but never protected quest or equipment-related items.
        if (incomingUsage == ITEM_USAGE_VENDOR && *carriedPosition != ITEM_USAGE_NONE)
            return nullptr;

        if (incomingPosition != order.end() && incomingPosition <= carriedPosition)
            return nullptr;

        // SmartDestroyItemAction reverses this value before eviction, so its last item is the first candidate.
        return items.back();
    }

    return nullptr;
}

bool SmartDestroyItemAction::Execute(Event /*event*/)
{
    uint8 bagSpace = AI_VALUE(uint8, "bag space");

    if (bagSpace < 90)
        return false;

    // only destoy grey items if with real player/guild
    if (botAI->HasRealPlayerMaster() && botAI->IsInRealGuild())
    {
        std::set<Item*> items;
        FindItemsToTradeByQualityVisitor visitor(ITEM_QUALITY_POOR, 5);
        IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);
        items.insert(visitor.GetResult().begin(), visitor.GetResult().end());

        for (auto& item : items)
        {
            FindItemByIdVisitor visitor(item->GetTemplate()->ItemId);
            DestroyItem(&visitor);

            bagSpace = AI_VALUE(uint8, "bag space");

            if (bagSpace < 90)
                return true;
        }
        return true;
    }

    for (ItemUsage usage : GetEvictionOrder(botAI))
    {
        std::vector<Item*> items = AI_VALUE2(std::vector<Item*>, "inventory items", "usage " + std::to_string(usage));
        std::reverse(items.begin(), items.end());

        for (auto& item : items)
        {
            FindItemByIdVisitor visitor(item->GetTemplate()->ItemId);
            DestroyItem(&visitor);

            bagSpace = AI_VALUE(uint8, "bag space");

            if (bagSpace < 90)
                return true;
        }
    }

    return false;
}
