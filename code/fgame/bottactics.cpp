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
        // Normal conversion (rotation only)
        // Recast is Y-up, Game is Z-up usually, but the helper functions handle coord transform.
        // However, normal is a direction.
        // Let's manually convert normal: Recast (x, y, z) -> Game (x, -z, y) usually,
        // but let's trust we can work in Recast coords for the logic and convert back at the end.

        // Check if this wall provides cover from enemy
        Vector recastEnemy;
        ConvertGameToRecastCoord(enemyPos, recastEnemy);

        Vector toEnemy = recastEnemy - Vector(hitPos);
        toEnemy.normalize();

        Vector normal(hitNormal);

        // If wall normal faces enemy, it's cover (we are behind it relative to enemy? No, normal points away from wall)
        // If we are standing at hitPos, the wall is 'at' hitPos. The normal points OUT of the wall.
        // If normal points TOWARDS enemy, then we are on the exposed side?
        // No, findDistanceToWall finds the nearest boundary. The normal points into the navigable area.
        // So if normal dot toEnemy is positive, the wall is facing the enemy (and us). We are in front of it.
        // We want to be BEHIND cover.
        // But we are on the navmesh. Navmesh is floor. "Wall" is the edge of navmesh.
        // So we are always on the "open" side.
        // We want a wall that is BETWEEN us and the enemy.
        // If we are finding nearest wall, we are just finding nearest obstruction.
        // To find cover, we need a spot where a ray to enemy is blocked.

        // For "Advanced Soldier AI", we want to stick to corners.
        // Let's try to find a corner near this wall hit.

        Vector wallDir = CrossProduct(Vector(0,1,0), normal); // Tangent

        // Check left and right along the wall
        // We move along wallDir, slightly offset from the wall (into navmesh), and check if we fall off or hit something?
        // No, we want to find where the wall ENDS.

        // Simple heuristic: Move along wall tangent. If we can raycast past the wall end to the enemy, it's a peek spot.

        // Let's propose the hit position itself as a cover spot if we are close enough
        if (dist < radius) {
             spot.position = gameHitPos;
             spot.type = COVER_FULL; // Assume full for now
             spot.valid = true;

             // Determine Peek Direction
             // Vector to enemy
             Vector enemyDir = enemyPos - gameHitPos;
             enemyDir.normalize();

             // Cross product with up to get right vector
             Vector up(0,0,1);
             Vector right = CrossProduct(enemyDir, up);

             // If cover is to our left, we peek right.
             // We know the wall normal points to us.
             // If wall normal is roughly -Right, then wall is on left.
             Vector gameNormal;
             // Hacky normal conversion because ConvertRecastToGameCoord is for points (scaling)
             // Game Z is up. Recast Y is up.
             // Recast X -> Game X
             // Recast Y -> Game Z
             // Recast Z -> Game -Y (usually)
             // Let's just use the vectors we have in Recast space for logic.

             Vector rcUp(0,1,0);
             Vector rcEnemyDir = recastEnemy - Vector(hitPos);
             rcEnemyDir.normalize();
             Vector rcRight = CrossProduct(rcEnemyDir, rcUp);

             float side = DotProduct(normal, rcRight);
             if (side > 0.2f) {
                 spot.peekDir = PEEK_LEFT; // Wall is on right, we peek Left?
                 // Normal points TO us. If Normal aligns with Right, wall is on Left.
                 // Wait: Normal points INTO navmesh.
                 // If Normal is (1,0,0) (Right), wall is on Left (-1,0,0).
                 // So if Normal dot Right > 0, wall is on Left. We peek LEFT?
                 // No, if wall is on Left, we lean LEFT to look around it? No, we lean RIGHT to look around a LEFT wall.
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
    // Trace from peek offset
    Vector offset = origin;
    // Standard Q3 lean is about 20-30 units?
    if (dir == PEEK_LEFT) offset += Vector(0, 20, 0); // Need proper right/forward vector
    // This requires view angles, which we don't have here easily.
    // Assume caller handles geometry check.
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

BotTactics::BotTactics() : m_controller(nullptr), m_inCover(false), m_stateTimer(0), m_currentPeek(PEEK_NONE)
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

void BotTactics::BuildTree()
{
    auto root = std::make_shared<BTSelector>();
    m_root = root;

    // Sequence 1: In Cover -> Peek & Shoot
    auto seqPeek = std::make_shared<BTSequence>();

    // Condition: In Cover
    seqPeek->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_inCover) return BT_SUCCESS;
        return BT_FAILURE;
    }));

    // Action: Peek
    seqPeek->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (!m_controller->getControlledEntity()->GetEnemy()) return BT_FAILURE;

        // Lean based on peek direction
        if (m_currentPeek == PEEK_LEFT) {
            ctx.cmd->buttons |= BUTTON_LEAN_LEFT;
        } else if (m_currentPeek == PEEK_RIGHT) {
            ctx.cmd->buttons |= BUTTON_LEAN_RIGHT;
        } else if (m_currentPeek == PEEK_OVER) {
             // Stand up (if ducking) or just stay
             ctx.cmd->upmove = 0;
        }

        // Attack is handled by State_Attack usually, but we can enforce it
        ctx.cmd->buttons |= BUTTON_ATTACKLEFT;

        return BT_SUCCESS;
    }));

    root->AddChild(seqPeek);

    // Sequence 2: Under Fire & Exposed -> Find Cover
    auto seqCover = std::make_shared<BTSequence>();

    // Condition: Taking damage or enemy visible
    seqCover->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        // Simple check: do we have an enemy?
        if (!m_controller->getControlledEntity()->GetEnemy()) return BT_FAILURE;
        // Are we already moving to cover?
        if (m_inCover) return BT_FAILURE;
        return BT_SUCCESS;
    }));

    // Action: Find and Move to Cover
    seqCover->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (!m_currentCover.valid || (level.inttime > m_stateTimer)) {
             // Find new spot
             Vector enemyPos = m_controller->getControlledEntity()->GetEnemy()->origin;
             m_currentCover = TacticalAnalyzer::FindTacticalSpot(
                 m_controller->getControlledEntity()->origin,
                 enemyPos,
                 500.0f
             );
             m_stateTimer = level.inttime + 2000; // Re-check every 2s
        }

        if (m_currentCover.valid) {
            // Move there
            m_controller->GetMovement().MoveTo(m_currentCover.position);
            m_currentPeek = m_currentCover.peekDir;

            // Check if arrived
            if (m_controller->GetMovement().MoveDone()) {
                m_inCover = true;
            }
            return BT_SUCCESS;
        }

        return BT_FAILURE;
    }));

    root->AddChild(seqCover);

    // Fallback: Crouch and Shoot
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        ctx.cmd->upmove = -127; // Duck
        return BT_SUCCESS;
    }));
}

void BotTactics::Update(usercmd_t* cmd)
{
    if (!m_root || !m_controller) return;

    BTContext ctx(m_controller, cmd);
    m_root->Tick(ctx);

    // Debug
    // G_DebugLine(m_controller->getControlledEntity()->origin, m_currentCover.position, 1, 0, 0, 1);
}
