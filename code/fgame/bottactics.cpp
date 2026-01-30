/*
===========================================================================
Copyright (C) 2025 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "bottactics.h"
#include "playerbot.h"
#include "navigation_recast_load.h"
#include "navigation_recast_helpers.h"
#include "Entities.h" // For Objective

// Detour includes
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourCommon.h>

//
// TacticalAnalyzer
//

static const vec3_t DETOUR_EXTENT = {(MAXS_X - MINS_X) / 2, (MAXS_Z - MINS_Z) / 2, (MAXS_Y - MINS_Y) / 2};

TacticalSpot TacticalAnalyzer::FindTacticalSpot(const Vector& origin, const Vector& enemyPos, float radius)
{
    TacticalSpot spot;
    spot.valid = false;

    if (!navigationMap.IsValid()) {
        return spot;
    }

    dtNavMeshQuery* query = navigationMap.GetNavMeshQuery();
    const dtQueryFilter* filter = navigationMap.GetQueryFilter();

    Vector recastOrigin;
    ConvertGameToRecastCoord(origin, recastOrigin);

    dtPolyRef startRef;
    float startPt[3];
    query->findNearestPoly(recastOrigin, DETOUR_EXTENT, filter, &startRef, startPt);

    if (!startRef) {
        return spot;
    }

    // 1. Find nearest wall (cover)
    float hitPos[3];
    float hitNormal[3];
    float dist;

    // Search radius for wall
    if (dtStatusSucceed(query->findDistanceToWall(startRef, startPt, radius, filter, &dist, hitPos, hitNormal))) {
        // We found a wall.
        Vector gameHitPos, gameHitNormal;
        ConvertRecastToGameCoord(hitPos, gameHitPos);

        Vector recastEnemy;
        ConvertGameToRecastCoord(enemyPos, recastEnemy);

        Vector toEnemy = recastEnemy - Vector(hitPos);
        toEnemy.normalize();

        Vector normal(hitNormal);

        // Let's propose the hit position itself as a cover spot if we are close enough
        // Or rather, if the distance to wall is less than our search radius, we consider moving there.
        if (dist < radius) {
             spot.position = gameHitPos;
             spot.type = COVER_FULL; // Assume full for now
             spot.valid = true;

             // Determine Peek Direction
             Vector rcUp(0,1,0);
             Vector rcEnemyDir = recastEnemy - Vector(hitPos);
             rcEnemyDir.normalize();
             Vector rcRight = CrossProduct(rcEnemyDir, rcUp);

             float side = DotProduct(normal, rcRight);
             if (side > 0.2f) {
                 spot.peekDir = PEEK_RIGHT;
             } else if (side < -0.2f) {
                 spot.peekDir = PEEK_LEFT;
             } else {
                 spot.peekDir = PEEK_OVER; // Crouch peek
             }
        }
    }

    return spot;
}

bool TacticalAnalyzer::CanPeek(const Vector& origin, const Vector& target, PeekDirection dir)
{
    // Simplified: Just return true for now, assuming FindTacticalSpot gave a valid peek spot.
    return true;
}

//
// Behavior Tree Implementation
//

BTStatus BTSelector::Tick(BTContext& ctx)
{
    for (auto& child : children) {
        BTStatus s = child->Tick(ctx);
        if (s != BT_FAILURE) {
            return s;
        }
    }
    return BT_FAILURE;
}

BTStatus BTSequence::Tick(BTContext& ctx)
{
    for (auto& child : children) {
        BTStatus s = child->Tick(ctx);
        if (s != BT_SUCCESS) {
            return s;
        }
    }
    return BT_SUCCESS;
}

//
// BotTactics
//

BotTactics::BotTactics() : m_controller(nullptr), m_inCover(false), m_stateTimer(0), m_currentPeek(PEEK_NONE), m_grenadeCooldown(0)
{
}

BotTactics::~BotTactics()
{
}

void BotTactics::Init(BotController* controller)
{
    m_controller = controller;
    BuildTree();
}

Vector BotTactics::GetObjectivePosition()
{
    // Find active func_objective
    Entity *ent = NULL;
    // Iterate G_Find(NULL, FOFS(classname), "func_objective") would be ideal but we need to check headers
    // Using G_Find from g_utils.cpp

    // NOTE: This search might be slow if done every frame. Should ideally cache or event-drive.
    // For now, simple iteration.
    ent = G_Find(NULL, FOFS(classname), "func_objective");
    while (ent) {
        if (ent->IsSubclassOf(Objective)) {
            // How to check if active? 'TurnOn' sets it.
            // We can check if it's not hidden?
            // The ScriptThread logic keeps track of objectives.
            // But we can just go to the first one found for now.
            // Better: Go to the one with the highest index (usually latest)?
            // Or assume objectives are linear.
            return static_cast<Objective*>(ent)->GetOrigin();
        }
        ent = G_Find(ent, FOFS(classname), "func_objective");
    }

    return vec_zero;
}

void BotTactics::BuildTree()
{
    auto root = std::make_shared<BTSelector>();
    m_root = root;

    // 1. Grenade Attack (High Priority if possible)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        Sentient* enemy = m_controller->GetEnemy();
        if (!enemy) return BT_FAILURE;

        if (level.inttime < m_grenadeCooldown) return BT_FAILURE;

        // Check chance
        if (rand() % 100 > 5) return BT_FAILURE; // 5% chance per tick if off cooldown

        Player* self = m_controller->getControlledEntity();
        Weapon* grenade = self->BestWeapon(NULL, false, WEAPON_CLASS_THROWABLE);

        if (grenade && grenade->HasAmmo(FIRE_PRIMARY)) {
            // Throw grenade
            self->useWeapon(grenade, WEAPON_MAIN);
            ctx.cmd->buttons |= BUTTON_ATTACKLEFT;
            m_grenadeCooldown = level.inttime + 5000; // 5s cooldown
            return BT_SUCCESS;
        }
        return BT_FAILURE;
    }));

    // 2. Sequence: In Cover -> Peek & Shoot (with Ducking)
    auto seqPeek = std::make_shared<BTSequence>();

    // Condition: In Cover
    seqPeek->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_inCover) return BT_SUCCESS;
        return BT_FAILURE;
    }));

    // Action: Peek
    seqPeek->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (!m_controller->GetEnemy()) return BT_FAILURE;

        // Lean based on peek direction
        if (m_currentPeek == PEEK_LEFT) {
            ctx.cmd->buttons |= BUTTON_LEAN_LEFT;
        } else if (m_currentPeek == PEEK_RIGHT) {
            ctx.cmd->buttons |= BUTTON_LEAN_RIGHT;
        } else if (m_currentPeek == PEEK_OVER) {
             // Crouch peek logic: stand up to fire, duck otherwise?
             // Actually 'over' usually means behind low cover.
             // We are standing (exposed), we want to duck (hide).
             // But to fire we must stand.
             // Let's toggle.
             if ((level.inttime / 1000) % 2 == 0) {
                 ctx.cmd->upmove = 0; // Stand
             } else {
                 ctx.cmd->upmove = -127; // Duck
             }
        }

        // Random "Duck and Lean"
        if ((m_currentPeek == PEEK_LEFT || m_currentPeek == PEEK_RIGHT) && (rand() % 100 < 20)) {
            ctx.cmd->upmove = -127;
        }

        // Attack is handled by State_Attack usually, but we can enforce it
        ctx.cmd->buttons |= BUTTON_ATTACKLEFT;

        return BT_SUCCESS;
    }));

    root->AddChild(seqPeek);

    // 3. Sequence: Under Fire & Exposed -> Find Cover
    auto seqCover = std::make_shared<BTSequence>();

    // Condition: Enemy visible
    seqCover->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (!m_controller->GetEnemy()) return BT_FAILURE;
        if (m_inCover) return BT_FAILURE;
        return BT_SUCCESS;
    }));

    // Action: Find and Move to Cover
    seqCover->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (!m_currentCover.valid || (level.inttime > m_stateTimer)) {
             Vector enemyPos = m_controller->GetEnemy()->origin;
             m_currentCover = TacticalAnalyzer::FindTacticalSpot(
                 m_controller->getControlledEntity()->origin,
                 enemyPos,
                 500.0f
             );
             m_stateTimer = level.inttime + 2000;
        }

        if (m_currentCover.valid) {
            m_controller->GetMovement().MoveTo(m_currentCover.position);
            m_currentPeek = m_currentCover.peekDir;

            if (m_controller->GetMovement().MoveDone()) {
                m_inCover = true;
            }
            return BT_SUCCESS;
        }

        return BT_FAILURE;
    }));

    root->AddChild(seqCover);

    // 4. Objective (If no combat or combat resolved)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_controller->GetEnemy()) return BT_FAILURE; // Prioritize combat

        Vector objPos = GetObjectivePosition();
        if (objPos != vec_zero) {
            m_controller->GetMovement().MoveTo(objPos);
            return BT_SUCCESS;
        }
        return BT_FAILURE;
    }));

    // 5. Fallback: Crouch and Shoot (Defensive Stance)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_controller->GetEnemy()) {
            ctx.cmd->upmove = -127; // Duck
            return BT_SUCCESS;
        }
        return BT_FAILURE;
    }));
}

void BotTactics::Update(usercmd_t* cmd)
{
    if (!m_root || !m_controller) return;

    BTContext ctx(m_controller, cmd);
    m_root->Tick(ctx);
}
