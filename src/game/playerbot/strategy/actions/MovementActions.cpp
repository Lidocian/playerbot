#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "../values/LastMovementValue.h"
#include "MovementActions.h"
#include "../../../MotionMaster.h"
#include "../../../movementGenerators/MovementGenerator.h"
#include "../../FleeManager.h"
#include "../../LootObjectStack.h"
#include "../../PlayerbotAIConfig.h"

using namespace ai;

bool MovementAction::MoveNear(uint32 mapId, float x, float y, float z, float distance)
{
    float angle = GetFollowAngle();
    return MoveTo(mapId, x + cos(angle) * distance, y + sin(angle) * distance, z);
}

bool MovementAction::MoveNear(WorldObject* target, float distance)
{
    if (!target)
        return false;

    distance += target->GetObjectBoundingRadius();

    float followAngle = GetFollowAngle();
    for (float angle = followAngle - M_PI; angle <= followAngle + M_PI; angle += M_PI / 4)
    {
        bool moved = MoveTo(target->GetMapId(),
            target->GetPositionX() + cos(angle) * distance,
            target->GetPositionY()+ sin(angle) * distance,
            target->GetPositionZ());
        if (moved)
            return true;
    }
    return false;
}

bool MovementAction::MoveTo(uint32 mapId, float x, float y, float z)
{
    if (!IsMovingAllowed(mapId, x, y, z))
        return false;

    bot->UpdateGroundPositionZ(x, y, z);

    float botZ = bot->GetPositionZ();
    if (z - botZ > 0.5f)
    {
        WaitForReach(0.5f);
        MotionMaster &mm = *bot->GetMotionMaster();
        mm.Clear();
        mm.MoveJump(x, y, botZ + 0.5f, bot->GetSpeed(MOVE_RUN), 0.5f);
        return true;
    }

    float distance = bot->GetDistance(x, y, z);
    if (distance > sPlayerbotAIConfig.meleeDistance / 2)
    {
        WaitForReach(distance);

        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        if (bot->IsNonMeleeSpellCasted(true))
        {
            bot->CastStop();
            ai->InterruptSpell();
        }

        bool generatePath = bot->GetAurasByType(SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED).empty() &&
                !bot->IsFlying() && !bot->IsUnderWater();
        MotionMaster &mm = *bot->GetMotionMaster();
        mm.Clear();
        mm.MovePoint(mapId, x, y, z, generatePath);
    }

    AI_VALUE(LastMovement&, "last movement").Set(x, y, z, bot->GetOrientation());
    return true;
}

bool MovementAction::MoveTo(Unit* target, float distance)
{
    if (!IsMovingAllowed(target))
        return false;

    float bx = bot->GetPositionX();
    float by = bot->GetPositionY();
    float bz = bot->GetPositionZ();

    float tx = target->GetPositionX();
    float ty = target->GetPositionY();
    float tz = target->GetPositionZ();

    float distanceToTarget = bot->GetDistance(target);
    float angle = bot->GetAngle(target);
    float needToGo = distanceToTarget - distance;

    float maxDistance = 2 * bot->GetSpeed(MOVE_RUN);
    if (needToGo > 0 && needToGo > maxDistance)
        needToGo = maxDistance;
    else if (needToGo < 0 && needToGo < -maxDistance)
        needToGo = -maxDistance;

    MotionMaster &mm = *bot->GetMotionMaster();
    if (abs(needToGo) <= sPlayerbotAIConfig.meleeDistance)
        return true;

    float dx = cos(angle) * needToGo + bx;
    float dy = sin(angle) * needToGo + by;

    return MoveTo(target->GetMapId(), dx, dy, tz);
}

float MovementAction::GetFollowAngle()
{
    Player* master = GetMaster();
    Group* group = master ? master->GetGroup() : bot->GetGroup();
    if (!group)
        return 0.0f;

    int index = 1;
    for (GroupReference *ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        if( ref->getSource() == master)
            continue;

        if( ref->getSource() == bot)
            return 2 * M_PI / (group->GetMembersCount() -1) * index;

        index++;
    }
    return 0;
}

bool MovementAction::IsMovingAllowed(Unit* target)
{
    if (!target)
        return false;

    if (bot->GetMapId() != target->GetMapId())
        return false;

    float distance = bot->GetDistance(target);
    if (distance > sPlayerbotAIConfig.reactDistance)
        return false;

    return IsMovingAllowed();
}

bool MovementAction::IsMovingAllowed(uint32 mapId, float x, float y, float z)
{
    float distance = bot->GetDistance(x, y, z);
    if (distance > sPlayerbotAIConfig.reactDistance)
        return false;

    return IsMovingAllowed();
}

bool MovementAction::IsMovingAllowed()
{
    if (bot->isFrozen() || bot->IsPolymorphed() ||
            (bot->isDead() && !bot->GetCorpse()) ||
            bot->IsBeingTeleported() || bot->IsBeingTeleportedDelayEvent() ||
            bot->isInRoots() ||
            bot->HasAuraType(SPELL_AURA_MOD_CONFUSE) || bot->isCharmed() ||
            bot->HasAuraType(SPELL_AURA_MOD_STUN) || bot->IsTaxiFlying())
        return false;

    MotionMaster &mm = *bot->GetMotionMaster();
    return mm.GetCurrentMovementGeneratorType() != FLIGHT_MOTION_TYPE;
}

bool MovementAction::Follow(Unit* target, float distance)
{
    return Follow(target, distance, GetFollowAngle());
}

bool MovementAction::Follow(Unit* target, float distance, float angle)
{
    MotionMaster &mm = *bot->GetMotionMaster();

    if (!target)
        return false;

    if (bot->GetDistance2d(target->GetPositionX(), target->GetPositionY()) <= sPlayerbotAIConfig.sightDistance &&
            abs(bot->GetPositionZ() - target->GetPositionZ()) >= sPlayerbotAIConfig.spellDistance)
    {
        mm.Clear();
        float x = bot->GetPositionX(), y = bot->GetPositionY(), z = target->GetPositionZ();
        if (target->GetMapId() && bot->GetMapId() != target->GetMapId())
        {
            bot->TeleportTo(target->GetMapId(), x, y, z, bot->GetOrientation());
        }
        else
        {
            Position pos(x, y, z, bot->GetOrientation());
            bot->SetPosition(pos, true);
        }
        AI_VALUE(LastMovement&, "last movement").Set(target);
        return true;
    }

    if (!IsMovingAllowed(target))
        return false;

    if (target->IsFriendlyTo(bot) && bot->IsMounted() && AI_VALUE(list<ObjectGuid>, "possible targets").empty())
        distance += angle;

    if (bot->GetDistance(target) <= sPlayerbotAIConfig.followDistance)
        return false;

    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    if (bot->IsNonMeleeSpellCasted(true))
    {
        bot->CastStop();
        ai->InterruptSpell();
    }

    mm.MoveFollow(target, distance, angle);

    AI_VALUE(LastMovement&, "last movement").Set(target);
    return true;
}

void MovementAction::WaitForReach(float distance)
{
    float delay = 1000.0f * distance / bot->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.reactDelay;

    if (delay > sPlayerbotAIConfig.maxWaitForMove)
        delay = sPlayerbotAIConfig.maxWaitForMove;

    Unit* target = *ai->GetAiObjectContext()->GetValue<Unit*>("current target");
    Unit* player = *ai->GetAiObjectContext()->GetValue<Unit*>("enemy player target");
    if ((player || target) && delay > sPlayerbotAIConfig.globalCoolDown)
        delay = sPlayerbotAIConfig.globalCoolDown;

    ai->SetNextCheckDelay((uint32)delay);
}

bool MovementAction::Flee(Unit *target)
{
    Player* master = GetMaster();
    if (!target)
        target = master;

    if (!target)
        return false;

    if (!sPlayerbotAIConfig.fleeingEnabled)
        return false;

    if (!IsMovingAllowed())
        return false;

    FleeManager manager(bot, sPlayerbotAIConfig.fleeDistance, GetFollowAngle());

    float rx, ry, rz;
    if (!manager.CalculateDestination(&rx, &ry, &rz))
        return false;

    return MoveTo(0, rx, ry, rz);
}

bool FleeAction::Execute(Event event)
{
    return Flee(AI_VALUE(Unit*, "current target"));
}

bool FleeAction::isUseful()
{
    return AI_VALUE(uint8, "attacker count") > 0 &&
            AI_VALUE2(float, "distance", "current target") <= sPlayerbotAIConfig.tooCloseDistance;
}

bool RunAwayAction::Execute(Event event)
{
    return Flee(AI_VALUE(Unit*, "master target"));
}

bool MoveRandomAction::Execute(Event event)
{
    WorldObject* target = NULL;

    if (!(rand() % 3))
    {
        list<ObjectGuid> npcs = AI_VALUE(list<ObjectGuid>, "nearest npcs");
        for (list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
        {
            target = ai->GetUnit(*i);

            if (target && bot->GetDistance(target) > sPlayerbotAIConfig.tooCloseDistance)
                break;
        }
    }

    if (!target || !(rand() % 3))
    {
        list<ObjectGuid> gos = AI_VALUE(list<ObjectGuid>, "nearest game objects");
        for (list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
        {
            target = ai->GetGameObject(*i);

            if (target && bot->GetDistance(target) > sPlayerbotAIConfig.tooCloseDistance)
                break;
        }
    }

    float distance = sPlayerbotAIConfig.tooCloseDistance + sPlayerbotAIConfig.grindDistance * urand(3, 10) / 10.0f;

    MapPtr map = bot->GetMapPtr();
    const TerrainInfoPtr terrain = map->GetTerrain();
    if (target)
    {
        float x = target->GetPositionX();
        float y = target->GetPositionY();
        float z = target->GetPositionZ();
        if (!terrain->IsInWater(x, y, z) && !terrain->IsUnderWater(x, y, z))
        {
            bool moved = MoveNear(target);
            map = MapPtr();
            return moved;
        }
    }

    for (int i = 0; i < 10; ++i)
    {
        float x = bot->GetPositionX();
        float y = bot->GetPositionY();
        float z = bot->GetPositionZ();
        x += urand(0, distance) - distance / 2;
        y += urand(0, distance) - distance / 2;
        bot->UpdateGroundPositionZ(x, y, z);

        if (terrain->IsInWater(x, y, z) || terrain->IsUnderWater(x, y, z))
            continue;

        bool moved = MoveNear(bot->GetMapId(), x, y, z);
        map = MapPtr();
        if (moved)
            return true;
    }

    map = MapPtr();
    return false;
}

bool MoveToLootAction::Execute(Event event)
{
    LootObject loot = AI_VALUE(LootObject, "loot target");
    if (!loot.IsLootPossible(bot))
        return false;

    return MoveNear(loot.GetWorldObject(bot));
}

bool MoveOutOfEnemyContactAction::Execute(Event event)
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;

    return MoveNear(target, sPlayerbotAIConfig.meleeDistance);
}

bool MoveOutOfEnemyContactAction::isUseful()
{
    return AI_VALUE2(float, "distance", "current target") <= sPlayerbotAIConfig.meleeDistance / 2;
}

bool SetFacingTargetAction::Execute(Event event)
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target)
        return false;

    bot->SetFacingTo(bot->GetAngle(target));
    ai->SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
    return true;
}

bool SetFacingTargetAction::isUseful()
{
    return !AI_VALUE2(bool, "facing", "current target");
}
