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
#include "weaputils.h" // For Projectile

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
        
        // Convert normal from Recast to game coordinate system
        // For direction vectors like normals, we apply the same axis transformation
        Vector gameHitNormal;
        gameHitNormal[0] = hitNormal[0];
        gameHitNormal[1] = -hitNormal[2];
        gameHitNormal[2] = hitNormal[1];
        gameHitNormal.normalize();

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
             Vector right = Vector::Cross(toEnemy, up);
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

BotTactics::BotTactics()
    : m_controller(nullptr)
    , m_inCover(false)
    , m_stateTimer(0)
    , m_currentPeek(PEEK_NONE)
    , m_grenadeCooldown(0)
    , m_cachedObjectivePos(vec_zero)
    , m_objectiveCacheTime(0)
    , m_lastDuckAndLeanTime(0)
    // Phase 1: Burst Fire
    , m_isFiring(false)
    , m_burstStartTime(0)
    , m_burstEndTime(0)
    , m_burstCooldownEnd(0)
    , m_burstLength(200)      // 200ms bursts
    , m_burstCooldown(300)    // 300ms between bursts
    // Phase 1: Strafe Dodging
    , m_strafeDirection(0)
    , m_strafeChangeTime(0)
    , m_nextStrafeChange(0)
    // Phase 1: Retreat
    , m_isRetreating(false)
    , m_retreatStartTime(0)
    , m_healthThreshold(0.3f) // Retreat below 30% health
    // Phase 1: Reload
    , m_needsReload(false)
    , m_lastReloadCheck(0)
    // Phase 2: Threat Memory
    , m_lastMemoryUpdate(0)
    // Phase 2: Lead Target Prediction
    , m_lastEnemyVel(vec_zero)
    , m_predictedPos(vec_zero)
    // Phase 2: Weapon Selection
    // Phase 2: Weapon Selection
    , m_lastWeaponSelectionTime(0)
    // Phase 3: Team Coordination
    , m_lastTeammateScan(0)
    , m_isFlanking(false)
    , m_lastFlankTime(0)
    // Phase 4: Tactical Polish
    , m_lastProjectileScan(0)
    , m_suppressionEndTime(0)
    , m_lastJumpTime(0)
    , m_isProne(false)
{
}

BotTactics::~BotTactics()
{
}

void BotTactics::Init(BotController* controller)
{
    m_controller = controller;
    // gi.Printf("BotTactics::Init called\n");
    
    Player* self = controller->getControlledEntity();
    // if (self) {
    //     gi.Printf("BotTactics::Init for %s\n", self->client->pers.netname);
    // }
    // Reset combat state
    m_isFiring = false;
    m_isFiring = false;
    m_isRetreating = false;
    m_strafeDirection = 0;
    m_isFlanking = false;
    m_suppressionEndTime = 0;
    m_isProne = false;
    
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
    
    if (level.inttime - m_objectiveCacheTime < cacheTimeoutMs) {
        return m_cachedObjectivePos;
    }
    
    // Find active func_objective
    // Using G_FindClass to iterate Objective entities.
    Entity *ent = NULL;

    // NOTE: This search might be slow if done every frame. Should ideally cache or event-drive.
    // For now, simple iteration.
    ent = G_FindClass(NULL, "Objective");
    while (ent) {
        if (ent->isSubclassOf(Objective)) {
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
        ent = G_FindClass(ent, "Objective");
    }

    m_cachedObjectivePos = vec_zero;
    m_objectiveCacheTime = level.inttime;
    return vec_zero;
}

void BotTactics::BuildTree()
{
    auto root = std::make_shared<BTSelector>();
    m_root = root;

    // 0. PHASE 4: Grenade Avoidance (Highest Priority)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_projectileMemory.empty()) return BT_FAILURE;

        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;

        // Flee from nearest projectile
        Vector threatPos = vec_zero;
        float minDistSq = 999999.0f;

        for (auto const& [entnum, pos] : m_projectileMemory) {
            float d2 = (pos - self->origin).lengthSquared();
            if (d2 < minDistSq) {
                minDistSq = d2;
                threatPos = pos;
            }
        }

        if (threatPos != vec_zero && minDistSq < 256.0f * 256.0f) {
            // gi.Printf("Bot %s fleeing from projectile!\n", self->client->pers.netname);
            
            // Vector away from threat
            Vector runDir = self->origin - threatPos;
            runDir.normalize();
            
            Vector safeSpot = self->origin + runDir * 400.0f;
            m_controller->GetMovement().MoveTo(safeSpot);
            
            // Sprint away
            ctx.cmd->buttons |= BUTTON_RUN;
            return BT_SUCCESS;
        }

        return BT_FAILURE;
    }));

    // 0.5. PHASE 4: Orders (Hold Position) - High Priority
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_currentOrder == ORDER_HOLD) {
            // Check if order expired (e.g., 30 seconds)
            if (level.inttime > m_orderTime + 30000) {
                m_currentOrder = ORDER_NONE;
                return BT_FAILURE;
            }
            
            // Allow firing/crouching but NO movement
            m_controller->GetMovement().ClearMove();
            
            // If we have an enemy, we can still shoot (State_Attack handles this if we don't move)
            // But we must return SUCCESS to prevent lower movement nodes from running.
            // However, we want 'State_Attack' logic (aiming, shooting) to run.
            // The Behavior Tree here seems to mix movement and actions.
            // If we return SUCCESS here, we stop processing.
            // We should arguably just prevent movement here.
            
            // Actually, State_Attack runs in BotController::State_Attack called by CheckStates.
            // BotTactics::Update controls movement goals primarily?
            // Yes, BotTactics calls `m_controller->GetMovement().MoveTo(...)`.
            
            // So if we just clear move and return SUCCESS, the bot stands still.
            // It can still aim and shoot if BotController::State_Attack is active.
            
            // Defensive stance instructions
            ctx.cmd->upmove = -127; // Crouch
            return BT_SUCCESS;
        }
        return BT_FAILURE;
    }));

    // 0.6. PHASE 4: Orders (Follow Me) - High Priority
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_currentOrder == ORDER_FOLLOW) {
            // Check if order expired
            if (level.inttime > m_orderTime + 60000) {
                m_currentOrder = ORDER_NONE;
                return BT_FAILURE;
            }

            Entity* target = m_orderTarget;
            if (!target) {
                m_currentOrder = ORDER_NONE;
                return BT_FAILURE;
            }

            // Move to target
            float dist = (target->origin - m_controller->getControlledEntity()->origin).length();
            if (dist > 300.0f) {
                m_controller->GetMovement().MoveTo(target->origin);
                ctx.cmd->buttons |= BUTTON_RUN;
                return BT_SUCCESS;
            } else {
                // We are close enough
                m_controller->GetMovement().ClearMove();
                return BT_SUCCESS;
            }
        }
        return BT_FAILURE;
    }));

     // 0.7. PHASE 4: Orders (Attack) - Modifies behavior rather than exclusive
     // Attack order essentially just means "Seek Enemy" which is default behavior if no order.
     // But we might want to prioritize it over hiding?
     // For now, let's just let it fall through to standard combat/hunt logic, 
     // maybe ensuring we don't retreat or hide as easily?
     // Let's implement it as an explicit "Hunt" node if we have an attack order but no enemy yet.
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_currentOrder == ORDER_ATTACK) {
             if (level.inttime > m_orderTime + 60000) {
                m_currentOrder = ORDER_NONE;
                return BT_FAILURE;
            }
            
            // If we have an enemy, let standard combat handle it (it will attack).
            if (m_controller->GetEnemy()) return BT_FAILURE;

            // If no enemy, roam aggressively?
            // Standard "Idle" state roams.
            // Maybe we just want to ensure we don't hold/hide?
            return BT_FAILURE;
        }
        return BT_FAILURE;
    }));

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
            float dist = (enemy->origin - self->origin).length();
            
            // User constraint: do not use nades in close combat
            if (dist < 400.0f) return BT_FAILURE;

            // Check for cluster
            int enemiesNear = 0;
            for (int i = 0; i < game.maxclients; i++) {
                gentity_t* ent = &g_entities[i];
                if (!ent->inuse || !ent->client) continue;
                Player* other = (Player*)ent->entity;
                if (!other || other == self || other->health <= 0) continue;
                if (g_gametype->integer >= GT_TEAM && other->GetTeam() == self->GetTeam()) continue;

                if ((other->origin - enemy->origin).lengthSquared() < (300.0f * 300.0f)) {
                    enemiesNear++;
                }
            }

            // Only use for clusters or clear room (distant single enemy is fine too if we consider clear room)
            if (enemiesNear < 2 && dist < 800.0f) {
                // If not a cluster and not very distant, maybe don't use
                // However "clear room" implies using it even on one if hidden?
                // For now, let's stick to cluster or >800 units for "clear room/distant"
                return BT_FAILURE;
            }

            // gi.Printf("Bot %s throwing grenade at %s (dist: %.0f, cluster: %d)\n", self->client->pers.netname, enemy->client->pers.netname, dist, enemiesNear);
            // Throw grenade
            self->useWeapon(grenade, WEAPON_MAIN);
            ctx.cmd->buttons |= BUTTON_ATTACKLEFT;
            m_grenadeCooldown = level.inttime + 8000; // Increased cooldown to 8s
            return BT_SUCCESS;
        }
        return BT_FAILURE;
    }));

    // 2. PHASE 1: Retreat when low health (High Priority)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;

        float healthPercent = (float)self->health / (float)self->max_health;
        
        // Check if we should start retreating
        if (healthPercent < m_healthThreshold && !m_isRetreating) {
            m_isRetreating = true;
            m_retreatStartTime = level.inttime;
        }
        
        // Stop retreating after 3 seconds or if health recovered
        if (m_isRetreating) {
            if (level.inttime - m_retreatStartTime > 3000 || healthPercent > 0.5f) {
                m_isRetreating = false;
                return BT_FAILURE;
            }
            
            // Find direction away from enemy
            Sentient* enemy = m_controller->GetEnemy();
            if (enemy) {
                Vector retreatDir = self->origin - enemy->origin;
                retreatDir.normalize();
                Vector retreatPos = self->origin + retreatDir * 300.0f;
                m_controller->GetMovement().MoveTo(retreatPos);
                
                // Also crouch while retreating
                ctx.cmd->upmove = -127;
            }
            return BT_SUCCESS;
        }
        
        return BT_FAILURE;
    }));

    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        Sentient* enemy = m_controller->GetEnemy();
        if (!enemy) return BT_FAILURE;

        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;

        // If currently flanking and not there yet
        if (m_isFlanking) {
            float dist = (m_flankPos - self->origin).length();
            if (dist < 100.0f) {
                m_isFlanking = false; // Arrived
                return BT_FAILURE; // Switch to other combat behaviors
            }
            m_controller->GetMovement().MoveTo(m_flankPos);
            return BT_RUNNING;
        }

        // Only flank occasionally
        if (level.inttime < m_lastFlankTime + 15000) return BT_FAILURE; 

        Vector flank = FindFlankPos(self->origin, enemy->origin);
        if (flank != vec_zero) {
            m_flankPos = flank;
            m_isFlanking = true;
            m_lastFlankTime = level.inttime;
            m_controller->GetMovement().MoveTo(m_flankPos);
            // gi.Printf("Bot %s attempting flank maneuver!\n", self->client->pers.netname);
            return BT_RUNNING;
        }

        return BT_FAILURE;
    }));

    // 3. PHASE 1: Smart Reload (reload when safe)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;

        // Check reload status periodically
        if (level.inttime - m_lastReloadCheck < 500) return BT_FAILURE;
        m_lastReloadCheck = level.inttime;

        Weapon* weapon = self->GetActiveWeapon(WEAPON_MAIN);
        if (!weapon) return BT_FAILURE;

        int clipAmmo = weapon->ClipAmmo(FIRE_PRIMARY);
        int maxClip = weapon->GetClipSize(FIRE_PRIMARY);
        
        // Need to reload if clip is low
        if (maxClip > 0 && clipAmmo < maxClip / 3) {
            Sentient* enemy = m_controller->GetEnemy();
            
            // Only reload if:
            // 1. No enemy visible
            // 2. In cover
            // 3. Enemy is far away (>500 units)
            bool safeToReload = !enemy || m_inCover;
            
            if (enemy && !m_inCover) {
                float distSq = (enemy->origin - self->origin).lengthSquared();
                safeToReload = distSq > 500.0f * 500.0f;
            }
            
            if (safeToReload) {
                // Crouch while reloading for safety
                ctx.cmd->upmove = -127;
                m_controller->SendCommand("reload");
                return BT_SUCCESS;
            }
        }
        
        return BT_FAILURE;
    }));

    // 4. PHASE 1: Strafe Dodging during combat
    auto seqStrafe = std::make_shared<BTSequence>();
    
    // Condition: Has enemy
    seqStrafe->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        return m_controller->GetEnemy() ? BT_SUCCESS : BT_FAILURE;
    }));
    
    // Action: Random strafe
    seqStrafe->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        // Change strafe direction periodically
        if (level.inttime >= m_nextStrafeChange) {
            // Random direction: -1, 0, or 1
            m_strafeDirection = (rand() % 3) - 1;
            // Change direction every 500-1500ms
            m_nextStrafeChange = level.inttime + 500 + (rand() % 1000);
            m_strafeChangeTime = level.inttime;
        }
        
        // Apply strafe movement
        if (m_strafeDirection != 0) {
            ctx.cmd->rightmove = m_strafeDirection * 127;
            
            // PHASE 4: Jump Dodging
            // 15% chance to jump when changing direction or randomly?
            // Let's do it on direction change in previous block, or just periodically here.
            if (level.inttime - m_lastJumpTime > 2000) { // Don't jump too often
                if (rand() % 100 < 5) { // 5% chance per frame? Too high.
                     // Logic: If strafing, maybe jump.
                     // Better: Check if we just changed direction.
                     if (level.inttime == m_strafeChangeTime) {
                         if (rand() % 100 < 15) {
                             ctx.cmd->upmove = 127; // Jump
                             m_lastJumpTime = level.inttime;
                            //  gi.Printf("Bot %s jump-dodging!\n", m_controller->getControlledEntity()->client->pers.netname);
                         }
                     }
                }
            }
        }
        
        return BT_SUCCESS;
    }));
    
    root->AddChild(seqStrafe);

    // 5. PHASE 1: Burst Fire Control (replaces continuous fire)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        Sentient* enemy = m_controller->GetEnemy();

        // PHASE 4: Suppressive Fire
        if (!enemy) {
            if (level.inttime < m_suppressionEndTime) {
                 Player* self = m_controller->getControlledEntity();
                 // Look at suppression target
                 m_controller->GetRotation().AimAt(m_suppressionPos);
                 
                 // Fire in short random bursts
                 if ((level.inttime / 100) % 3 == 0) {
                     ctx.cmd->buttons |= BUTTON_ATTACKLEFT;
                 }
                 
                 // Stop if we have moved too far or logic dictates? 
                 // Actually this leaf returns SUCCESS if suppressing, preventing lower nodes.
                 return BT_SUCCESS;
            }

            m_isFiring = false;
            return BT_FAILURE;
        }

        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;

        int currentTime = level.inttime;
        
        // Are we in cooldown between bursts?
        if (currentTime < m_burstCooldownEnd) {
            // Don't fire during cooldown
            return BT_SUCCESS; // Still consuming this node
        }
        
        // Should we start a new burst?
        if (!m_isFiring) {
            m_isFiring = true;
            m_burstStartTime = currentTime;
            // Randomize burst length slightly (150-250ms)
            m_burstLength = 150 + (rand() % 100);
            m_burstEndTime = currentTime + m_burstLength;
        }
        
        // Are we still in the burst window?
        if (currentTime < m_burstEndTime) {
            ctx.cmd->buttons |= BUTTON_ATTACKLEFT;
        } else {
            // Burst ended, enter cooldown
            m_isFiring = false;
            // Randomize cooldown (200-400ms)
            m_burstCooldown = 200 + (rand() % 200);
            m_burstCooldownEnd = currentTime + m_burstCooldown;
        }
        
        return BT_SUCCESS;
    }));

    // 6. Sequence: In Cover -> Peek & Shoot (with Ducking)
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
        const int duckCooldownMs = 1000; // Minimum time between duck attempts in milliseconds

        if (m_currentPeek == PEEK_LEFT || m_currentPeek == PEEK_RIGHT) {
            if ((level.inttime - m_lastDuckAndLeanTime) >= duckCooldownMs) {
                if (rand() % 100 < 20) {
                    ctx.cmd->upmove = -127;
                }
                m_lastDuckAndLeanTime = level.inttime;
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

    // 4. Follow Leader (If no combat and leader found)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (m_controller->GetEnemy()) return BT_FAILURE;
        
        Player* leader = FindLeader();
        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;

        if (leader) {
            Vector leaderPos = leader->origin;
            float dist = (leaderPos - self->origin).length();
            if (dist > 500.0f) {
                m_controller->GetMovement().MoveTo(leaderPos);
                return BT_SUCCESS;
            } else if (dist < 200.0f) {
                m_controller->GetMovement().ClearMove();
                return BT_SUCCESS;
            }
            return BT_SUCCESS; // In formation
        }
        return BT_FAILURE;
    }));

    // 5. Objective (If no combat or combat resolved)
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

    // Phase 2: Threat Memory Update
    ScanEnemies();

    // Phase 3: Team Coordination Update
    ScanTeammates();

    // Phase 2: Lead Target Prediction
    UpdatePrediction();

    // Phase 4: Projectile Awareness
    ScanProjectiles();

    // Phase 2: Context-Aware Weapon Selection
    UpdateWeaponSelection();

    BTContext ctx(m_controller, cmd);
    m_root->Tick(ctx);
}

void BotTactics::UpdatePrediction()
{
    Sentient* enemy = m_controller->GetEnemy();
    if (!enemy) {
        m_predictedPos = vec_zero;
        return;
    }

    Player* self = m_controller->getControlledEntity();
    if (!self) return;

    // Use memory if not currently visible
    EnemyMemory& mem = m_enemyMemory[enemy->entnum];
    Vector basePos = enemy->origin;
    Vector velocity = enemy->velocity;

    bool currentlyVisible = self->CanSee(enemy, 80, 2048, false);
    
    if (!currentlyVisible) {
        // If we "remember" them and it's been less than 5 seconds
        if (mem.lastSeenTime > 0 && (level.inttime - mem.lastSeenTime < 5000)) {
            basePos = mem.lastPosition;
            velocity = mem.velocity;
        } else {
            m_predictedPos = vec_zero;
            return;
        }
    } else {
        // Update memory while visible
        mem.lastPosition = enemy->origin;
        mem.velocity = enemy->velocity;
        mem.lastSeenTime = level.inttime;
        mem.currentlyVisible = true;
    }

    Vector delta = basePos - self->origin;
    float dist = delta.length();
    
    Weapon* weapon = self->GetActiveWeapon(WEAPON_MAIN);
    float projectileSpeed = 10000.0f; 
    
    if (weapon) {
        projectileSpeed = 5000.0f; 
    }

    float leadTime = dist / projectileSpeed;
    if (leadTime > 1.0f) leadTime = 1.0f;

    m_predictedPos = basePos + velocity * leadTime;

    if (currentlyVisible && velocity.length() > 10) {
        // gi.Printf("Bot %s leading %s by %.2f units\n", self->client->pers.netname, enemy->client->pers.netname, (velocity * leadTime).length());
    }
}

void BotTactics::ScanEnemies()
{
    if (level.inttime - m_lastMemoryUpdate < 100) return; // Update every 100ms
    m_lastMemoryUpdate = level.inttime;

    Player* self = m_controller->getControlledEntity();
    if (!self) return;

    for (int i = 0; i < game.maxclients; i++) {
        gentity_t* ent = &g_entities[i];
        if (!ent->inuse || !ent->client) continue;
        
        Player* other = (Player*)ent->entity;
        if (!other || other == self || other->health <= 0) continue;
        
        // Check if on different team
        if (g_gametype->integer >= GT_TEAM && other->GetTeam() == self->GetTeam()) continue;

        bool visible = self->CanSee(other, 80, 2048, false);
        
        EnemyMemory& mem = m_enemyMemory[other->entnum];
        if (visible) {
            mem.lastPosition = other->origin;
            mem.velocity = other->velocity;
            mem.lastSeenTime = level.inttime;
            mem.currentlyVisible = true;
            // gi.Printf("Bot %s tracking enemy %s at %.0f %.0f %.0f\n", self->client->pers.netname, other->client->pers.netname, other->origin.x, other->origin.y, other->origin.z);
        } else {
            // If just lost visibility, trigger suppression
            if (mem.currentlyVisible) {
                m_suppressionEndTime = level.inttime + 2000; // Suppress for 2 seconds
                m_suppressionPos = mem.lastPosition;
                // gi.Printf("Bot %s lost LOS, suppressing!\n", self->client->pers.netname);
            }
            mem.currentlyVisible = false;
        }
    }
}

void BotTactics::UpdateWeaponSelection()
{
    if (level.inttime - m_lastWeaponSelectionTime < 1000) return; // Every 1s
    m_lastWeaponSelectionTime = level.inttime;

    Player* self = m_controller->getControlledEntity();
    if (!self) return;

    Sentient* enemy = m_controller->GetEnemy();
    if (!enemy) return;

    float dist = (enemy->origin - self->origin).length();
    
    // Simple weapon selection logic
    Weapon* best = nullptr;
    if (dist > 1500.0f) {
        // Long range: Sniper
        best = self->BestWeapon(NULL, false, WEAPON_CLASS_RIFLE);
    } else if (dist > 500.0f) {
        // Medium range: SMG/Rifle
        best = self->BestWeapon(NULL, false, WEAPON_CLASS_SMG);
        if (!best) best = self->BestWeapon(NULL, false, WEAPON_CLASS_RIFLE);
    } else {
        // Close range: SMG/Shotgun
        best = self->BestWeapon(NULL, false, WEAPON_CLASS_MG);
        if (!best) best = self->BestWeapon(NULL, false, WEAPON_CLASS_SMG);
    }

    if (best && best != self->GetActiveWeapon(WEAPON_MAIN)) {
        // gi.Printf("Bot %s switching to %s (dist: %.0f)\n", self->client->pers.netname, best->getName().c_str(), dist);
        self->useWeapon(best, WEAPON_MAIN);
    }
}

void BotTactics::ScanTeammates()
{
    if (level.inttime - m_lastTeammateScan < 500) return; // Update every 500ms
    m_lastTeammateScan = level.inttime;

    Player* self = m_controller->getControlledEntity();
    if (!self) return;

    m_teammateMemory.clear();

    for (int i = 0; i < game.maxclients; i++) {
        gentity_t* ent = &g_entities[i];
        if (!ent->inuse || !ent->client) continue;
        
        Player* other = (Player*)ent->entity;
        if (!other || other == self || other->health <= 0) continue;
        
        // Check if on same team
        if (g_gametype->integer >= GT_TEAM && self->GetTeam() != other->GetTeam()) continue;
        // In FFA, teammates don't really exist in a standard way
        if (g_gametype->integer < GT_TEAM) continue;

        TeammateMemory& mem = m_teammateMemory[other->entnum];
        mem.position = other->origin;
        mem.entnum = other->entnum;
        // Check if human (not a bot)
        mem.isHuman = (other->edict->r.svFlags & SVF_BOT) ? false : true;
    }
}

Player* BotTactics::FindLeader()
{
    // Look for human teammates first
    for (auto const& [entnum, mem] : m_teammateMemory) {
        if (mem.isHuman) {
            gentity_t* ent = &g_entities[entnum];
            return (Player*)ent->entity;
        }
    }

    // Fallback? Pick the teammate with the lowest entnum
    if (!m_teammateMemory.empty()) {
         gentity_t* ent = &g_entities[m_teammateMemory.begin()->first];
         return (Player*)ent->entity;
    }

    return nullptr;
}

Vector BotTactics::FindFlankPos(const Vector& selfPos, const Vector& enemyPos)
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return vec_zero;

    Vector dir = selfPos - enemyPos;
    float dist = dir.length();
    if (dist < 200.0f) return vec_zero; // Too close to flank
    
    dir.normalize();

    // Perpendicular vector
    Vector perp(-dir.y, dir.x, 0);
    
    // Choose side based on randomness for now
    if (rand() % 2 == 0) perp *= -1.0f;

    Vector target = enemyPos + (dir * dist) + (perp * 800.0f);
    
    // Check if spot is valid cover
    TacticalSpot spot = TacticalAnalyzer::FindTacticalSpot(target, enemyPos, 400.0f);
    if (spot.valid) {
        return spot.position;
    }

    return vec_zero;
}

void BotTactics::ScanProjectiles()
{
    if (level.inttime - m_lastProjectileScan < 200) return; // Update every 200ms
    m_lastProjectileScan = level.inttime;

    m_projectileMemory.clear();

    Player* self = m_controller->getControlledEntity();
    if (!self) return;

    // Use findradius to detect nearby projectiles
    // NOTE: This assumes findradius is available as a global or accessible helper
    Entity* ent = findradius(NULL, self->origin, 500.0f);
    while (ent) {
        if (ent->isSubclassOf(Projectile)) {
             m_projectileMemory[ent->entnum] = ent->origin;
        }
        ent = findradius(ent, self->origin, 500.0f);
    }
}

void BotTactics::SetOrder(int orderType, Entity* target)
{
    m_currentOrder = orderType;
    m_orderTarget = target;
    m_orderTime = level.inttime;
    
    // Immediate feedback
    if (m_controller && m_controller->getControlledEntity()) {
        // gi.Printf("Bot %s received order %d\n", m_controller->getControlledEntity()->client->pers.netname, orderType);
    }
}
