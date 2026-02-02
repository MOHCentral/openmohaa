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

#pragma once

#include "g_local.h"
#include "../corepp/vector.h"
#include "entity.h"
#include <vector>
#include <memory>
#include <functional>
#include <map>

class BotController;
class RecastPather;
class Weapon;

enum CoverType {
    COVER_NONE,
    COVER_FULL,
    COVER_LOW
};

enum BotOrder {
    ORDER_NONE,
    ORDER_FOLLOW,
    ORDER_HOLD,
    ORDER_ATTACK,
    ORDER_REPORT
};

enum GrenadeThrowMode {
    GRENADE_THROW_HIGH,    // Standard arc throw
    GRENADE_THROW_LOW,     // Roll/low toss
    GRENADE_THROW_NONE     // Cannot throw
};

// Grenade decision result
struct GrenadeDecision {
    bool        canThrow;
    Vector      velocity;
    GrenadeThrowMode mode;
    int         enemyClusterSize;  // Number of enemies near target
    float       distanceToTarget;
    bool        teammateInBlastRadius;
    
    GrenadeDecision() 
        : canThrow(false)
        , velocity(vec_zero)
        , mode(GRENADE_THROW_NONE)
        , enemyClusterSize(0)
        , distanceToTarget(0)
        , teammateInBlastRadius(false) 
    {}
};

enum PeekDirection {
    PEEK_NONE,
    PEEK_LEFT,
    PEEK_RIGHT,
    PEEK_OVER // For crouching/standing
};

struct TacticalSpot {
    Vector position;
    CoverType type;
    PeekDirection peekDir;
    bool valid;

    TacticalSpot() : position(vec_zero), type(COVER_NONE), peekDir(PEEK_NONE), valid(false) {}
};

struct EnemyMemory {
    Vector lastPosition;
    Vector velocity;
    int lastSeenTime;
    bool currentlyVisible;

    EnemyMemory() : lastPosition(vec_zero), velocity(vec_zero), lastSeenTime(0), currentlyVisible(false) {}
};

// Sound awareness memory
struct SoundMemory {
    Vector position;          // Where sound originated
    int eventType;            // AI_EVENT_* type
    int heardTime;            // When we heard it
    int sourceEntNum;         // Entity number of sound source (-1 if unknown)
    float priority;           // Calculated priority for this sound
    
    SoundMemory() 
        : position(vec_zero)
        , eventType(0)
        , heardTime(0)
        , sourceEntNum(-1)
        , priority(0.0f)
    {}
    
    SoundMemory(const Vector& pos, int type, int time, int entNum, float prio)
        : position(pos)
        , eventType(type)
        , heardTime(time)
        , sourceEntNum(entNum)
        , priority(prio)
    {}
};

// Squad coordination roles
enum SquadRole {
    SQUAD_ROLE_NONE,
    SQUAD_ROLE_ASSAULT,     // Direct engagement
    SQUAD_ROLE_FLANK_LEFT,  // Moving to left flank
    SQUAD_ROLE_FLANK_RIGHT, // Moving to right flank
    SQUAD_ROLE_SUPPRESS     // Providing cover fire
};

struct TeammateMemory {
    Vector position;
    int entnum;
    bool isHuman;
    SquadRole role;          // Current role in coordinated attack
    int roleAssignedTime;    // When role was assigned

    TeammateMemory() : position(vec_zero), entnum(-1), isHuman(false), role(SQUAD_ROLE_NONE), roleAssignedTime(0) {}
};

class TacticalAnalyzer {
public:
    // Finds a spot near origin that offers cover from enemyPos
    static TacticalSpot FindTacticalSpot(const Vector& origin, const Vector& enemyPos, float radius);

    // Checks if the current position allows leaning out to see target
    static bool CanPeek(const Vector& origin, const Vector& target, PeekDirection dir);
};

// Constants for grenade logic
static const float GRENADE_BLAST_RADIUS = 256.0f;
static const float GRENADE_MIN_THROW_DIST = 400.0f;  // Don't throw closer than this
static const float GRENADE_MAX_THROW_DIST = 1200.0f; // Max effective throw distance
static const float GRENADE_CLUSTER_RADIUS = 300.0f;  // Enemies within this count as cluster
static const int   GRENADE_COOLDOWN_MS = 8000;       // 8 seconds between throws
static const float GRENADE_GRAVITY_MULT = 0.8f;      // Grenade gravity multiplier

//
// Behavior Tree System
//

struct BTContext {
    BotController* controller;
    usercmd_t* cmd;

    BTContext(BotController* c, usercmd_t* u) : controller(c), cmd(u) {}
};

enum BTStatus {
    BT_SUCCESS,
    BT_FAILURE,
    BT_RUNNING
};

class BTNode {
public:
    virtual ~BTNode() = default;
    virtual BTStatus Tick(BTContext& ctx) = 0;
};

class BTComposite : public BTNode {
protected:
    std::vector<std::shared_ptr<BTNode>> children;
public:
    void AddChild(std::shared_ptr<BTNode> child) { children.push_back(child); }
};

class BTSelector : public BTComposite {
public:
    BTStatus Tick(BTContext& ctx) override;
};

class BTSequence : public BTComposite {
public:
    BTStatus Tick(BTContext& ctx) override;
};

class BTLeaf : public BTNode {
    std::function<BTStatus(BTContext&)> action;
public:
    BTLeaf(std::function<BTStatus(BTContext&)> a) : action(a) {}
    BTStatus Tick(BTContext& ctx) override { return action(ctx); }
};

class BotTactics {
public:
    BotTactics();
    ~BotTactics();

    void Init(BotController* controller);
    void Update(usercmd_t* cmd);
    void SetOrder(int orderType, Entity* target);
    Vector GetPredictedPos() const { return m_predictedPos; }
    
    // Grenade tactical functions
    GrenadeDecision EvaluateGrenadeThrow(const Vector& targetPos);
    bool IsGrenadeReady() const;
    bool ThrowGrenade(const GrenadeDecision& decision);
    
    // Flee from grenade
    bool ShouldFleeFromGrenade(Vector& outFleeDir, float& outDanger) const;
    
    // Sound awareness and investigation (public for BotController access)
    float GetSoundPriority(int eventType) const;
    bool ProcessSound(const Vector& pos, int eventType, int sourceEntNum);
    SoundMemory GetMostImportantSound() const;
    bool ShouldInvestigateSound() const;
    TacticalSpot FindCoverTowardSound(const Vector& soundPos) const;
    void ClearOldSounds();
    
    // Cover-to-cover movement (public for State_Curious access)
    bool AdvanceThroughCover(const Vector& destination);

private:
    void BuildTree();
    Vector GetObjectivePosition();
    void UpdatePrediction();
    void ScanEnemies();
    void ScanTeammates();
    void UpdateWeaponSelection();
    Vector FindFlankPos(const Vector& selfPos, const Vector& enemyPos);
    Player* FindLeader();
    void ScanProjectiles();
    
    // Grenade trajectory helpers (adapted from Actor)
    static Vector CalcThrowVelocity(const Vector& vFrom, const Vector& vTo);
    static Vector CalcRollVelocity(const Vector& vFrom, const Vector& vTo);
    bool ValidGrenadePath(const Vector& vFrom, const Vector& vTo, const Vector& vVel) const;
    int CountEnemiesInRadius(const Vector& pos, float radius) const;
    bool IsTeammateInRadius(const Vector& pos, float radius) const;
    
    // Skill system helpers
    float GetSkillLevel() const;
    float GetSkillAdjusted(float minVal, float maxVal, bool invertForHarder = false) const;
    
    // Room clearing detection
    bool IsTargetInRoom(const Vector& targetPos) const;
    bool IsSelfInRoom(const Vector& targetPos) const;
    
    // Smoke grenade detection and usage
    bool HasSmokeGrenade() const;
    bool IsSmokeGrenade(Weapon* weapon) const;
    Weapon* GetSmokeGrenade() const;
    bool ThrowSmokeForCover(const Vector& targetPos);
    bool ShouldUseSmokeGrenade() const;
    
    // Weapon-specific combat behavior
    float GetPreferredCombatDistance() const;
    bool ShouldMaintainDistance() const;
    bool ShouldCloseDistance() const;
    
    // Pickup awareness and health management
    Entity* FindNearestPickup(const char* classname, float maxRange) const;
    Entity* FindNearestHealth(float maxRange) const;
    Entity* FindNearestAmmo(float maxRange) const;
    bool NeedsHealth() const;
    bool NeedsAmmo() const;
    bool SeekHealthPickup();
    bool SeekAmmoPickup();
    
    // Cover-to-cover movement (private helpers)
    TacticalSpot FindNextCoverToward(const Vector& destination) const;
    bool IsInOpenTerrain() const;
    
    // Squad coordination
    void CoordinateSquadAttack();
    SquadRole DetermineMyRole() const;
    Vector GetFlankPosition(bool rightFlank) const;

    BotController* m_controller;
    std::shared_ptr<BTNode> m_root;

    // Blackboard/Memory
    TacticalSpot m_currentCover;
    bool m_inCover;
    int m_stateTimer;
    PeekDirection m_currentPeek;
    int m_grenadeCooldown;
    int m_smokeCooldown;      // Separate cooldown for smoke grenades
    Vector m_cachedObjectivePos;
    int m_objectiveCacheTime;
    int m_lastDuckAndLeanTime;

    // Phase 1: Burst Fire Control
    bool m_isFiring;
    int m_burstStartTime;
    int m_burstEndTime;
    int m_burstCooldownEnd;
    int m_burstLength;        // Current burst duration (ms)
    int m_burstCooldown;      // Time between bursts (ms)

    // Phase 1: Strafe Dodging
    int m_strafeDirection;    // -1 = left, 0 = none, 1 = right
    int m_strafeChangeTime;
    int m_nextStrafeChange;

    // Phase 1: Retreat Behavior
    bool m_isRetreating;
    int m_retreatStartTime;
    float m_healthThreshold;  // Health percentage to trigger retreat

    // Phase 1: Reload Timing
    bool m_needsReload;
    int m_lastReloadCheck;

    // Phase 2: Threat Memory
    std::map<int, EnemyMemory> m_enemyMemory;
    int m_lastMemoryUpdate;

    // Phase 2: Lead Target Prediction
    Vector m_lastEnemyVel;
    Vector m_predictedPos;

    // Phase 2: Weapon Selection
    int m_lastWeaponSelectionTime;

    // Phase 3: Team Coordination
    std::map<int, TeammateMemory> m_teammateMemory;
    int m_lastTeammateScan;

    // Phase 3: Flanking
    bool m_isFlanking;
    Vector m_flankPos;
    int m_lastFlankTime;
    
    // Phase 3: Squad Coordination
    SquadRole m_mySquadRole;
    int m_squadRoleAssignedTime;
    int m_lastSquadCoordinationTime;
    Vector m_sharedEnemyPos;  // Enemy position shared by teammates

    // Phase 4: Tactical Polish
    int m_lastProjectileScan;
    std::map<int, Vector> m_projectileMemory;
    int m_suppressionEndTime;
    Vector m_suppressionPos;
    int m_lastJumpTime;
    
    // Enhanced suppressive fire (last known enemy position)
    Vector m_lastKnownEnemyPos;
    int m_lastKnownEnemyTime;
    bool m_isSuppressing;
    
    // Voice callout cooldowns
    int m_lastEnemySpottedCallout;
    int m_lastReloadingCallout;
    int m_lastCoveringCallout;

    bool m_isProne;

    // Phase 4: Orders
    int m_currentOrder;
    SafePtr<Entity> m_orderTarget;
    int m_orderTime;
    
    // Grenade tactical state
    int m_lastGrenadeThrowTime;      // For cooldown tracking
    Vector m_pendingGrenadeTarget;   // Where we're aiming
    bool m_grenadeInFlight;          // Did we just throw?
    int m_grenadeThrowStartTime;     // When we started throw animation
    
    // Pickup seeking state
    SafePtr<Entity> m_targetPickup;  // Current pickup we're seeking
    int m_lastPickupScan;            // Last time we scanned for pickups
    bool m_seekingHealth;            // Currently going for health
    bool m_seekingAmmo;              // Currently going for ammo
    
    // Cover-to-cover movement
    TacticalSpot m_nextCoverSpot;    // Next cover to advance to
    bool m_advancingThroughCover;    // Currently doing cover-to-cover
    int m_lastCoverAdvanceTime;      // Last time we advanced covers
    
    // Sound awareness
    std::vector<SoundMemory> m_soundMemory;  // Recent sounds heard
    SoundMemory m_investigatingSound;        // Current sound being investigated
    bool m_isInvestigatingSound;             // Whether we're in investigation mode
    int m_lastSoundCleanup;                  // Last time we cleared old sounds
    static const int MAX_SOUND_MEMORY = 5;   // Max sounds to track
    static const int SOUND_MEMORY_DURATION = 15000;  // 15 seconds sound memory
};
