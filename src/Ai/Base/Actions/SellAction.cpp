/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SellAction.h"

#include "Event.h"
#include "ItemPackets.h"
#include "ItemUsageValue.h"
#include "ItemVisitors.h"
#include "Playerbots.h"

class SellItemsVisitor : public IterateItemsVisitor
{
public:
    SellItemsVisitor(SellAction* action) : IterateItemsVisitor(), action(action) {}

    bool Visit(Item* item) override
    {
        action->Sell(item);
        return true;
    }

private:
    SellAction* action;
};

class SellGrayItemsVisitor : public SellItemsVisitor
{
public:
    SellGrayItemsVisitor(SellAction* action) : SellItemsVisitor(action) {}

    bool Visit(Item* item) override
    {
        if (item->GetTemplate()->Quality != ITEM_QUALITY_POOR)
            return true;

        return SellItemsVisitor::Visit(item);
    }
};

class SellVendorItemsVisitor : public SellItemsVisitor
{
public:
    SellVendorItemsVisitor(SellAction* action, AiObjectContext* con) : SellItemsVisitor(action) { context = con; }

    AiObjectContext* context;

    bool Visit(Item* item) override
    {
        ItemUsage usage = context->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
        if (usage != ITEM_USAGE_VENDOR && usage != ITEM_USAGE_AH)
            return true;

        return SellItemsVisitor::Visit(item);
    }
};

bool SellAction::Execute(Event event)
{
    if (!FindVendor())
        return false;

    std::string const text = event.getParam();
    if (text == "gray" || text == "*")
    {
        SellGrayItemsVisitor visitor(this);
        IterateItems(&visitor);
        return true;
    }

    if (text == "vendor")
    {
        SellVendorItemsVisitor visitor(this, context);
        IterateItems(&visitor);
        return true;
    }

    if (text != "")
    {
        std::vector<Item*> items = parseItems(text, ITERATE_ITEMS_IN_BAGS);
        for (Item* item : items)
        {
            Sell(item);
        }
        return true;
    }

    botAI->TellError("Usage: s gray/*/vendor/[item link]");
    return false;
}

void SellAction::Sell(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    std::vector<Item*> items = visitor->GetResult();
    for (Item* item : items)
    {
        Sell(item);
    }
}

void SellAction::Sell(Item* item)
{
    if (!bot->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_VENDOR))
        return;

    ItemTemplate const* itemTemplate = item->GetTemplate();
    ObjectGuid itemGuid = item->GetGUID();
    uint32 count = item->GetCount();
    uint32 botMoney = bot->GetMoney();

    WorldPacket packet(CMSG_SELL_ITEM);
    packet << vendorGuid << itemGuid << count;

    WorldPackets::Item::SellItem sellPacket(std::move(packet));
    sellPacket.Read();
    bot->GetSession()->HandleSellItemOpcode(sellPacket);

    if (botAI->HasCheat(BotCheatMask::gold))
        bot->SetMoney(botMoney);

    std::ostringstream out;
    out << "Selling " << chat->FormatItem(itemTemplate);
    botAI->TellMaster(out);

    bot->PlayDistanceSound(120);
}

bool SellAction::FindVendor()
{
    vendorGuid = ObjectGuid::Empty;
    GuidVector const vendors = AI_VALUE(GuidVector, "nearest npcs");
    for (ObjectGuid const& guid : vendors)
    {
        if (!bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_VENDOR))
            continue;

        vendorGuid = guid;
        return true;
    }

    return false;
}
