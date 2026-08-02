#ifndef PLAYERBOTS_ZONETRAVELPOLICY_H
#define PLAYERBOTS_ZONETRAVELPOLICY_H

#include "NewRpgInfo.h"

class PlayerbotAI;

struct ZoneTravelRoute
{
    std::vector<ZoneTravelStep> steps;
    float cost{0.0f};
};

enum class ZoneTravelStepResult : uint8
{
    InProgress,
    Complete,
    Failed
};

class ZoneTravelRoutePolicy
{
public:
    static ZoneTravelRoute BuildRoute(PlayerbotAI* botAI, WorldPosition const& destination);
    static ZoneTravelStepResult ExecuteNonWalkStep(PlayerbotAI* botAI, ZoneTravelStep const& step);
    static char const* GetMethodName(ZoneTravelMethod method);
};

#endif
