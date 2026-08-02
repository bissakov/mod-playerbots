/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CHOREACTIONS_H
#define PLAYERBOTS_CHOREACTIONS_H

#include "BankAction.h"
#include "BuyAction.h"
#include "SellAction.h"
#include "TrainerAction.h"

class PlayerbotAI;

class SellChoreAction : public SellAction
{
public:
    SellChoreAction(PlayerbotAI* botAI) : SellAction(botAI, "sell chore") {}

    bool Execute(Event event) override;
};

class TrainChoreAction : public TrainerAction
{
public:
    TrainChoreAction(PlayerbotAI* botAI) : TrainerAction(botAI) {}

    std::string const getName() override { return "train chore"; }
    bool Execute(Event event) override;
    bool isUseful() override;
    Unit* GetTarget() override;
};

class BankChoreAction : public BankAction
{
public:
    BankChoreAction(PlayerbotAI* botAI) : BankAction(botAI) {}

    std::string const getName() override { return "bank chore"; }
    bool Execute(Event event) override;
};

class BuyBagsChoreAction : public BuyAction
{
public:
    BuyBagsChoreAction(PlayerbotAI* botAI) : BuyAction(botAI) {}

    std::string const getName() override { return "buy bags chore"; }
    bool Execute(Event event) override;
};

#endif
