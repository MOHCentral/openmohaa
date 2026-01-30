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
#include <vector>
#include <memory>
#include <functional>

class BotController;
class RecastPather;

enum CoverType {
    COVER_NONE,
    COVER_FULL,
    COVER_LOW
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

private:
    void BuildTree();
    Vector GetObjectivePosition();

    BotController* m_controller;
    std::shared_ptr<BTNode> m_root;

    // Blackboard/Memory
    TacticalSpot m_currentCover;
    bool m_inCover;
    int m_stateTimer;
    PeekDirection m_currentPeek;
    int m_grenadeCooldown;
};
