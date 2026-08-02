#include "ZoneTravelPolicy.h"

#include <mutex>

#include "DBCStores.h"
#include "Event.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Transport.h"
#include "TravelNode.h"

namespace
{
std::mutex routeBuildMutex;

bool IsValidDestination(WorldPosition const& destination)
{
    return sMapStore.LookupEntry(destination.GetMapId()) != nullptr;
}

bool SameArrival(WorldPosition const& left, WorldPosition const& right, float tolerance = 100.0f)
{
    return left.GetMapId() == right.GetMapId() && left.GetExactDist(right) <= tolerance;
}

bool HasReagents(Player* bot, SpellInfo const* spellInfo)
{
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spellInfo->Reagent[i] > 0 &&
            !bot->HasItemCount(spellInfo->Reagent[i], std::max<uint32>(1, spellInfo->ReagentCount[i])))
            return false;
    }
    return true;
}

bool GetTeleportDestination(Player* bot, uint32 spellId, WorldPosition& destination)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo || !spellInfo->HasEffect(SPELL_EFFECT_TELEPORT_UNITS))
        return false;

    if (spellId == 8690)
    {
        destination = WorldPosition(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ);
        return IsValidDestination(destination);
    }

    for (uint8 effect = 0; effect < MAX_SPELL_EFFECTS; ++effect)
    {
        if (SpellTargetPosition const* target =
                sSpellMgr->GetSpellTargetPosition(spellId, static_cast<SpellEffIndex>(effect)))
        {
            destination = WorldPosition(target->target_mapId, target->target_X, target->target_Y, target->target_Z,
                                        target->target_Orientation);
            return IsValidDestination(destination);
        }
    }
    return false;
}

Item* FindUsableItem(PlayerbotAI* botAI, uint32 spellId, ObjectGuid preferred = ObjectGuid::Empty)
{
    Player* bot = botAI->GetBot();
    if (preferred)
    {
        Item* item = bot->GetItemByGuid(preferred);
        if (item && bot->CanUseItem(item) == EQUIP_ERR_OK)
            return item;
    }

    for (Item* item : botAI->GetInventoryAndEquippedItems())
    {
        if (!item || bot->CanUseItem(item) != EQUIP_ERR_OK)
            continue;

        ItemTemplate const* itemTemplate = item->GetTemplate();
        for (uint8 slot = 0; slot < MAX_ITEM_PROTO_SPELLS; ++slot)
        {
            if (itemTemplate->Spells[slot].SpellId <= 0 ||
                static_cast<uint32>(itemTemplate->Spells[slot].SpellId) != spellId)
                continue;
            if (itemTemplate->Spells[slot].SpellCharges && !item->GetSpellCharges(slot))
                continue;
            return item;
        }
    }
    return nullptr;
}

bool CanUseSpell(PlayerbotAI* botAI, uint32 spellId, Item* item = nullptr)
{
    Player* bot = botAI->GetBot();
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo || bot->HasSpellCooldown(spellId) || !HasReagents(bot, spellInfo))
        return false;

    if (item)
        return bot->CanUseItem(item) == EQUIP_ERR_OK &&
               botAI->CanCastSpell(spellId, bot, false, nullptr, item);

    return bot->HasSpell(spellId) && botAI->CanCastSpell(spellId, bot, true);
}

bool ConvertTravelPath(PlayerbotAI* botAI, WorldPosition const& start, TravelPath const& travelPath,
                       ZoneTravelRoute& route)
{
    std::vector<PathNodePoint> points = travelPath.getPath();
    if (points.empty())
        return false;

    Player* bot = botAI->GetBot();
    WorldPosition previous = start;
    WorldPosition walkEnd;
    uint32 travelPrice = 0;

    auto flushWalk = [&]()
    {
        if (!walkEnd)
            return;
        route.cost += previous.fDist(walkEnd) / std::max(1.0f, bot->GetSpeed(MOVE_RUN));
        route.steps.push_back({ZoneTravelMethod::Walk, previous, walkEnd});
        previous = walkEnd;
        walkEnd = WorldPosition();
    };

    for (size_t i = 0; i < points.size(); ++i)
    {
        PathNodePoint const& point = points[i];
        if (point.type == NODE_PREPATH || point.type == NODE_PATH || point.type == NODE_NODE)
        {
            walkEnd = point.point;
            continue;
        }

        flushWalk();
        if (i + 1 >= points.size() || points[i + 1].type != point.type || points[i + 1].entry != point.entry)
            return false;

        PathNodePoint const& arrival = points[++i];
        ZoneTravelStep step;
        step.from = point.point;
        step.to = arrival.point;
        step.objectId = point.entry;

        switch (point.type)
        {
            case NODE_PORTAL:
                step.method = ZoneTravelMethod::AreaTriggerPortal;
                route.cost += 3.0f;
                break;
            case NODE_TRANSPORT:
                step.method = ZoneTravelMethod::Transport;
                route.cost += 120.0f;
                break;
            case NODE_FLIGHTPATH:
            {
                TaxiPathEntry const* taxiPath = sTaxiPathStore.LookupEntry(point.entry);
                if (!taxiPath || !bot->m_taxi.IsTaximaskNodeKnown(taxiPath->from) ||
                    !bot->m_taxi.IsTaximaskNodeKnown(taxiPath->to))
                    return false;
                travelPrice += taxiPath->price;
                if (travelPrice > bot->GetMoney())
                    return false;
                step.method = ZoneTravelMethod::KnownTaxi;
                route.cost += 60.0f;
                break;
            }
            case NODE_TELEPORT:
            {
                if (point.entry == 8690)
                {
                    Item* hearthstone = FindUsableItem(botAI, point.entry);
                    if (!hearthstone || !CanUseSpell(botAI, point.entry, hearthstone))
                        return false;
                    step.method = ZoneTravelMethod::OwnedItem;
                    step.itemGuid = hearthstone->GetGUID();
                }
                else
                {
                    if (!CanUseSpell(botAI, point.entry))
                        return false;
                    step.method = ZoneTravelMethod::KnownSpell;
                }
                route.cost += 10.0f;
                break;
            }
            default:
                return false;
        }

        route.steps.push_back(step);
        previous = arrival.point;
    }

    flushWalk();
    return !route.steps.empty();
}

ZoneTravelRoute BuildGraphRoute(PlayerbotAI* botAI, WorldPosition const& start, WorldPosition const& destination,
                                Player* routeBot)
{
    ZoneTravelRoute route;
    TravelPath travelPath = TravelNodeMap::getFullPath(start, destination, routeBot);
    if (!ConvertTravelPath(botAI, start, travelPath, route))
        route = ZoneTravelRoute();
    return route;
}

void ConsiderRoute(ZoneTravelRoute candidate, ZoneTravelRoute& best)
{
    if (candidate.steps.empty())
        return;
    if (best.steps.empty() || candidate.cost < best.cost)
        best = std::move(candidate);
}
}

ZoneTravelRoute ZoneTravelRoutePolicy::BuildRoute(PlayerbotAI* botAI, WorldPosition const& destination)
{
    std::lock_guard routeBuildLock(routeBuildMutex);
    if (!IsValidDestination(destination))
    {
        LOG_ERROR("playerbots", "[New RPG] {} rejected zone route destination with invalid map {}",
                  botAI->GetBot()->GetName(), destination.GetMapId());
        return {};
    }

    Player* bot = botAI->GetBot();
    WorldPosition start(bot);
    ZoneTravelRoute best = BuildGraphRoute(botAI, start, destination, bot);

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !bot->HasSpell(spellId))
            continue;

        WorldPosition arrival;
        if (!GetTeleportDestination(bot, spellId, arrival) || !CanUseSpell(botAI, spellId))
            continue;

        ZoneTravelRoute tail = BuildGraphRoute(botAI, arrival, destination, nullptr);
        if (tail.steps.empty() && !SameArrival(arrival, destination))
            continue;

        ZoneTravelRoute candidate;
        candidate.steps.push_back({ZoneTravelMethod::KnownSpell, start, arrival, spellId});
        candidate.cost = 10.0f;
        candidate.steps.insert(candidate.steps.end(), tail.steps.begin(), tail.steps.end());
        candidate.cost += tail.cost;
        ConsiderRoute(std::move(candidate), best);
    }

    for (Item* item : botAI->GetInventoryAndEquippedItems())
    {
        if (!item || bot->CanUseItem(item) != EQUIP_ERR_OK)
            continue;

        ItemTemplate const* itemTemplate = item->GetTemplate();
        for (uint8 slot = 0; slot < MAX_ITEM_PROTO_SPELLS; ++slot)
        {
            int32 itemSpellId = itemTemplate->Spells[slot].SpellId;
            if (itemSpellId <= 0 ||
                (itemTemplate->Spells[slot].SpellCharges && !item->GetSpellCharges(slot)))
                continue;
            uint32 spellId = static_cast<uint32>(itemSpellId);

            WorldPosition arrival;
            if (!GetTeleportDestination(bot, spellId, arrival) || !CanUseSpell(botAI, spellId, item))
                continue;

            ZoneTravelRoute tail = BuildGraphRoute(botAI, arrival, destination, nullptr);
            if (tail.steps.empty() && !SameArrival(arrival, destination))
                continue;

            ZoneTravelRoute candidate;
            ZoneTravelStep step{ZoneTravelMethod::OwnedItem, start, arrival, spellId};
            step.itemGuid = item->GetGUID();
            candidate.steps.push_back(step);
            candidate.cost = 10.0f;
            candidate.steps.insert(candidate.steps.end(), tail.steps.begin(), tail.steps.end());
            candidate.cost += tail.cost;
            ConsiderRoute(std::move(candidate), best);
        }
    }

    return best;
}

ZoneTravelStepResult ZoneTravelRoutePolicy::ExecuteNonWalkStep(PlayerbotAI* botAI, ZoneTravelStep const& step)
{
    Player* bot = botAI->GetBot();
    WorldPosition current(bot);
    if (SameArrival(current, step.to, 40.0f))
        return ZoneTravelStepResult::Complete;

    switch (step.method)
    {
        case ZoneTravelMethod::AreaTriggerPortal:
        {
            AreaTrigger const* trigger = sObjectMgr->GetAreaTrigger(step.objectId);
            AreaTriggerTeleport const* teleport = sObjectMgr->GetAreaTriggerTeleport(step.objectId);
            if (!trigger || !teleport || trigger->map != bot->GetMapId())
                return ZoneTravelStepResult::Failed;

            WorldPosition definedDestination(teleport->target_mapId, teleport->target_X, teleport->target_Y,
                                             teleport->target_Z);
            if (!IsValidDestination(definedDestination) || !SameArrival(definedDestination, step.to))
                return ZoneTravelStepResult::Failed;

            WorldPacket packet(CMSG_AREATRIGGER);
            packet << step.objectId;
            packet.rpos(0);
            bot->GetSession()->HandleAreaTriggerOpcode(packet);
            return ZoneTravelStepResult::InProgress;
        }
        case ZoneTravelMethod::KnownTaxi:
        {
            TaxiPathEntry const* taxiPath = sTaxiPathStore.LookupEntry(step.objectId);
            if (!taxiPath || !bot->m_taxi.IsTaximaskNodeKnown(taxiPath->from) ||
                !bot->m_taxi.IsTaximaskNodeKnown(taxiPath->to) || taxiPath->price > bot->GetMoney())
                return ZoneTravelStepResult::Failed;

            if (bot->IsInFlight())
                return ZoneTravelStepResult::InProgress;

            GuidVector npcs = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
            for (ObjectGuid const& guid : npcs)
            {
                Creature* flightMaster = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
                if (!flightMaster || bot->GetDistance(flightMaster) > INTERACTION_DISTANCE)
                    continue;

                uint32 node = sObjectMgr->GetNearestTaxiNode(flightMaster->GetPositionX(), flightMaster->GetPositionY(),
                                                            flightMaster->GetPositionZ(), flightMaster->GetMapId(),
                                                            bot->GetTeamId());
                if (node != taxiPath->from)
                    continue;

                botAI->RemoveShapeshift();
                if (bot->IsMounted())
                    bot->Dismount();
                return bot->ActivateTaxiPathTo({taxiPath->from, taxiPath->to}, flightMaster, 0)
                           ? ZoneTravelStepResult::InProgress
                           : ZoneTravelStepResult::Failed;
            }
            return ZoneTravelStepResult::Failed;
        }
        case ZoneTravelMethod::Transport:
        {
            if (Transport* transport = bot->GetTransport())
            {
                if (transport->GetEntry() != step.objectId)
                    return ZoneTravelStepResult::Failed;
                if (bot->GetMapId() == step.to.GetMapId() && bot->GetExactDist(step.to) < 60.0f)
                {
                    transport->RemovePassenger(bot);
                    return ZoneTravelStepResult::Complete;
                }
                return ZoneTravelStepResult::InProgress;
            }

            WorldPosition departure = step.from;
            for (Transport* transport : departure.getTransports(step.objectId))
            {
                if (transport->GetEntry() != step.objectId || bot->GetDistance(transport) > 10.0f)
                    continue;
                transport->AddPassenger(bot);
                return ZoneTravelStepResult::InProgress;
            }
            return ZoneTravelStepResult::InProgress;
        }
        case ZoneTravelMethod::KnownSpell:
        case ZoneTravelMethod::OwnedItem:
        {
            Item* item = step.method == ZoneTravelMethod::OwnedItem
                             ? FindUsableItem(botAI, step.objectId, step.itemGuid)
                             : nullptr;
            if (step.method == ZoneTravelMethod::OwnedItem && !item)
                return ZoneTravelStepResult::Failed;

            if (Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            {
                if (currentSpell->GetSpellInfo()->Id == step.objectId)
                    return ZoneTravelStepResult::InProgress;
            }

            WorldPosition definedDestination;
            if (!GetTeleportDestination(bot, step.objectId, definedDestination) ||
                !SameArrival(definedDestination, step.to) || !CanUseSpell(botAI, step.objectId, item))
                return ZoneTravelStepResult::Failed;

            if (bot->isMoving())
            {
                bot->StopMoving();
                return ZoneTravelStepResult::InProgress;
            }

            SpellCastResult result =
                bot->CastSpell(bot, step.objectId, TriggerCastFlags(TRIGGERED_NONE), item);
            return result == SPELL_CAST_OK ? ZoneTravelStepResult::InProgress : ZoneTravelStepResult::Failed;
        }
        case ZoneTravelMethod::Walk:
            break;
    }
    return ZoneTravelStepResult::Failed;
}

char const* ZoneTravelRoutePolicy::GetMethodName(ZoneTravelMethod method)
{
    switch (method)
    {
        case ZoneTravelMethod::Walk:
            return "walk";
        case ZoneTravelMethod::Transport:
            return "transport";
        case ZoneTravelMethod::KnownTaxi:
            return "known taxi";
        case ZoneTravelMethod::AreaTriggerPortal:
            return "area-trigger portal";
        case ZoneTravelMethod::KnownSpell:
            return "known spell";
        case ZoneTravelMethod::OwnedItem:
            return "owned item";
    }
    return "unknown";
}
