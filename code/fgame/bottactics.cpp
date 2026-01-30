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
        // Convert Recast coordinates to game coordinates immediately
        Vector gameHitPos;
        ConvertRecastToGameCoord(hitPos, gameHitPos);
        
        Vector gameHitNormal;
        ConvertRecastToGameCoord(hitNormal, gameHitNormal);

        // Let's propose the hit position itself as a cover spot if we are close enough
        // Or rather, if the distance to wall is less than our search radius, we consider moving there.
        if (dist < radius) {
             spot.position = gameHitPos;
             spot.type = COVER_FULL; // Assume full for now
             spot.valid = true;

             // Determine Peek Direction (using game coordinates)
             Vector toEnemy = enemyPos - gameHitPos;
             toEnemy.normalize();
             
             Vector up(0, 0, 1); // Game coordinate up vector
             Vector right = CrossProduct(toEnemy, up);
             right.normalize();

             float side = DotProduct(gameHitNormal, right);
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
    // Determine if we can peek at the target from origin in the requested direction
    // by reusing the tactical spot search logic.
    const float kDefaultPeekRadius = 256.0f;
    TacticalSpot spot = FindTacticalSpot(origin, target, kDefaultPeekRadius);

    if (!spot.valid)
    {
        return false;
    }

    return spot.peekDir == dir;
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

BotTactics::BotTactics() : m_controller(nullptr), m_inCover(false), m_stateTimer(0), m_currentPeek(PEEK_NONE), m_grenadeCooldown(0), m_cachedObjectivePos(vec_zero), m_objectiveCacheTime(0)
{
}

BotTactics::~BotTactics()
{
}

void BotTactics::Init(BotController* controller)
{
    m_controller = controller;
    
    // Build the behavior tree only once to avoid repeated allocations.
    if (!m_root)
    {
        BuildTree();
    }
}

Vector BotTactics::GetObjectivePosition()
{
    // Cache objective position to avoid linear search every frame
    // Refresh cache every 5 seconds
    const int cacheTimeoutMs = 5000;
    
    if (level.inttime - m_objectiveCacheTime < cacheTimeoutMs && m_cachedObjectivePos != vec_zero) {
        return m_cachedObjectivePos;
    }
    
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
            m_cachedObjectivePos = static_cast<Objective*>(ent)->GetOrigin();
            m_objectiveCacheTime = level.inttime;
            return m_cachedObjectivePos;
        }
        ent = G_Find(ent, FOFS(classname), "func_objective");
    }

    m_cachedObjectivePos = vec_zero;
    m_objectiveCacheTime = level.inttime;
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

        Player* self = m_controller->getControlledEntity();
        if (!self) {
            return BT_FAILURE;
        }
        
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
        if (!m_inCover) return BT_FAILURE;
        
        // Reset cover state if enemy is no longer present or position changed significantly
        auto enemy = m_controller->GetEnemy();
        if (!enemy) {
            m_inCover = false;
            return BT_FAILURE;
        }
        
        // Check if enemy position changed significantly (more than 300 units)
        if (m_currentCover.valid) {
            auto* controlledEntity = m_controller->getControlledEntity();
            if (controlledEntity) {
                Vector enemyPos = enemy->origin;
                Vector oldEnemyDir = m_currentCover.position - controlledEntity->origin;
                Vector newEnemyDir = enemyPos - controlledEntity->origin;
                
                // If the enemy has moved significantly relative to our cover, invalidate cover
                if ((enemyPos - m_currentCover.position).lengthSquared() > 300.0f * 300.0f) {
                    m_inCover = false;
                    m_currentCover.valid = false;
                    return BT_FAILURE;
                }
            }
        }
        
        return BT_SUCCESS;
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

        // Use a simple time-based cooldown to avoid frame-rate dependent behavior.
        // This limits how often we attempt a random duck while peeking.
        static int lastDuckAndLeanTime = 0;
        const int duckCooldownMs = 1000; // Minimum time between duck attempts in milliseconds

        if (m_currentPeek == PEEK_LEFT || m_currentPeek == PEEK_RIGHT) {
            if ((level.inttime - lastDuckAndLeanTime) >= duckCooldownMs) {
                if (rand() % 100 < 20) {
                    ctx.cmd->upmove = -127;
                }
                lastDuckAndLeanTime = level.inttime;
            }
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
        if (!m_controller->GetEnemy()) {
            // No enemy, reset cover state
            m_inCover = false;
            return BT_FAILURE;
        }
        if (m_inCover) return BT_FAILURE;
        return BT_SUCCESS;
    }));

    // Action: Find Cover (stance/peek only; movement handled by other states)
    seqCover->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        auto enemy = m_controller->GetEnemy();
        if (!enemy) {
            return BT_FAILURE;
        }

        auto* controlledEntity = m_controller->getControlledEntity();
        if (!controlledEntity) {
            return BT_FAILURE;
        }

        if (!m_currentCover.valid || (level.inttime > m_stateTimer)) {
             Vector enemyPos = enemy->origin;
             m_currentCover = TacticalAnalyzer::FindTacticalSpot(
                 controlledEntity->origin,
                 enemyPos,
                 500.0f
             );
             m_stateTimer = level.inttime + 2000;
        }

        if (m_currentCover.valid) {
            // Do not issue MoveTo here to avoid overriding attack movement.
            // Instead, update tactical state so other systems can use this info.
            m_currentPeek = m_currentCover.peekDir;
            
            // Consider bot "in cover" if close to the cover position
            Vector toCover = m_currentCover.position - controlledEntity->origin;
            if (toCover.lengthSquared() < 100.0f * 100.0f) {
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
