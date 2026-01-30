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

struct TeammateMemory {
    Vector position;
    int entnum;
    bool isHuman;

    TeammateMemory() : position(vec_zero), entnum(-1), isHuman(false) {}
};

class TacticalAnalyzer {
public:
    // Finds a spot near origin that offers cover from enemyPos
    static TacticalSpot FindTacticalSpot(const Vector& origin, const Vector& enemyPos, float radius);

    // Checks if the current position allows leaning out to see target
    static bool CanPeek(const Vector& origin, const Vector& target, PeekDirection dir);
};

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

    BotController* m_controller;
    std::shared_ptr<BTNode> m_root;

    // Blackboard/Memory
    TacticalSpot m_currentCover;
    bool m_inCover;
    int m_stateTimer;
    PeekDirection m_currentPeek;
    int m_grenadeCooldown;
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

    // Phase 4: Tactical Polish
    int m_lastProjectileScan;
    std::map<int, Vector> m_projectileMemory;
    int m_suppressionEndTime;
    Vector m_suppressionPos;
    int m_lastJumpTime;

    bool m_isProne;

    // Phase 4: Orders
    int m_currentOrder;
    SafePtr<Entity> m_orderTarget;
    int m_orderTime;
};
