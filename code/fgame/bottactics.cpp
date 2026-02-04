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
#include "weapon.h"    // For Weapon class
#include "g_main.h"   // For g_protocol
#include "health.h"   // For Health pickups
#include "item.h"     // For Item base class
#include "ammo.h"     // For AmmoEntity

// Detour includes
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourCommon.h>

//
// TUNING CONSTANTS
// 
// This section contains centralized tuning parameters for bot behavior.
// Modify these values to balance bot skill, responsiveness, and difficulty.
//

// === Timing intervals (ms) - how often systems update ===
static const int TIMING_MEMORY_UPDATE        = 100;   // Enemy memory updates
static const int TIMING_PROJECTILE_SCAN      = 200;   // Projectile detection
static const int TIMING_RELOAD_CHECK         = 500;   // Reload state checks
static const int TIMING_TEAMMATE_SCAN        = 500;   // Teammate position updates
static const int TIMING_PICKUP_SCAN          = 500;   // Health/ammo pickup scan
static const int TIMING_WEAPON_SELECTION     = 1000;  // Weapon switching logic
static const int TIMING_SOUND_CLEANUP        = 2000;  // Sound memory cleanup
static const int TIMING_SQUAD_COORDINATION   = 2000;  // Squad role updates
static const int TIMING_OBJECTIVE_CACHE      = 5000;  // Objective caching

// === Duration values (ms) - how long behaviors persist ===
static const int DURATION_RETREAT_MAX        = 3000;  // Max retreat time
static const int DURATION_COVER_ADVANCE      = 5000;  // Cover-to-cover timeout
static const int DURATION_SQUAD_ROLE         = 10000; // Squad role reassignment
static const int DURATION_SMOKE_COOLDOWN     = 20000; // Between smoke throws

// === Distance values (units) - spatial thresholds ===
static const float DIST_GRENADE_FLEE_MIN     = 350.0f;  // Minimum flee distance
static const float DIST_GRENADE_FLEE_MAX     = 500.0f;  // Maximum flee distance
static const float DIST_FLANK_BASE           = 600.0f;  // Base flanking distance
static const float DIST_FLANK_VARIANCE       = 300.0f;  // Flanking distance variance
static const float DIST_PICKUP_SEARCH        = 1500.0f; // Health/ammo search radius
static const float DIST_ENEMY_TOO_CLOSE      = 500.0f;  // Cancel pickup if enemy nearer

// === Probability ranges (skill 0.0 to 1.0 maps to these ranges) ===
// Format: BASE + skill * SCALE = effective chance
static const float PROB_CALLOUT_BASE         = 0.30f;  // Base callout chance
static const float PROB_CALLOUT_SCALE        = 0.50f;  // Additional callout per skill
static const float PROB_ENEMY_SPOT_BASE      = 0.30f;  // Enemy spotted callout base
static const float PROB_ENEMY_SPOT_SCALE     = 0.40f;  // Enemy spotted scale
static const float PROB_SMOKE_USE_BASE       = 0.50f;  // Smoke usage base
static const float PROB_SMOKE_USE_SCALE      = 0.30f;  // Smoke usage scale
static const float PROB_JUMP_DODGE_BASE      = 0.05f;  // Jump dodge base (5%)
static const float PROB_JUMP_DODGE_SCALE     = 0.20f;  // Jump dodge scale
static const float PROB_INVESTIGATE_BASE     = 0.40f;  // Sound investigation base
static const float PROB_INVESTIGATE_SCALE    = 0.50f;  // Investigation scale

// === Jump and movement cooldowns (ms) ===
static const int JUMP_COOLDOWN_MIN           = 500;   // Min time between jumps
static const int JUMP_COOLDOWN_MAX           = 2000;  // Max time (scales with skill)

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
    , m_smokeCooldown(0)
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
    , m_mySquadRole(SQUAD_ROLE_NONE)
    , m_squadRoleAssignedTime(0)
    , m_lastSquadCoordinationTime(0)
    , m_sharedEnemyPos(vec_zero)
    // Phase 4: Tactical Polish
    , m_lastProjectileScan(0)
    , m_suppressionEndTime(0)
    , m_lastJumpTime(0)
    , m_lastKnownEnemyPos(vec_zero)
    , m_lastKnownEnemyTime(0)
    , m_isSuppressing(false)
    , m_lastEnemySpottedCallout(0)
    , m_lastReloadingCallout(0)
    , m_lastCoveringCallout(0)
    , m_isProne(false)
    // Grenade tactical state
    , m_lastGrenadeThrowTime(0)
    , m_pendingGrenadeTarget(vec_zero)
    , m_grenadeInFlight(false)
    , m_grenadeThrowStartTime(0)
    // Pickup seeking state
    , m_targetPickup(nullptr)
    , m_lastPickupScan(0)
    , m_seekingHealth(false)
    , m_seekingAmmo(false)
    // Cover-to-cover movement
    , m_advancingThroughCover(false)
    , m_lastCoverAdvanceTime(0)
    // Sound awareness
    , m_isInvestigatingSound(false)
    , m_lastSoundCleanup(0)
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
    m_isSuppressing = false;
    m_lastKnownEnemyPos = vec_zero;
    m_lastKnownEnemyTime = 0;
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
        Vector fleeDir;
        float danger;
        
        if (!ShouldFleeFromGrenade(fleeDir, danger)) {
            return BT_FAILURE;
        }

        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;

        // Calculate safe flee distance based on danger level
        float fleeDist = 350.0f + (danger * 150.0f);  // 350-500 units
        Vector safeSpot = self->origin + fleeDir * fleeDist;
        
        // Move to safe spot
        m_controller->GetMovement().MoveTo(safeSpot);
        
        // Sprint away
        ctx.cmd->buttons |= BUTTON_RUN;
        
        // Don't fire while fleeing - focus on survival
        ctx.cmd->buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
        
        return BT_SUCCESS;
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

    // 1. Grenade Attack (High Priority if valid tactical situation)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        Sentient* enemy = m_controller->GetEnemy();
        if (!enemy) return BT_FAILURE;

        // Check if grenade is ready (has grenade + cooldown expired)
        if (!IsGrenadeReady()) return BT_FAILURE;

        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;

        // Evaluate tactical grenade decision
        GrenadeDecision decision = EvaluateGrenadeThrow(enemy->origin);
        
        // Immediately reject if throw is impossible or teammate endangered
        if (!decision.canThrow) return BT_FAILURE;
        if (decision.teammateInBlastRadius) return BT_FAILURE;
        
        // STRICT GRENADE USAGE POLICY:
        // Only use grenades for:
        // 1. Cluster targets (2+ enemies grouped together)
        // 2. Room clearing (enemy in enclosed space, bot NOT in that space)
        
        bool validGrenadeUse = false;
        
        // Case 1: Multiple enemies clustered together (area denial/multi-kill opportunity)
        if (decision.enemyClusterSize >= 2 && decision.distanceToTarget > 300.0f) {
            validGrenadeUse = true;
        }
        
        // Case 2: Room clearing - enemy is in a room but we are NOT in that room
        if (!validGrenadeUse) {
            bool targetInRoom = IsTargetInRoom(enemy->origin);
            bool selfInRoom = IsSelfInRoom(enemy->origin);
            
            // Only throw if target is in room, we're safely outside, AND at safe distance
            // Increased distance requirement to prevent indoor self-kills
            if (targetInRoom && !selfInRoom && decision.distanceToTarget > GRENADE_BLAST_RADIUS + 150.0f) {
                validGrenadeUse = true;
            }
        }
        
        // ADDITIONAL SAFETY: Never throw if we're in the same enclosed space
        // Check if bot and target share the same room/area
        if (validGrenadeUse) {
            bool selfInAnyRoom = IsTargetInRoom(self->origin); // Check if bot is indoors
            bool targetInRoom = IsTargetInRoom(enemy->origin);
            
            // If both bot and target are indoors and close, don't throw
            if (selfInAnyRoom && targetInRoom && decision.distanceToTarget < 600.0f) {
                validGrenadeUse = false;
            }
        }
        
        // Skill affects grenade decision - lower skill bots are more hesitant
        float skill = GetSkillLevel();
        if (validGrenadeUse && skill < 0.3f) {
            // Low skill bots only throw 50% of the time even when valid
            if (G_Random(1.0f) > 0.5f) {
                validGrenadeUse = false;
            }
        }
        
        if (!validGrenadeUse) {
            return BT_FAILURE;
        }

        // Execute the throw
        if (ThrowGrenade(decision)) {
            // Aim in the throw direction
            Vector throwDir = decision.velocity;
            throwDir.normalize();
            Vector lookTarget = self->origin + throwDir * 100.0f;
            m_controller->GetRotation().AimAt(lookTarget);
            
            ctx.cmd->buttons |= BUTTON_ATTACKLEFT;
            return BT_SUCCESS;
        }

        return BT_FAILURE;
    }));

    // 1.5. Smoke Grenade Cover (when flanking or advancing in open)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        // Check if tactical situation calls for smoke
        if (!ShouldUseSmokeGrenade()) {
            return BT_FAILURE;
        }
        
        Sentient* enemy = m_controller->GetEnemy();
        if (!enemy) return BT_FAILURE;
        
        // Throw smoke for cover
        if (ThrowSmokeForCover(enemy->origin)) {
            ctx.cmd->buttons |= BUTTON_ATTACKLEFT;
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
                // Try to find and move toward health pickup instead of just retreating
                if (SeekHealthPickup()) {
                    ctx.cmd->upmove = -127; // Crouch while seeking health
                    return BT_SUCCESS;
                }
                
                // No health found, just retreat away from enemy
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

    // 2.5. Seek ammo when low (even if not retreating)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        if (!NeedsAmmo()) return BT_FAILURE;
        
        // Only seek ammo if no immediate combat threat
        Sentient* enemy = m_controller->GetEnemy();
        if (enemy) {
            Player* self = m_controller->getControlledEntity();
            if (self) {
                float distSq = (enemy->origin - self->origin).lengthSquared();
                // Too close to enemy, can't safely seek ammo
                if (distSq < 500.0f * 500.0f) return BT_FAILURE;
            }
        }
        
        if (SeekAmmoPickup()) {
            return BT_RUNNING;
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
                
                // Voice callout: "Reloading!" (with cooldown)
                float skill = GetSkillLevel();
                if (level.inttime > m_lastReloadingCallout + 8000) { // 8 second cooldown
                    if (G_Random(1.0f) < 0.4f + skill * 0.3f) { // 40-70% chance
                        Player* self = m_controller->getControlledEntity();
                        if (self) {
                            Event event("dmmessage");
                            event.AddInteger(-1);
                            // "*22" = "Reloading!" in MOHTA+
                            if (g_protocol >= protocol_e::PROTOCOL_MOHTA_MIN) {
                                event.AddString("*52");  // "Cover me, reloading!"
                            } else {
                                event.AddString("*22");
                            }
                            self->ProcessEvent(event);
                            m_lastReloadingCallout = level.inttime;
                        }
                    }
                }
                
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
    
    // Action: Random strafe - skill affects dodge frequency and agility
    seqStrafe->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        float skill = GetSkillLevel();
        
        // Change strafe direction periodically
        if (level.inttime >= m_nextStrafeChange) {
            // Random direction: -1, 0, or 1
            m_strafeDirection = (rand() % 3) - 1;
            
            // Skill affects strafe change frequency:
            // Low skill (0.0): 800-1800ms (slow, predictable)
            // High skill (1.0): 300-800ms (fast, unpredictable)
            int baseDelay = (int)(800 - skill * 500);     // 800 to 300
            int variableDelay = (int)(1000 - skill * 500); // 1000 to 500
            m_nextStrafeChange = level.inttime + baseDelay + (rand() % variableDelay);
            m_strafeChangeTime = level.inttime;
        }
        
        // Apply strafe movement
        if (m_strafeDirection != 0) {
            ctx.cmd->rightmove = m_strafeDirection * 127;
            
            // PHASE 4: Jump Dodging - skill affects jump chance
            // Low skill: 5% chance, High skill: 25% chance
            int jumpChance = (int)(5 + skill * 20);
            int jumpCooldown = (int)(2500 - skill * 1000); // 2.5s (low) to 1.5s (high)
            
            if (level.inttime - m_lastJumpTime > jumpCooldown) {
                // Check if we just changed direction
                if (level.inttime == m_strafeChangeTime) {
                    if (rand() % 100 < jumpChance) {
                        ctx.cmd->upmove = 127; // Jump
                        m_lastJumpTime = level.inttime;
                    }
                }
            }
        }
        
        return BT_SUCCESS;
    }));
    
    root->AddChild(seqStrafe);

    // 4.5. Weapon-specific positioning (snipers hold distance, SMGs close in)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        Sentient* enemy = m_controller->GetEnemy();
        if (!enemy) return BT_FAILURE;
        
        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;
        
        // Check if we should maintain distance (sniper)
        if (ShouldMaintainDistance()) {
            Vector awayDir = self->origin - enemy->origin;
            awayDir.normalize();
            Vector targetPos = self->origin + awayDir * 200.0f;
            m_controller->GetMovement().MoveTo(targetPos);
            
            // Move backward while maintaining aim
            ctx.cmd->forwardmove = -80;
            return BT_SUCCESS;
        }
        
        // Check if we should close distance (SMG)
        if (ShouldCloseDistance()) {
            // Use cover-to-cover advancement if in open terrain
            if (IsInOpenTerrain()) {
                if (AdvanceThroughCover(enemy->origin)) {
                    return BT_RUNNING;
                }
            }
            
            // Direct approach if no cover available
            m_controller->GetMovement().MoveTo(enemy->origin);
            ctx.cmd->forwardmove = 100;
            return BT_SUCCESS;
        }
        
        return BT_FAILURE;
    }));

    // 5. PHASE 1: Burst Fire Control (replaces continuous fire)
    root->AddChild(std::make_shared<BTLeaf>([this](BTContext& ctx) -> BTStatus {
        Sentient* enemy = m_controller->GetEnemy();

        // PHASE 4: Suppressive Fire at last known position
        if (!enemy) {
            if (level.inttime < m_suppressionEndTime && m_isSuppressing) {
                 Player* self = m_controller->getControlledEntity();
                 if (!self) return BT_FAILURE;
                 
                 // Don't suppress with explosive weapons - too dangerous
                 Weapon* activeWeapon = self->GetActiveWeapon(WEAPON_MAIN);
                 if (activeWeapon && (activeWeapon->GetWeaponClass() & WEAPON_CLASS_HEAVY)) {
                     m_isSuppressing = false;
                     return BT_FAILURE;
                 }
                 
                 // Look at suppression target
                 m_controller->GetRotation().AimAt(m_suppressionPos);
                 
                 // Skill affects suppression fire rate: low skill = sparse, high skill = dense
                 float skill = GetSkillLevel();
                 int burstFrequency = (int)(5 - skill * 3); // 5 (low skill) to 2 (high skill)
                 if (burstFrequency < 2) burstFrequency = 2;
                 
                 // Fire in short bursts based on skill
                 if ((level.inttime / 100) % burstFrequency == 0) {
                     ctx.cmd->buttons |= BUTTON_ATTACKLEFT;
                 }
                 
                 return BT_SUCCESS;
            }

            m_isFiring = false;
            m_isSuppressing = false;
            return BT_FAILURE;
        }

        Player* self = m_controller->getControlledEntity();
        if (!self) return BT_FAILURE;
        
        // EXPLOSIVE WEAPON SAFETY: Don't fire bazooka/rockets at close range
        Weapon* activeWeapon = self->GetActiveWeapon(WEAPON_MAIN);
        if (activeWeapon) {
            int weaponClass = activeWeapon->GetWeaponClass();
            
            // Check if this is an explosive weapon (heavy weapons like bazooka)
            if (weaponClass & WEAPON_CLASS_HEAVY) {
                float distToEnemy = (enemy->origin - self->origin).length();
                
                // Don't fire rockets if enemy is too close - would kill self
                if (distToEnemy < EXPLOSIVE_WEAPON_MIN_DIST) {
                    // Try to switch to a safer weapon instead
                    Weapon* safeWeapon = self->BestWeapon(activeWeapon, true, WEAPON_CLASS_PISTOL | WEAPON_CLASS_RIFLE | WEAPON_CLASS_SMG);
                    if (safeWeapon && safeWeapon != activeWeapon) {
                        self->useWeapon(safeWeapon, WEAPON_MAIN);
                    }
                    // Don't fire this frame - let weapon switch happen
                    return BT_SUCCESS;
                }
                
                // Also check if there's a wall between us and enemy that rocket would hit
                trace_t trace = G_Trace(self->origin + Vector(0,0,self->viewheight), 
                                        vec_zero, vec_zero, enemy->origin, self, MASK_SHOT, false, "ExplosiveCheck");
                if (trace.fraction < 0.9f) {
                    float hitDist = (Vector(trace.endpos) - self->origin).length();
                    if (hitDist < EXPLOSIVE_WEAPON_MIN_DIST) {
                        // Rocket would hit wall too close to us
                        return BT_SUCCESS; // Skip firing
                    }
                }
            }
        }

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
            
            // Skill affects burst length:
            // Low skill (0.0): 100-200ms (short, inaccurate bursts)
            // High skill (1.0): 200-350ms (longer, controlled bursts)
            float skill = GetSkillLevel();
            int baseBurst = (int)(100 + skill * 100);     // 100 to 200
            int variableBurst = (int)(100 + skill * 50);  // 100 to 150
            m_burstLength = baseBurst + (rand() % variableBurst);
            m_burstEndTime = currentTime + m_burstLength;
        }
        
        // Are we still in the burst window?
        if (currentTime < m_burstEndTime) {
            ctx.cmd->buttons |= BUTTON_ATTACKLEFT;
        } else {
            // Burst ended, enter cooldown
            m_isFiring = false;
            
            // Skill affects cooldown:
            // Low skill (0.0): 350-500ms (slow reactions)
            // High skill (1.0): 150-250ms (fast reactions)
            float skill = GetSkillLevel();
            int baseCooldown = (int)(350 - skill * 200);      // 350 to 150
            int variableCooldown = (int)(150 - skill * 50);   // 150 to 100
            m_burstCooldown = baseCooldown + (rand() % variableCooldown);
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
    
    // PERFORMANCE: Early-out for dead or spectating bots
    Player* self = m_controller->getControlledEntity();
    if (!self || self->health <= 0 || self->deadflag) {
        return;
    }
    
    // PERFORMANCE: Stagger expensive operations across frames
    // This distributes CPU load more evenly
    int frameOffset = self->entnum % 5; // Stagger by entity number
    bool doFullUpdate = ((level.inttime / 100) % 5) == frameOffset;

    // Phase 2: Threat Memory Update - always run (critical)
    ScanEnemies();

    // Phase 3: Team Coordination - staggered
    if (doFullUpdate || g_gametype->integer >= GT_TEAM) {
        ScanTeammates();
    }
    
    // Phase 3: Squad Coordination - staggered
    if (doFullUpdate && g_gametype->integer >= GT_TEAM) {
        CoordinateSquadAttack();
    }

    // Phase 2: Lead Target Prediction - always run (affects aim)
    UpdatePrediction();

    // Phase 4: Projectile Awareness - always run (safety critical)
    ScanProjectiles();

    // Phase 2: Context-Aware Weapon Selection - staggered
    if (doFullUpdate) {
        UpdateWeaponSelection();
    }

    BTContext ctx(m_controller, cmd);
    m_root->Tick(ctx);
    
    // Combat leaning - apply after behavior tree so it doesn't conflict
    // Only lean when we have an enemy
    if (m_controller->GetEnemy()) {
        ApplyCombatLean(cmd);
    }

    // Safety check to prevent walking into walls
    CheckMoveSafety(cmd);
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
    if (level.inttime - m_lastMemoryUpdate < TIMING_MEMORY_UPDATE) return;
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
        
        // PERFORMANCE: Distance pre-filter before expensive CanSee()
        // Skip visibility check for very distant players
        float distSq = (other->origin - self->origin).lengthSquared();
        static const float MAX_VISION_DIST_SQ = 4096.0f * 4096.0f; // 4096 units max vision

        bool visible = false;
        if (distSq < 250.0f * 250.0f) {
            // Close range awareness - always detect enemies within range regardless of FOV
            visible = true;
        } else if (distSq > MAX_VISION_DIST_SQ) {
            // Too far to see, clear visibility but keep memory
            EnemyMemory& mem = m_enemyMemory[other->entnum];
            mem.currentlyVisible = false;
            continue;
        } else {
            visible = self->CanSee(other, 80, 2048, false);
        }

        EnemyMemory& mem = m_enemyMemory[other->entnum];
        if (visible) {
            // Voice callout: "Enemy spotted!" when newly detecting an enemy
            bool wasHidden = !mem.currentlyVisible;
            
            mem.lastPosition = other->origin;
            mem.velocity = other->velocity;
            mem.lastSeenTime = level.inttime;
            mem.currentlyVisible = true;
            
            // Update last known position for coordination/suppression
            m_lastKnownEnemyPos = other->origin;
            m_lastKnownEnemyTime = level.inttime;
            
            // Announce when we first spot an enemy (with cooldown)
            if (wasHidden && level.inttime > m_lastEnemySpottedCallout + 5000) {
                float skill = GetSkillLevel();
                if (G_Random(1.0f) < 0.3f + skill * 0.4f) { // 30-70% chance
                    Event event("dmmessage");
                    event.AddInteger(-1);
                    // "*43" = "Enemy spotted!" in MOHTA+
                    if (g_protocol >= protocol_e::PROTOCOL_MOHTA_MIN) {
                        event.AddString("*43");
                    } else {
                        event.AddString("*33");  // "Contact!"
                    }
                    self->ProcessEvent(event);
                    m_lastEnemySpottedCallout = level.inttime;
                }
            }
        } else {
            // If just lost visibility, trigger suppression
            if (mem.currentlyVisible) {
                // Suppression duration based on skill: 1.5s (low) to 3s (high)
                int suppressDuration = (int)GetSkillAdjusted(1500.0f, 3000.0f, false);
                m_suppressionEndTime = level.inttime + suppressDuration;
                m_suppressionPos = mem.lastPosition;
                m_isSuppressing = true;
                
                // Voice callout: "Covering!" when starting suppression
                if (level.inttime > m_lastCoveringCallout + 6000) {
                    float skill = GetSkillLevel();
                    if (G_Random(1.0f) < 0.3f + skill * 0.4f) { // 30-70% chance
                        Event event("dmmessage");
                        event.AddInteger(-1);
                        // "*32" = "I'll cover you!" / "Covering!" in MOHTA+
                        if (g_protocol >= protocol_e::PROTOCOL_MOHTA_MIN) {
                            event.AddString("*53");  // "Suppressing!"
                        } else {
                            event.AddString("*32");
                        }
                        self->ProcessEvent(event);
                        m_lastCoveringCallout = level.inttime;
                    }
                }
            }
            mem.currentlyVisible = false;
        }
    }
}

void BotTactics::UpdateWeaponSelection()
{
    if (level.inttime - m_lastWeaponSelectionTime < TIMING_WEAPON_SELECTION) return;
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
    if (level.inttime - m_lastTeammateScan < TIMING_TEAMMATE_SCAN) return;
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
    if (level.inttime - m_lastProjectileScan < TIMING_PROJECTILE_SCAN) return;
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

//
// Grenade Trajectory Functions (adapted from Actor)
//

/*
===============
BotTactics::CalcThrowVelocity

Calculates required grenade throw velocity to get grenade from vFrom to vTo.
Adapted from Actor::CalcThrowVelocity
===============
*/
Vector BotTactics::CalcThrowVelocity(const Vector& vFrom, const Vector& vTo)
{
    Vector vDelta;
    float  fHorzDistSquared, fDistance;
    float  fVelHorz, fVelVert;

    vDelta           = vTo - vFrom;
    fHorzDistSquared = vDelta.lengthXYSquared();
    fDistance        = sqrt(fHorzDistSquared + Square(vDelta.z));

    if (fDistance < 1.0f) {
        return vec_zero;
    }

    // Gravity factor for grenades
    float fGravity = sv_gravity->value * GRENADE_GRAVITY_MULT;

    fVelVert = sqrt(fGravity * 0.5f * fHorzDistSquared / fDistance);
    
    if (fHorzDistSquared < 1.0f || (fDistance - vDelta.z) < 0.01f) {
        return vec_zero;
    }

    fVelHorz = sqrt((fDistance + vDelta.z) / (fDistance - vDelta.z) / fHorzDistSquared) * fVelVert;

    return Vector(vDelta.x * fVelHorz, vDelta.y * fVelHorz, fVelVert);
}

/*
===============
BotTactics::CalcRollVelocity

Calculates required grenade roll velocity for a low toss.
Adapted from Actor::CalcRollVelocity
===============
*/
Vector BotTactics::CalcRollVelocity(const Vector& vFrom, const Vector& vTo)
{
    Vector vDelta;
    float  fVelVert;
    float  fOOTime;

    // Roll requires throwing from above (higher than target)
    if (vTo.z >= vFrom.z) {
        return vec_zero;
    }

    vDelta = vTo - vFrom;
    float fGravity = sv_gravity->value * GRENADE_GRAVITY_MULT;
    
    fVelVert = sqrt(-vDelta.z * fGravity);
    
    if (fVelVert < 0.01f) {
        return vec_zero;
    }

    fOOTime = fGravity * 0.21961521f / fVelVert;

    return Vector(vDelta.x * fOOTime, vDelta.y * fOOTime, fVelVert);
}

/*
===============
BotTactics::ValidGrenadePath

Returns true if grenade trajectory from vFrom to vTo with vVel has no obstacles.
Adapted from Actor::ValidGrenadePath
===============
*/
bool BotTactics::ValidGrenadePath(const Vector& vFrom, const Vector& vTo, const Vector& vVel) const
{
    vec3_t  mins, maxs;
    Vector  vPoint1, vPoint2, vPoint3;
    float   fTime, fTimeLand;
    trace_t trace;
    float   fGravity;
    
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;

    VectorSet(mins, -4, -4, -4);
    VectorSet(maxs, 4, 4, 4);

    // Velocity sanity check
    if (vVel.lengthSquared() > Square(768)) {
        return false;
    }

    fGravity = sv_gravity->value * GRENADE_GRAVITY_MULT;

    fTime = vVel.z / fGravity * 0.5f;

    // First arc segment (launch)
    vPoint1.x = vVel.x * fTime + vFrom.x;
    vPoint1.y = vVel.y * fTime + vFrom.y;
    vPoint1.z = vVel.z * fTime * 0.75f + vFrom.z;
    maxs[2]   = fGravity / 8.0f * fTime * fTime + 4.0f;

    if (!G_SightTrace(vFrom, mins, maxs, vPoint1, self, NULL, MASK_GRENADEPATH, false, "BotTactics::ValidGrenadePath 1")) {
        return false;
    }

    // Second arc segment (apex)
    fTime *= 2;
    vPoint2.x = vVel.x * fTime + vFrom.x;
    vPoint2.y = vVel.y * fTime + vFrom.y;
    vPoint2.z = vVel.z * fTime * 0.5f + vFrom.z;

    if (!G_SightTrace(vPoint1, mins, maxs, vPoint2, self, NULL, MASK_GRENADEPATH, false, "BotTactics::ValidGrenadePath 2")) {
        return false;
    }

    // Third arc segment (descent)
    if (fabs(vVel.x) > fabs(vVel.y)) {
        if (fabs(vVel.x) < 0.01f) return false;
        fTimeLand = (vTo.x - vFrom.x) / vVel.x;
    } else {
        if (fabs(vVel.y) < 0.01f) return false;
        fTimeLand = (vTo.y - vFrom.y) / vVel.y;
    }

    maxs[2] = fGravity / 32.f * (fTimeLand - fTime) * (fTimeLand - fTime) + 4;
    fTime   = (fTime + fTimeLand) * 0.5f;

    vPoint3.x = vVel.x * fTime + vFrom.x;
    vPoint3.y = vVel.y * fTime + vFrom.y;
    vPoint3.z = vFrom.z + (vVel.z - fGravity * 0.5f * fTime) * fTime;

    if (!G_SightTrace(vPoint2, mins, maxs, vPoint3, self, NULL, MASK_GRENADEPATH, false, "BotTactics::ValidGrenadePath 3")) {
        return false;
    }

    // Final segment (landing)
    trace = G_Trace(vPoint3, mins, maxs, vTo, self, MASK_GRENADEPATH, false, "BotTactics::ValidGrenadePath 4");
    if (!trace.allsolid) {
        if (!trace.ent) {
            return true;
        }

        // Allow hitting enemies
        if (trace.ent->entity->IsSubclassOfSentient()) {
            Sentient* hit = static_cast<Sentient*>(trace.ent->entity);
            if (g_gametype->integer >= GT_TEAM) {
                if (hit->IsSubclassOfPlayer()) {
                    Player* hitPlayer = static_cast<Player*>(hit);
                    if (hitPlayer->GetTeam() != self->GetTeam()) {
                        return true;
                    }
                }
            } else {
                // FFA - any enemy is valid
                if (hit != self) {
                    return true;
                }
            }
        }
    }

    if (trace.entityNum != ENTITYNUM_WORLD || trace.plane.normal[2] < 0.999f) {
        return false;
    }

    return true;
}

/*
===============
BotTactics::CountEnemiesInRadius

Counts the number of enemies within radius of pos.
Used for cluster detection.
===============
*/
int BotTactics::CountEnemiesInRadius(const Vector& pos, float radius) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return 0;

    int count = 0;
    float radiusSq = radius * radius;

    for (int i = 0; i < game.maxclients; i++) {
        gentity_t* ent = &g_entities[i];
        if (!ent->inuse || !ent->client) continue;
        
        Player* other = (Player*)ent->entity;
        if (!other || other == self || other->health <= 0) continue;
        
        // Check if enemy
        if (g_gametype->integer >= GT_TEAM) {
            if (other->GetTeam() == self->GetTeam()) continue;
        }

        if ((other->origin - pos).lengthSquared() < radiusSq) {
            count++;
        }
    }

    return count;
}

/*
===============
BotTactics::IsTeammateInRadius

Returns true if any teammate is within radius of pos.
Used to prevent friendly fire.
===============
*/
bool BotTactics::IsTeammateInRadius(const Vector& pos, float radius) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;

    // No teammates in FFA
    if (g_gametype->integer < GT_TEAM) {
        return false;
    }

    float radiusSq = radius * radius;

    for (int i = 0; i < game.maxclients; i++) {
        gentity_t* ent = &g_entities[i];
        if (!ent->inuse || !ent->client) continue;
        
        Player* other = (Player*)ent->entity;
        if (!other || other == self || other->health <= 0) continue;
        
        // Check if teammate
        if (other->GetTeam() != self->GetTeam()) continue;

        if ((other->origin - pos).lengthSquared() < radiusSq) {
            return true;
        }
    }

    return false;
}

/*
===============
BotTactics::IsGrenadeReady

Returns true if bot can throw a grenade (has grenade, cooldown expired).
===============
*/
bool BotTactics::IsGrenadeReady() const
{
    if (level.inttime < m_grenadeCooldown) {
        return false;
    }

    Player* self = m_controller->getControlledEntity();
    if (!self) return false;

    Weapon* grenade = self->BestWeapon(NULL, false, WEAPON_CLASS_THROWABLE);
    return grenade && grenade->HasAmmo(FIRE_PRIMARY);
}

/*
===============
BotTactics::EvaluateGrenadeThrow

Evaluates whether throwing a grenade at targetPos is tactically sound.
Returns a GrenadeDecision with all relevant information.
===============
*/
GrenadeDecision BotTactics::EvaluateGrenadeThrow(const Vector& targetPos)
{
    GrenadeDecision decision;
    
    Player* self = m_controller->getControlledEntity();
    if (!self) return decision;

    // Get throw origin (slightly in front and above eye level)
    Vector throwOrigin = self->origin;
    throwOrigin.z += self->viewheight * 0.9f;

    // IMMEDIATE OBSTRUCTION CHECK
    // Cast a short ray forward to ensure we aren't facing a wall
    Vector forward;
    self->angles.AngleVectors(&forward, NULL, NULL);
    trace_t wallTrace = G_Trace(throwOrigin, vec_zero, vec_zero, throwOrigin + forward * 48.0f, self, MASK_SOLID, false, "GrenadeWallCheck");
    if (wallTrace.fraction < 1.0f) {
        return decision; // Blocked by wall immediately
    }

    decision.distanceToTarget = (targetPos - throwOrigin).length();

    // Distance checks
    if (decision.distanceToTarget < GRENADE_MIN_THROW_DIST) {
        // Too close - would hurt self
        return decision;
    }
    
    // CRITICAL SAFETY: Check if BOT would be in blast radius of the target
    // This prevents throwing grenades while standing in the kill zone
    if (decision.distanceToTarget < GRENADE_BLAST_RADIUS + GRENADE_SELF_SAFE_DIST) {
        // Bot is too close to where grenade will land - would hurt self
        return decision;
    }
    if (decision.distanceToTarget > GRENADE_MAX_THROW_DIST) {
        // Too far - can't throw that far
        return decision;
    }

    // Check for teammates in blast radius
    decision.teammateInBlastRadius = IsTeammateInRadius(targetPos, GRENADE_BLAST_RADIUS);
    if (decision.teammateInBlastRadius) {
        // Never throw if teammate could be hit
        return decision;
    }

    // Count enemy cluster
    decision.enemyClusterSize = CountEnemiesInRadius(targetPos, GRENADE_CLUSTER_RADIUS);

    // Try high throw first (standard arc)
    Vector throwVel = CalcThrowVelocity(throwOrigin, targetPos);
    if (throwVel != vec_zero && ValidGrenadePath(throwOrigin, targetPos, throwVel)) {
        decision.canThrow = true;
        decision.velocity = throwVel;
        decision.mode = GRENADE_THROW_HIGH;
        return decision;
    }

    // Try low toss/roll
    Vector rollVel = CalcRollVelocity(throwOrigin, targetPos);
    if (rollVel != vec_zero && ValidGrenadePath(throwOrigin, targetPos, rollVel)) {
        decision.canThrow = true;
        decision.velocity = rollVel;
        decision.mode = GRENADE_THROW_LOW;
        return decision;
    }

    return decision;
}

/*
===============
BotTactics::ThrowGrenade

Executes the grenade throw based on the decision.
Returns true if throw was initiated.
===============
*/
bool BotTactics::ThrowGrenade(const GrenadeDecision& decision)
{
    if (!decision.canThrow) {
        return false;
    }

    Player* self = m_controller->getControlledEntity();
    if (!self) return false;

    Weapon* grenade = self->BestWeapon(NULL, false, WEAPON_CLASS_THROWABLE);
    if (!grenade || !grenade->HasAmmo(FIRE_PRIMARY)) {
        return false;
    }

    // Switch to grenade and throw
    self->useWeapon(grenade, WEAPON_MAIN);
    
    m_pendingGrenadeTarget = decision.velocity;
    m_grenadeInFlight = true;
    m_grenadeThrowStartTime = level.inttime;
    m_grenadeCooldown = level.inttime + GRENADE_COOLDOWN_MS;
    
    // Voice callout: "Grenade! Take Cover!" (with chance based on skill)
    // Higher skill bots communicate more often
    float skill = GetSkillLevel();
    float calloutChance = 0.3f + skill * 0.5f; // 30%-80% chance based on skill
    
    if (G_Random(1.0f) < calloutChance) {
        Event event("dmmessage");
        event.AddInteger(-1); // Team message
        
        // *45 = "Grenade! Take Cover!" in MOHTA+
        // *35 = "Grenade! Take Cover!" in older versions
        if (g_protocol >= protocol_e::PROTOCOL_MOHTA_MIN) {
            event.AddString("*45");
        } else {
            event.AddString("*35");
        }
        
        self->ProcessEvent(event);
    }
    
    return true;
}

/*
===============
BotTactics::ShouldFleeFromGrenade

Checks if there's a nearby grenade/projectile the bot should flee from.
Returns flee direction and danger level (0-1).
===============
*/
bool BotTactics::ShouldFleeFromGrenade(Vector& outFleeDir, float& outDanger) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;

    Vector nearestThreat = vec_zero;
    float minDistSq = 999999.0f;
    outDanger = 0.0f;

    // Check projectile memory
    for (auto const& [entnum, pos] : m_projectileMemory) {
        float distSq = (pos - self->origin).lengthSquared();
        if (distSq < minDistSq) {
            minDistSq = distSq;
            nearestThreat = pos;
        }
    }

    // Also do a fresh scan for grenades specifically
    Entity* ent = findradius(NULL, self->origin, GRENADE_BLAST_RADIUS * 1.5f);
    while (ent) {
        if (ent->isSubclassOf(Projectile)) {
            Projectile* proj = static_cast<Projectile*>(ent);
            
            // Check if it's an enemy projectile (grenade)
            Sentient* owner = proj->GetOwner();
            bool isEnemy = true;
            if (owner == self) {
                isEnemy = false;
            } else if (owner && g_gametype->integer >= GT_TEAM) {
                if (owner->IsSubclassOfPlayer()) {
                    Player* ownerPlayer = static_cast<Player*>(owner);
                    if (ownerPlayer->GetTeam() == self->GetTeam()) {
                        isEnemy = false;
                    }
                }
            }

            if (isEnemy) {
                float distSq = (ent->origin - self->origin).lengthSquared();
                if (distSq < minDistSq) {
                    minDistSq = distSq;
                    nearestThreat = ent->origin;
                }
            }
        }
        ent = findradius(ent, self->origin, GRENADE_BLAST_RADIUS * 1.5f);
    }

    if (nearestThreat == vec_zero) {
        return false;
    }

    float dist = sqrt(minDistSq);
    
    // Only flee if within danger zone
    if (dist > GRENADE_BLAST_RADIUS * 1.2f) {
        return false;
    }

    // Calculate danger level (1.0 = on top of grenade, 0.0 = at edge of blast)
    outDanger = 1.0f - (dist / (GRENADE_BLAST_RADIUS * 1.2f));
    outDanger = Q_clamp_float(outDanger, 0.0f, 1.0f);

    // Flee direction = away from threat
    outFleeDir = self->origin - nearestThreat;
    outFleeDir.z = 0; // Keep flee horizontal
    if (outFleeDir.lengthSquared() < 1.0f) {
        // If we're right on top, pick random direction
        outFleeDir = Vector(G_CRandom(1.0f), G_CRandom(1.0f), 0);
    }
    outFleeDir.normalize();

    return true;
}

/*
===============
BotTactics::GetSkillLevel

Returns the bot skill level from cvar (0.0 - 1.0).
===============
*/
float BotTactics::GetSkillLevel() const
{
    float skill = g_bot_skill->value;
    return Q_clamp_float(skill, 0.0f, 1.0f);
}

/*
===============
BotTactics::GetSkillAdjusted

Returns a value interpolated between minVal and maxVal based on skill.
If invertForHarder is true, higher skill returns minVal (for delays/cooldowns).
Otherwise higher skill returns maxVal (for accuracy/speed).
===============
*/
float BotTactics::GetSkillAdjusted(float minVal, float maxVal, bool invertForHarder) const
{
    float skill = GetSkillLevel();
    if (invertForHarder) {
        // For things like reaction delay, higher skill = shorter delay
        return maxVal - skill * (maxVal - minVal);
    } else {
        // For things like accuracy, higher skill = better
        return minVal + skill * (maxVal - minVal);
    }
}

/*
===============
BotTactics::IsTargetInRoom

Returns true if the target position appears to be inside a room/enclosed space.
Uses trace rays to detect walls around the target.
===============
*/
bool BotTactics::IsTargetInRoom(const Vector& targetPos) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;

    // Cast rays in 8 directions from target to detect walls
    const float checkDist = 256.0f;
    int wallCount = 0;
    
    for (int i = 0; i < 8; i++) {
        float angle = i * 45.0f * (M_PI / 180.0f);
        Vector dir(cos(angle), sin(angle), 0);
        Vector endPos = targetPos + dir * checkDist;
        
        trace_t trace = G_Trace(targetPos, vec3_origin, vec3_origin, endPos, NULL, MASK_SOLID, false, "IsTargetInRoom");
        if (trace.fraction < 1.0f) {
            wallCount++;
        }
    }
    
    // Also check ceiling
    Vector ceilingPos = targetPos;
    ceilingPos.z += 128.0f;
    trace_t ceilingTrace = G_Trace(targetPos, vec3_origin, vec3_origin, ceilingPos, NULL, MASK_SOLID, false, "IsTargetInRoom ceiling");
    if (ceilingTrace.fraction < 1.0f) {
        wallCount++;
    }
    
    // Consider it a room if 4+ walls detected (half+ of directions blocked)
    return wallCount >= 4;
}

/*
===============
BotTactics::IsSelfInRoom

Returns true if bot is in the same enclosed space as the target.
Uses line of sight and distance checks.
===============
*/
bool BotTactics::IsSelfInRoom(const Vector& targetPos) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;

    // Quick distance check - if far away, not in same room
    float dist = (self->origin - targetPos).length();
    if (dist > 400.0f) {
        return false;
    }

    // Check if there's clear line of sight (no intervening walls)
    Vector eyePos = self->origin;
    eyePos.z += self->viewheight;
    
    trace_t trace = G_Trace(eyePos, vec3_origin, vec3_origin, targetPos, self, MASK_SOLID, false, "IsSelfInRoom");
    
    // If we have clear LOS and are within close range, we're likely in the same room
    return (trace.fraction == 1.0f && dist < 300.0f);
}

/*
===============
BotTactics::IsSmokeGrenade

Returns true if the given weapon is a smoke grenade.
Checks weapon name for "smoke" substring.
===============
*/
bool BotTactics::IsSmokeGrenade(Weapon* weapon) const
{
    if (!weapon) return false;
    
    // Check if weapon name contains "smoke" (case-insensitive)
    const char* weaponName = weapon->getName().c_str();
    return (Q_stristr(weaponName, "smoke") != NULL);
}

/*
===============
BotTactics::HasSmokeGrenade

Returns true if bot has a smoke grenade available.
===============
*/
bool BotTactics::HasSmokeGrenade() const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    // Check all throwables
    Weapon* throwable = self->BestWeapon(NULL, false, WEAPON_CLASS_THROWABLE);
    while (throwable) {
        if (IsSmokeGrenade(throwable) && throwable->HasAmmo(FIRE_PRIMARY)) {
            return true;
        }
        // Get next throwable (if inventory supports multiple)
        // For now, just check the best one
        break;
    }
    
    return false;
}

/*
===============
BotTactics::DetermineMyRole

Determines this bot's role in a coordinated squad attack.
Based on entity number to distribute roles.
===============
*/
SquadRole BotTactics::DetermineMyRole() const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return SQUAD_ROLE_NONE;
    
    // Count how many bot teammates we have and what position this bot is
    int myIndex = 0;
    int totalBots = 0;
    
    for (int i = 0; i < game.maxclients; i++) {
        gentity_t* ent = &g_entities[i];
        if (!ent->inuse || !ent->client) continue;
        
        Player* other = (Player*)ent->entity;
        if (!other || other->health <= 0) continue;
        
        // Same team only
        if (g_gametype->integer >= GT_TEAM && other->GetTeam() != self->GetTeam()) continue;
        
        // Count bots only
        if (ent->r.svFlags & SVF_BOT) {
            if (other == self) {
                myIndex = totalBots;
            }
            totalBots++;
        }
    }
    
    if (totalBots <= 1) {
        return SQUAD_ROLE_ASSAULT; // Solo bot - just attack
    }
    
    // Distribute roles based on position in team
    // First bot suppresses, others flank alternating left/right
    switch (myIndex % 3) {
        case 0:
            return SQUAD_ROLE_SUPPRESS;
        case 1:
            return SQUAD_ROLE_FLANK_LEFT;
        case 2:
            return SQUAD_ROLE_FLANK_RIGHT;
        default:
            return SQUAD_ROLE_ASSAULT;
    }
}

/*
===============
BotTactics::GetFlankPosition

Gets a flanking position on the left or right of the enemy.
===============
*/
Vector BotTactics::GetFlankPosition(bool rightFlank) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return vec_zero;
    
    Sentient* enemy = m_controller->GetEnemy();
    Vector enemyPos = enemy ? enemy->origin : m_lastKnownEnemyPos;
    
    if (enemyPos == vec_zero) return vec_zero;
    
    Vector dir = self->origin - enemyPos;
    float dist = dir.length();
    if (dist < 200.0f) return vec_zero;
    
    dir.normalize();
    
    // Perpendicular vector
    Vector perp(-dir.y, dir.x, 0);
    if (!rightFlank) perp *= -1.0f;
    
    // Target position: wide flank at 600-900 units perpendicular
    float flankDist = 600.0f + G_Random(300.0f);
    Vector target = enemyPos + (dir * (dist * 0.5f)) + (perp * flankDist);
    
    // Check if spot is valid cover
    TacticalSpot spot = TacticalAnalyzer::FindTacticalSpot(target, enemyPos, 400.0f);
    if (spot.valid) {
        return spot.position;
    }
    
    return target;
}

/*
===============
BotTactics::CoordinateSquadAttack

Coordinates attack with teammates.
Called during Update to synchronize flanking maneuvers.
===============
*/
void BotTactics::CoordinateSquadAttack()
{
    // Don't coordinate too frequently
    if (level.inttime - m_lastSquadCoordinationTime < TIMING_SQUAD_COORDINATION) return;
    m_lastSquadCoordinationTime = level.inttime;
    
    Player* self = m_controller->getControlledEntity();
    if (!self) return;
    
    Sentient* enemy = m_controller->GetEnemy();
    if (!enemy && m_lastKnownEnemyPos == vec_zero) {
        m_mySquadRole = SQUAD_ROLE_NONE;
        return;
    }
    
    // Re-evaluate role every 10 seconds or if no role
    if (m_mySquadRole == SQUAD_ROLE_NONE || level.inttime - m_squadRoleAssignedTime > 10000) {
        m_mySquadRole = DetermineMyRole();
        m_squadRoleAssignedTime = level.inttime;
    }
    
    // Share enemy position with teammates
    if (enemy) {
        m_sharedEnemyPos = enemy->origin;
    }
    
    // Execute role-based behavior
    switch (m_mySquadRole) {
        case SQUAD_ROLE_SUPPRESS:
            // Suppressor stays in place and fires
            // Already handled by m_isSuppressing
            break;
            
        case SQUAD_ROLE_FLANK_LEFT:
            if (!m_isFlanking && level.inttime > m_lastFlankTime + 8000) {
                Vector flankPos = GetFlankPosition(false);
                if (flankPos != vec_zero) {
                    m_flankPos = flankPos;
                    m_isFlanking = true;
                    m_lastFlankTime = level.inttime;
                }
            }
            break;
            
        case SQUAD_ROLE_FLANK_RIGHT:
            if (!m_isFlanking && level.inttime > m_lastFlankTime + 8000) {
                Vector flankPos = GetFlankPosition(true);
                if (flankPos != vec_zero) {
                    m_flankPos = flankPos;
                    m_isFlanking = true;
                    m_lastFlankTime = level.inttime;
                }
            }
            break;
            
        case SQUAD_ROLE_ASSAULT:
        default:
            // Direct engagement - no special behavior
            break;
    }
}

/*
===============
BotTactics::GetSmokeGrenade

Returns the smoke grenade weapon if the bot has one available.
===============
*/
Weapon* BotTactics::GetSmokeGrenade() const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return nullptr;
    
    // Iterate through throwables to find smoke
    Weapon* throwable = self->BestWeapon(NULL, false, WEAPON_CLASS_THROWABLE);
    while (throwable) {
        if (IsSmokeGrenade(throwable) && throwable->HasAmmo(FIRE_PRIMARY)) {
            return throwable;
        }
        break;  // Only checking best one
    }
    
    return nullptr;
}

/*
===============
BotTactics::ShouldUseSmokeGrenade

Determines if tactical situation calls for smoke cover.
Use when:
- Flanking and need concealment
- Advancing on enemy position across open ground
- Enemy is at significant range with good sightlines
===============
*/
bool BotTactics::ShouldUseSmokeGrenade() const
{
    // Check cooldown
    if (level.inttime < m_smokeCooldown) {
        return false;
    }
    
    // Need smoke grenade available
    if (!HasSmokeGrenade()) {
        return false;
    }
    
    Player* self = m_controller->getControlledEntity();
    Sentient* enemy = m_controller->GetEnemy();
    
    if (!self || !enemy) return false;
    
    Vector toEnemy = enemy->origin - self->origin;
    float distance = toEnemy.length();
    
    // Only use smoke at medium to long range (400-1500 units)
    if (distance < 400.0f || distance > 1500.0f) {
        return false;
    }
    
    // Check if we're flanking - smoke is excellent cover for flanking maneuvers
    if (m_isFlanking) {
        return true;
    }
    
    // Check if we're in open terrain (trace straight to enemy succeeds)
    Vector start = self->origin + Vector(0, 0, self->viewheight);
    Vector end = enemy->origin + Vector(0, 0, enemy->viewheight);
    
    trace_t trace;
    trace = G_Trace(start, vec_zero, vec_zero, end, self, MASK_SHOT, false, "ShouldUseSmokeGrenade");
    
    // If clear line of sight at range, smoke helps
    if (trace.fraction > 0.9f || trace.entityNum == enemy->entnum) {
        // Additional check: are we moving toward enemy?
        // Check if we have forward momentum
        float dotToEnemy = Vector(self->velocity[0], self->velocity[1], 0) * toEnemy;
        if (dotToEnemy > 50.0f) {  // Moving toward enemy
            return true;
        }
    }
    
    return false;
}

/*
===============
BotTactics::ThrowSmokeForCover

Throws a smoke grenade between self and target for concealment.
Targets a point approximately halfway to enemy or slightly in front of self.
Returns true if smoke was thrown.
===============
*/
bool BotTactics::ThrowSmokeForCover(const Vector& targetPos)
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    Weapon* smoke = GetSmokeGrenade();
    if (!smoke) return false;
    
    // Calculate where to throw smoke - about 1/3 of the way to target
    // This creates a smoke screen we can advance through
    Vector toTarget = targetPos - self->origin;
    float dist = toTarget.length();
    toTarget.normalize();
    
    // Smoke landing point - closer to us than to enemy
    float smokeDist = dist * 0.3f;
    if (smokeDist < 200.0f) smokeDist = 200.0f;
    if (smokeDist > 600.0f) smokeDist = 600.0f;
    
    Vector smokeTarget = self->origin + toTarget * smokeDist;
    
    // Switch to smoke grenade and throw
    self->useWeapon(smoke, WEAPON_MAIN);
    
    m_smokeCooldown = level.inttime + 20000; // 20 second cooldown on smoke
    
    // Voice callout
    float skill = GetSkillLevel();
    if (G_Random(1.0f) < 0.5f + skill * 0.3f) {
        Event event("dmmessage");
        event.AddInteger(-1);
        
        // "*32" = "I'll cover you!" / "Covering!" in MOHTA+
        if (g_protocol >= protocol_e::PROTOCOL_MOHTA_MIN) {
            event.AddString("*52");  // "Popping smoke!"
        } else {
            event.AddString("*32");
        }
        
        self->ProcessEvent(event);
    }
    
    return true;
}

/*
===============
BotTactics::GetPreferredCombatDistance

Returns the preferred combat distance based on equipped weapon class.
Snipers prefer long range, SMGs prefer close range.
===============
*/
float BotTactics::GetPreferredCombatDistance() const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return 600.0f; // Default medium range
    
    Weapon* weapon = self->GetActiveWeapon(WEAPON_MAIN);
    if (!weapon) return 600.0f;
    
    int weaponClass = weapon->GetWeaponClass();
    
    // Sniper rifles - stay far
    if (weaponClass & WEAPON_CLASS_RIFLE) {
        // Check if it's actually a sniper (has scope)
        str weaponName = weapon->getName();
        if (Q_stristr(weaponName.c_str(), "sniper") || 
            Q_stristr(weaponName.c_str(), "kar98") ||
            Q_stristr(weaponName.c_str(), "springfield") ||
            Q_stristr(weaponName.c_str(), "mosin")) {
            return 1200.0f; // Long range for snipers
        }
        return 800.0f; // Medium-long for regular rifles
    }
    
    // SMGs - close range is preferred
    if (weaponClass & WEAPON_CLASS_SMG) {
        return 400.0f;
    }
    
    // Machine guns - medium range
    if (weaponClass & WEAPON_CLASS_MG) {
        return 500.0f;
    }
    
    // Pistols - close range
    if (weaponClass & WEAPON_CLASS_PISTOL) {
        return 300.0f;
    }
    
    // Heavy weapons - medium-long
    if (weaponClass & WEAPON_CLASS_HEAVY) {
        return 700.0f;
    }
    
    return 600.0f; // Default medium range
}

/*
===============
BotTactics::ShouldMaintainDistance

Returns true if bot should keep distance (has long-range weapon, enemy too close).
===============
*/
bool BotTactics::ShouldMaintainDistance() const
{
    Player* self = m_controller->getControlledEntity();
    Sentient* enemy = m_controller->GetEnemy();
    
    if (!self || !enemy) return false;
    
    float preferredDist = GetPreferredCombatDistance();
    float currentDist = (enemy->origin - self->origin).length();
    
    // If we prefer long range and enemy is too close, back off
    if (preferredDist > 700.0f && currentDist < preferredDist * 0.6f) {
        return true;
    }
    
    return false;
}

/*
===============
BotTactics::ShouldCloseDistance

Returns true if bot should close in (has close-range weapon, enemy too far).
===============
*/
bool BotTactics::ShouldCloseDistance() const
{
    Player* self = m_controller->getControlledEntity();
    Sentient* enemy = m_controller->GetEnemy();
    
    if (!self || !enemy) return false;
    
    float preferredDist = GetPreferredCombatDistance();
    float currentDist = (enemy->origin - self->origin).length();
    
    // If we prefer close range and enemy is too far, close in
    if (preferredDist < 500.0f && currentDist > preferredDist * 1.5f) {
        return true;
    }
    
    return false;
}

/*
===============
BotTactics::FindNearestPickup

Finds the nearest pickup entity of a given class within range.
===============
*/
Entity* BotTactics::FindNearestPickup(const char* classname, float maxRange) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return nullptr;
    
    Entity* nearest = nullptr;
    float nearestDistSq = maxRange * maxRange;
    
    Entity* ent = nullptr;
    while ((ent = (Entity*)G_FindClass(ent, classname)) != nullptr) {
        // Skip if not in use
        if (!ent->edict->inuse) continue;
        // Skip items that are hidden (respawning)
        if (ent->hidden()) continue;
        
        float distSq = (ent->origin - self->origin).lengthSquared();
        if (distSq < nearestDistSq) {
            // Check if reachable (basic LOS check)
            trace_t trace = G_Trace(self->origin + Vector(0,0,32), vec_zero, vec_zero, 
                                    ent->origin + Vector(0,0,16), self, MASK_PLAYERSOLID, false, "FindNearestPickup");
            if (trace.fraction > 0.9f || trace.entityNum == ent->entnum) {
                nearestDistSq = distSq;
                nearest = ent;
            }
        }
    }
    
    return nearest;
}

/*
===============
BotTactics::FindNearestHealth

Finds the nearest health pickup within range.
===============
*/
Entity* BotTactics::FindNearestHealth(float maxRange) const
{
    return FindNearestPickup("Health", maxRange);
}

/*
===============
BotTactics::FindNearestAmmo

Finds the nearest ammo pickup within range.
===============
*/
Entity* BotTactics::FindNearestAmmo(float maxRange) const
{
    return FindNearestPickup("AmmoEntity", maxRange);
}

/*
===============
BotTactics::NeedsHealth

Returns true if bot health is low enough to seek health pickup.
===============
*/
bool BotTactics::NeedsHealth() const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    float healthPercent = (float)self->health / (float)self->max_health;
    return healthPercent < 0.5f; // Below 50% health
}

/*
===============
BotTactics::NeedsAmmo

Returns true if bot ammo is low enough to seek ammo pickup.
===============
*/
bool BotTactics::NeedsAmmo() const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    Weapon* weapon = self->GetActiveWeapon(WEAPON_MAIN);
    if (!weapon) return false;
    
    int clipAmmo = weapon->ClipAmmo(FIRE_PRIMARY);
    int maxClip = weapon->GetClipSize(FIRE_PRIMARY);
    int totalAmmo = self->AmmoCount(weapon->GetAmmoType(FIRE_PRIMARY));
    
    // Need ammo if clip is low and no reserve ammo
    if (maxClip > 0) {
        if (clipAmmo < maxClip / 4 && totalAmmo < maxClip) {
            return true;
        }
    }
    
    return false;
}

/*
===============
BotTactics::SeekHealthPickup

Attempts to move toward nearest health pickup.
Returns true if found and moving toward one.
===============
*/
bool BotTactics::SeekHealthPickup()
{
    // Don't scan too often
    if (level.inttime - m_lastPickupScan < 500) {
        // If already seeking, continue
        if (m_seekingHealth && m_targetPickup) {
            m_controller->GetMovement().MoveTo(m_targetPickup->origin);
            
            // Check if we're close enough to have picked it up
            float distSq = (m_controller->getControlledEntity()->origin - m_targetPickup->origin).lengthSquared();
            if (distSq < 100.0f * 100.0f) {
                m_seekingHealth = false;
                m_targetPickup = nullptr;
            }
            return true;
        }
        return m_seekingHealth;
    }
    m_lastPickupScan = level.inttime;
    
    Entity* health = FindNearestHealth(1500.0f);
    if (health) {
        m_targetPickup = health;
        m_seekingHealth = true;
        m_controller->GetMovement().MoveTo(health->origin);
        return true;
    }
    
    m_seekingHealth = false;
    return false;
}

/*
===============
BotTactics::SeekAmmoPickup

Attempts to move toward nearest ammo pickup.
Returns true if found and moving toward one.
===============
*/
bool BotTactics::SeekAmmoPickup()
{
    // Don't scan too often
    if (level.inttime - m_lastPickupScan < 500) {
        if (m_seekingAmmo && m_targetPickup) {
            m_controller->GetMovement().MoveTo(m_targetPickup->origin);
            
            float distSq = (m_controller->getControlledEntity()->origin - m_targetPickup->origin).lengthSquared();
            if (distSq < 100.0f * 100.0f) {
                m_seekingAmmo = false;
                m_targetPickup = nullptr;
            }
            return true;
        }
        return m_seekingAmmo;
    }
    m_lastPickupScan = level.inttime;
    
    Entity* ammo = FindNearestAmmo(1500.0f);
    if (ammo) {
        m_targetPickup = ammo;
        m_seekingAmmo = true;
        m_controller->GetMovement().MoveTo(ammo->origin);
        return true;
    }
    
    m_seekingAmmo = false;
    return false;
}

/*
===============
BotTactics::IsInOpenTerrain

Returns true if bot is in open terrain (no cover nearby).
===============
*/
bool BotTactics::IsInOpenTerrain() const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    // Check 8 directions for nearby walls/cover
    int hitCount = 0;
    float checkDist = 200.0f;
    
    for (int i = 0; i < 8; i++) {
        float angle = (float)i * 45.0f * M_PI / 180.0f;
        Vector dir(cos(angle), sin(angle), 0);
        Vector end = self->origin + dir * checkDist;
        
        trace_t trace = G_Trace(self->origin + Vector(0,0,32), vec_zero, vec_zero, 
                                end + Vector(0,0,32), self, MASK_SHOT, false, "IsInOpenTerrain");
        if (trace.fraction < 0.8f) {
            hitCount++;
        }
    }
    
    // If fewer than 2 directions blocked, we're in open terrain
    return hitCount < 2;
}

/*
===============
BotTactics::FindNextCoverToward

Finds the next cover position on the path toward a destination.
===============
*/
TacticalSpot BotTactics::FindNextCoverToward(const Vector& destination) const
{
    TacticalSpot result;
    result.valid = false;
    
    Player* self = m_controller->getControlledEntity();
    if (!self) return result;
    
    Vector toDestination = destination - self->origin;
    float totalDist = toDestination.length();
    if (totalDist < 100.0f) return result; // Already there
    
    toDestination.normalize();
    
    // Look for cover points along the path
    float checkDist = 300.0f; // How far ahead to look
    if (checkDist > totalDist) checkDist = totalDist * 0.8f;
    
    Vector checkPoint = self->origin + toDestination * checkDist;
    
    // Find cover near the check point, hiding from assumed enemy at destination
    Sentient* enemy = m_controller->GetEnemy();
    Vector threatPos = enemy ? enemy->origin : destination;
    
    TacticalSpot coverTest = TacticalAnalyzer::FindTacticalSpot(checkPoint, threatPos, 400.0f);
    if (coverTest.valid) {
        // Make sure the cover is generally toward our destination
        Vector toCover = coverTest.position - self->origin;
        float dot = toCover * toDestination;
        if (dot > 0) { // Cover is ahead of us
            result = coverTest;
        }
    }
    
    return result;
}

/*
===============
BotTactics::AdvanceThroughCover

Advances toward a destination using cover-to-cover movement.
Returns true if currently executing cover movement.
===============
*/
bool BotTactics::AdvanceThroughCover(const Vector& destination)
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    Sentient* enemy = m_controller->GetEnemy();
    
    // If no enemy, just move directly
    if (!enemy) {
        m_advancingThroughCover = false;
        return false;
    }
    
    // Check if we need to find new cover
    if (!m_advancingThroughCover || !m_nextCoverSpot.valid || 
        level.inttime - m_lastCoverAdvanceTime > 5000) {
        
        m_nextCoverSpot = FindNextCoverToward(destination);
        if (!m_nextCoverSpot.valid) {
            // No cover found, just use direct movement
            m_advancingThroughCover = false;
            return false;
        }
        
        m_advancingThroughCover = true;
        m_lastCoverAdvanceTime = level.inttime;
    }
    
    // Check if we've reached current cover spot
    float distToCover = (m_nextCoverSpot.position - self->origin).lengthSquared();
    if (distToCover < 100.0f * 100.0f) {
        // Reached cover, pause briefly then find next cover
        m_advancingThroughCover = false;
        m_inCover = true;
        m_currentCover = m_nextCoverSpot;
        return true;
    }
    
    // Move toward cover
    m_controller->GetMovement().MoveTo(m_nextCoverSpot.position);
    return true;
}

/*
===============================================================================
SOUND AWARENESS SYSTEM

Bots can hear sounds and investigate them tactically.
Higher skill bots have better hearing range and prioritize threats better.
===============================================================================
*/

/*
===============
BotTactics::GetSoundPriority

Returns a priority value for a sound type.
Higher priority sounds override lower priority sounds.
===============
*/
float BotTactics::GetSoundPriority(int eventType) const
{
    switch (eventType) {
    case AI_EVENT_EXPLOSION:
        return 1.0f;   // Highest priority - explosions
    case AI_EVENT_WEAPON_FIRE:
        return 0.9f;   // Gunfire is very important
    case AI_EVENT_GRENADE:
        return 0.85f;  // Grenade warnings
    case AI_EVENT_WEAPON_IMPACT:
        return 0.7f;   // Bullets hitting nearby
    case AI_EVENT_AMERICAN_URGENT:
    case AI_EVENT_GERMAN_URGENT:
        return 0.6f;   // Urgent voice callouts
    case AI_EVENT_AMERICAN_VOICE:
    case AI_EVENT_GERMAN_VOICE:
        return 0.4f;   // Normal voice
    case AI_EVENT_MISC_LOUD:
        return 0.3f;   // Loud misc sounds
    case AI_EVENT_FOOTSTEP:
        return 0.2f;   // Footsteps - lowest priority
    case AI_EVENT_MISC:
    default:
        return 0.1f;   // Very low priority
    }
}

/*
===============
BotTactics::ProcessSound

Processes a sound event and stores it in memory.
Returns true if the sound was noteworthy enough to remember.
===============
*/
bool BotTactics::ProcessSound(const Vector& pos, int eventType, int sourceEntNum)
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    float priority = GetSoundPriority(eventType);
    
    // Skill affects hearing sensitivity
    // Low skill bots may miss quiet sounds
    float skill = GetSkillLevel();
    float hearingChance = 0.5f + skill * 0.5f; // 50%-100% based on skill
    
    // Very low priority sounds (footsteps) are harder to hear
    if (priority < 0.3f && random() > hearingChance) {
        return false; // Low skill bot missed the sound
    }
    
    // Create sound memory entry
    SoundMemory newSound(pos, eventType, level.inttime, sourceEntNum, priority);
    
    // Check if we already have a higher priority sound to investigate
    bool foundSlot = false;
    for (auto& sound : m_soundMemory) {
        // Replace lower priority sounds or expired sounds
        if (sound.priority < priority || level.inttime - sound.heardTime > SOUND_MEMORY_DURATION) {
            sound = newSound;
            foundSlot = true;
            break;
        }
    }
    
    // If no slot found, add if we have room
    if (!foundSlot && (int)m_soundMemory.size() < MAX_SOUND_MEMORY) {
        m_soundMemory.push_back(newSound);
    }
    
    return true;
}

/*
===============
BotTactics::GetMostImportantSound

Returns the highest priority unexpired sound in memory.
===============
*/
SoundMemory BotTactics::GetMostImportantSound() const
{
    SoundMemory best;
    
    for (const auto& sound : m_soundMemory) {
        // Skip expired sounds
        if (level.inttime - sound.heardTime > SOUND_MEMORY_DURATION) {
            continue;
        }
        
        if (sound.priority > best.priority) {
            best = sound;
        }
    }
    
    return best;
}

/*
===============
BotTactics::ShouldInvestigateSound

Returns true if the bot should investigate a sound
(no current visual enemy, has noteworthy sound in memory)
===============
*/
bool BotTactics::ShouldInvestigateSound() const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    // If we have a current enemy that we can see, don't investigate sounds
    Sentient* enemy = m_controller->GetEnemy();
    if (enemy && self->CanSee(enemy, 80, 2048, false)) {
        return false;
    }
    
    // Check if we have any noteworthy sounds
    SoundMemory best = GetMostImportantSound();
    if (best.priority < 0.15f) {
        return false; // No sounds worth investigating
    }
    
    // Higher skill = more likely to investigate
    float skill = GetSkillLevel();
    float investigateChance = 0.4f + skill * 0.5f; // 40%-90% based on skill
    
    // High priority sounds always investigated
    if (best.priority >= 0.8f) {
        return true;
    }
    
    return random() < investigateChance;
}

/*
===============
BotTactics::FindCoverTowardSound

Finds a cover position that allows approaching a sound source safely.
===============
*/
TacticalSpot BotTactics::FindCoverTowardSound(const Vector& soundPos) const
{
    return FindNextCoverToward(soundPos);
}

/*
===============
BotTactics::ClearOldSounds

Removes expired sounds from memory.
===============
*/
void BotTactics::ClearOldSounds()
{
    // Only cleanup periodically
    if (level.inttime - m_lastSoundCleanup < TIMING_SOUND_CLEANUP) {
        return;
    }
    m_lastSoundCleanup = level.inttime;
    
    // Remove expired sounds
    auto it = m_soundMemory.begin();
    while (it != m_soundMemory.end()) {
        if (level.inttime - it->heardTime > SOUND_MEMORY_DURATION) {
            it = m_soundMemory.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clear investigation state if sound expired
    if (m_isInvestigatingSound && 
        level.inttime - m_investigatingSound.heardTime > SOUND_MEMORY_DURATION) {
        m_isInvestigatingSound = false;
    }
}

/*
===============
HasWallToSide

Checks if there's a wall within maxDist to the left or right of the bot.
Returns true if wall found, and sets outDist to actual distance.
===============
*/
bool BotTactics::HasWallToSide(bool checkRight, float maxDist, float& outDist) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    Vector forward, right, up;
    self->angles.AngleVectors(&forward, &right, &up);
    
    // Check side direction
    Vector sideDir = checkRight ? right : (right * -1.0f);
    
    Vector start = self->origin + Vector(0, 0, self->viewheight * 0.5f);
    Vector end = start + sideDir * maxDist;
    
    trace_t trace = G_Trace(start, vec_zero, vec_zero, end, self, MASK_SOLID, false, "HasWallToSide");
    
    if (trace.fraction < 1.0f) {
        outDist = trace.fraction * maxDist;
        return true;
    }
    
    outDist = maxDist;
    return false;
}

/*
===============
CanSeeAroundWall

Checks if leaning in a direction would allow seeing the target.
Used to determine if leaning would be beneficial.
===============
*/
bool BotTactics::CanSeeAroundWall(bool leanRight, const Vector& targetPos) const
{
    Player* self = m_controller->getControlledEntity();
    if (!self) return false;
    
    Vector forward, right, up;
    self->angles.AngleVectors(&forward, &right, &up);
    
    // Simulate lean offset (about 16 units sideways, typical lean amount)
    float leanOffset = 16.0f;
    Vector leanDir = leanRight ? right : (right * -1.0f);
    
    Vector leanedEyePos = self->origin + Vector(0, 0, self->viewheight) + leanDir * leanOffset;
    
    // Check if we can see target from leaned position
    trace_t trace = G_Trace(leanedEyePos, vec_zero, vec_zero, targetPos, self, MASK_OPAQUE, false, "CanSeeAroundWall");
    
    return (trace.fraction >= 0.95f);
}

/*
===============
GetCombatLeanDirection

Determines ideal lean direction during combat based on:
1. Wall proximity - lean away from nearby walls
2. Enemy position - lean to expose minimum profile while maintaining sight
3. Corner detection - lean around corners to peek

Returns PEEK_NONE if leaning is not beneficial.
===============
*/
PeekDirection BotTactics::GetCombatLeanDirection() const
{
    Player* self = m_controller->getControlledEntity();
    Sentient* enemy = m_controller->GetEnemy();
    
    if (!self || !enemy) return PEEK_NONE;
    
    // Only lean if stationary or moving slowly (leaning while running looks bad)
    if (self->velocity.lengthSquared() > 100.0f * 100.0f) {
        return PEEK_NONE;
    }
    
    float leftDist = 0, rightDist = 0;
    bool hasWallLeft = HasWallToSide(false, 64.0f, leftDist);
    bool hasWallRight = HasWallToSide(true, 64.0f, rightDist);
    
    // If we have a wall on one side, lean toward it (use it as cover)
    // This exposes less body while still being able to shoot
    if (hasWallLeft && !hasWallRight && leftDist < 48.0f) {
        // Wall on left - lean left to use it as cover
        if (CanSeeAroundWall(false, enemy->origin)) {
            return PEEK_LEFT;
        }
    }
    
    if (hasWallRight && !hasWallLeft && rightDist < 48.0f) {
        // Wall on right - lean right to use it as cover
        if (CanSeeAroundWall(true, enemy->origin)) {
            return PEEK_RIGHT;
        }
    }
    
    // Corner detection: Check if we're at a corner and should lean to peek
    // This is when wall is close on one side and we can see enemy by leaning other way
    if (hasWallLeft && leftDist < 32.0f) {
        if (!CanSeeAroundWall(false, enemy->origin) && CanSeeAroundWall(true, enemy->origin)) {
            return PEEK_RIGHT;
        }
    }
    
    if (hasWallRight && rightDist < 32.0f) {
        if (!CanSeeAroundWall(true, enemy->origin) && CanSeeAroundWall(false, enemy->origin)) {
            return PEEK_LEFT;
        }
    }
    
    // Skill-based random lean for dodging (higher skill = more likely to lean during combat)
    float skill = GetSkillLevel();
    if (skill > 0.5f && (rand() % 1000) < (int)(skill * 30)) {
        // Random tactical lean to reduce profile
        return (rand() % 2 == 0) ? PEEK_LEFT : PEEK_RIGHT;
    }
    
    return PEEK_NONE;
}

/*
===============
ApplyCombatLean

Applies lean buttons during combat based on tactical situation.
Should be called from Update() when in combat.
===============
*/
void BotTactics::ApplyCombatLean(usercmd_t* cmd)
{
    if (!cmd) return;
    
    // Don't apply combat lean if already in cover peeking (behavior tree handles that)
    if (m_inCover && m_currentPeek != PEEK_NONE) {
        return;
    }
    
    // Check for combat lean opportunity
    PeekDirection leanDir = GetCombatLeanDirection();
    
    // Apply lean with more stability to avoid flickering
    // Only change lean direction every 500ms minimum
    static int lastLeanChangeTime = 0;
    static PeekDirection lastLeanDir = PEEK_NONE;
    
    if (level.inttime - lastLeanChangeTime > 500 || leanDir == lastLeanDir) {
        if (leanDir != lastLeanDir) {
            lastLeanDir = leanDir;
            lastLeanChangeTime = level.inttime;
        }
        
        switch (leanDir) {
            case PEEK_LEFT:
                cmd->buttons |= BUTTON_LEAN_LEFT;
                cmd->buttons &= ~BUTTON_LEAN_RIGHT;
                break;
            case PEEK_RIGHT:
                cmd->buttons |= BUTTON_LEAN_RIGHT;
                cmd->buttons &= ~BUTTON_LEAN_LEFT;
                break;
            default:
                // Clear lean buttons if not leaning
                cmd->buttons &= ~(BUTTON_LEAN_LEFT | BUTTON_LEAN_RIGHT);
                break;
        }
    }
}

/*
===============
BotTactics::CheckMoveSafety

Prevents the bot from walking into walls when using direct movement commands.
Traces in the direction of intended movement and stops if blocked.
===============
*/
void BotTactics::CheckMoveSafety(usercmd_t* cmd)
{
    if (!cmd || (cmd->forwardmove == 0 && cmd->rightmove == 0)) return;
    
    Player* self = m_controller->getControlledEntity();
    if (!self) return;
    
    // Calculate wish direction relative to bot angles
    Vector forward, right;
    self->angles.AngleVectors(&forward, &right, NULL);
    
    // Normalize moves to -1.0 to 1.0 range for vector math
    float fwd = (float)cmd->forwardmove / 127.0f;
    float rt = (float)cmd->rightmove / 127.0f;
    
    Vector moveDir = forward * fwd + right * rt;
    if (moveDir.lengthSquared() < 0.01f) return;
    moveDir.normalize();
    
    // Trace ahead
    // We trace from slightly elevated position to avoid ground clutter (stairs)
    Vector start = self->origin + Vector(0, 0, 18); 
    Vector end = start + moveDir * 64.0f; // Check 64 units ahead
    
    trace_t trace = G_Trace(start, self->mins, self->maxs, end, self, MASK_PLAYERSOLID, false, "CheckMoveSafety");
    
    if (trace.fraction < 1.0f && !trace.startsolid) {
        // We hit something. Stop movement.
        // Check if it's a step we can move up (handled by physics generally, but direct move might fight it)
        // If the normal is verticalish, it's floor/slope.
        if (trace.plane.normal[2] > 0.7f) return; 
        
        // It's a wall.
        cmd->forwardmove = 0;
        cmd->rightmove = 0;
    }
}
