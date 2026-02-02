/*
===========================================================================
Copyright (C) 2024 the OpenMoHAA team

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
// playerbot.cpp: Multiplayer bot system.
//
// FIXME: Refactor code and use OOP-based state system

#include "g_local.h"
#include "actor.h"
#include "playerbot.h"
#include "consoleevent.h"
#include "debuglines.h"
#include "scriptexception.h"
#include "vehicleturret.h"
#include "weaputils.h"
#include "windows.h"
#include "g_bot.h"
#include "g_scriptevents.h"

// We assume that we have limited access to the server-side
// and that most logic come from the playerstate_s structure

cvar_t *bot_manualmove;

CLASS_DECLARATION(Listener, BotController, NULL) {
    {NULL, NULL}
};

BotController::botfunc_t BotController::botfuncs[MAX_BOT_FUNCTIONS];

BotController::BotController()
{
    if (LoadingSavegame) {
        return;
    }

    m_botCmd.serverTime = 0;
    m_botCmd.msec       = 0;
    m_botCmd.buttons    = 0;
    m_botCmd.angles[0]  = ANGLE2SHORT(0);
    m_botCmd.angles[1]  = ANGLE2SHORT(0);
    m_botCmd.angles[2]  = ANGLE2SHORT(0);

    m_botCmd.forwardmove = 0;
    m_botCmd.rightmove   = 0;
    m_botCmd.upmove      = 0;

    m_botEyes.angles[0] = 0;
    m_botEyes.angles[1] = 0;
    m_botEyes.ofs[0]    = 0;
    m_botEyes.ofs[1]    = 0;
    m_botEyes.ofs[2]    = DEFAULT_VIEWHEIGHT;

    m_iCuriousTime        = 0;
    m_iCuriousEventType   = AI_EVENT_NONE;
    m_iAttackTime         = 0;
    m_iEnemyEyesTag       = -1;
    m_iContinuousFireTime = 0;
    m_iLastSeenTime       = 0;
    m_iLastUnseenTime     = 0;
    m_iLastBurstTime      = 0;

    m_iNextTauntTime = 0;
    
    // Grenade state initialization
    m_vGrenadeFleeDir = vec_zero;
    m_fGrenadeDanger = 0;
    m_iGrenadeFleeStartTime = 0;
    
    // Tactical state initialization
    m_iTacticalStateTime = 0;
    m_iTacticalMode = 0;
    
    m_tactics.Init(this);

    m_StateFlags = 0;
}

BotController::~BotController()
{
    if (controlledEnt) {
        controlledEnt->delegate_gotKill.Remove(delegateHandle_gotKill);
        controlledEnt->delegate_killed.Remove(delegateHandle_killed);
        controlledEnt->delegate_stufftext.Remove(delegateHandle_stufftext);
        controlledEnt->delegate_spawned.Remove(delegateHandle_spawned);
        controlledEnt->delegate_pain.Remove(delegateHandle_pain);
    }
}

BotMovement& BotController::GetMovement()
{
    return movement;
}

void BotController::Init(void)
{
    bot_manualmove = gi.Cvar_Get("bot_manualmove", "0", 0);

    for (int i = 0; i < MAX_BOT_FUNCTIONS; i++) {
        botfuncs[i].BeginState = &BotController::State_DefaultBegin;
        botfuncs[i].EndState   = &BotController::State_DefaultEnd;
    }

    InitState_Attack(&botfuncs[0]);
    InitState_Curious(&botfuncs[1]);
    InitState_Grenade(&botfuncs[2]);
    InitState_Idle(&botfuncs[3]);
    InitState_Tactical(&botfuncs[4]);
}

void BotController::GetUsercmd(usercmd_t *ucmd)
{
    *ucmd = m_botCmd;
}

void BotController::GetEyeInfo(usereyes_t *eyeinfo)
{
    *eyeinfo = m_botEyes;
}

void BotController::UpdateBotStates(void)
{
    if (bot_manualmove->integer) {
        memset(&m_botCmd, 0, sizeof(usercmd_t));
        return;
    }

    m_botCmd.serverTime = level.svsTime;

    if (!controlledEnt->client->pers.dm_primary[0]) {
        Event *event;

        //
        // Primary weapon
        //
        event = new Event(EV_Player_PrimaryDMWeapon);
        event->AddString("auto");

        controlledEnt->ProcessEvent(event);
    }

    if (controlledEnt->GetTeam() == TEAM_NONE || controlledEnt->GetTeam() == TEAM_SPECTATOR) {
        float time;

        // Add some delay to avoid telefragging
        time = controlledEnt->entnum / 20.0;

        if (controlledEnt->EventPending(EV_Player_AutoJoinDMTeam)) {
            return;
        }

        //
        // Team
        //
        controlledEnt->PostEvent(EV_Player_AutoJoinDMTeam, time);
        return;
    }

    if (controlledEnt->IsDead() || controlledEnt->IsSpectator()) {
        // The bot should respawn
        m_botCmd.buttons ^= BUTTON_ATTACKLEFT;
        return;
    }

    m_botCmd.buttons |= BUTTON_RUN;

    m_botEyes.ofs[0]    = 0;
    m_botEyes.ofs[1]    = 0;
    m_botEyes.ofs[2]    = controlledEnt->viewheight;
    m_botEyes.angles[0] = 0;
    m_botEyes.angles[1] = 0;

    CheckStates();

    movement.MoveThink(m_botCmd);
    rotation.TurnThink(m_botCmd, m_botEyes);
    CheckUse();

    CheckValidWeapon();
}

void BotController::CheckUse(void)
{
    Vector  dir;
    Vector  start;
    Vector  end;
    trace_t trace;

    if (controlledEnt->GetLadder()) {
        return;
    }

    controlledEnt->angles.AngleVectorsLeft(&dir);

    start = controlledEnt->origin + Vector(0, 0, controlledEnt->viewheight);
    end   = controlledEnt->origin + Vector(0, 0, controlledEnt->viewheight) + dir * 64;

    trace = G_Trace(
        start, vec_zero, vec_zero, end, controlledEnt, MASK_USABLE | MASK_LADDER, false, "BotController::CheckUse"
    );

    if (!trace.ent || trace.ent->entity == world) {
        m_botCmd.buttons &= ~BUTTON_USE;
        return;
    }

    if (trace.ent->entity->IsSubclassOfDoor()) {
        Door *door = static_cast<Door *>(trace.ent->entity);
        if (door->isOpen()) {
            // Don't use an open door
            m_botCmd.buttons &= ~BUTTON_USE;
            return;
        }
    } else if (!trace.ent->entity->isSubclassOf(FuncLadder)) {
        m_botCmd.buttons &= ~BUTTON_USE;
        return;
    }

    //
    // Toggle the use button
    //
    m_botCmd.buttons ^= BUTTON_USE;

#if 0
    Vector  forward;
    Vector  start, end;

    AngleVectors(controlledEnt->GetViewAngles(), forward, NULL, NULL);

    start = (controlledEnt->m_vViewPos - forward * 12.0f);
    end   = (controlledEnt->m_vViewPos + forward * 128.0f);

    trace = G_Trace(start, vec_zero, vec_zero, end, controlledEnt, MASK_LADDER, qfalse, "checkladder");
    if (trace.ent->entity && trace.ent->entity->isSubclassOf(FuncLadder)) {
        return;
    }

    m_botCmd.buttons ^= BUTTON_USE;
#endif
}

bool BotController::CheckWindows(void)
{
    trace_t trace;
    Vector  start, end;
    Vector  dir;

    controlledEnt->angles.AngleVectorsLeft(&dir);
    start = controlledEnt->origin + Vector(0, 0, controlledEnt->viewheight);
    end   = controlledEnt->origin + Vector(0, 0, controlledEnt->viewheight) + dir * 64;

    trace = G_Trace(start, vec_zero, vec_zero, end, controlledEnt, MASK_PLAYERSOLID, false, "BotController::CheckUse");

    if (trace.fraction != 1 && trace.ent) {
        if (trace.ent->entity->isSubclassOf(WindowObject)) {
            return true;
        }
    }

    return false;
}

void BotController::CheckValidWeapon()
{
    Weapon *weapon = controlledEnt->GetActiveWeapon(WEAPON_MAIN);
    if (!weapon) {
        // If holstered, use the best weapon available
        UseWeaponWithAmmo();
    } else if (!weapon->HasAmmo(FIRE_PRIMARY) && !controlledEnt->GetNewActiveWeapon()) {
        // In case the current weapon has no ammo, use the best available weapon
        UseWeaponWithAmmo();
    }
}

void BotController::SendCommand(const char *text)
{
    char        *buffer;
    char        *data;
    size_t       len;
    ConsoleEvent ev;

    len = strlen(text) + 1;

    buffer = (char *)gi.Malloc(len);
    data   = buffer;
    Q_strncpyz(data, text, len);

    const char *com_token = COM_Parse(&data);

    if (!com_token) {
        return;
    }

    controlledEnt->m_lastcommand = com_token;

    if (!Event::GetEvent(com_token)) {
        return;
    }

    ev = ConsoleEvent(com_token);

    if (!(ev.GetEventFlags(ev.eventnum) & EV_CONSOLE)) {
        gi.Free(buffer);
        return;
    }

    ev.SetConsoleEdict(controlledEnt->edict);

    while (1) {
        com_token = COM_Parse(&data);

        if (!com_token || !*com_token) {
            break;
        }

        ev.AddString(com_token);
    }

    gi.Free(buffer);

    try {
        controlledEnt->ProcessEvent(ev);
    } catch (ScriptException& exc) {
        gi.DPrintf("*** Bot Command Exception *** %s\n", exc.string.c_str());
    }
}

/*
====================
AimAtAimNode

Make the bot face toward the current path
====================
*/
void BotController::AimAtAimNode(void)
{
    Vector goal;

    if (!movement.IsMoving()) {
        return;
    }

    //goal = movement.GetCurrentGoal();
    //if (goal != controlledEnt->origin) {
    //    rotation.AimAt(goal);
    //}

    if (controlledEnt->GetLadder()) {
        Vector vAngles = movement.GetCurrentPathDirection().toAngles();
        vAngles.x      = Q_clamp_float(vAngles.x, -80, 80);

        rotation.SetTargetAngles(vAngles);
        return;
    } else {
        Vector targetAngles;
        targetAngles   = movement.GetCurrentPathDirection().toAngles();
        targetAngles.x = 0;
        rotation.SetTargetAngles(targetAngles);
    }
}

/*
====================
CheckReload

Make the bot reload if necessary
====================
*/
void BotController::CheckReload(void)
{
    Weapon *weap;

    if (level.inttime < m_iLastFireTime + 2000) {
        // Don't reload while attacking
        return;
    }

    weap = controlledEnt->GetActiveWeapon(WEAPON_MAIN);

    if (weap && weap->CheckReload(FIRE_PRIMARY)) {
        SendCommand("reload");
    }
}

/*
====================
NoticeEvent

Warn the bot of an event. Enhanced with skill-based hearing
and tactical sound awareness.
====================
*/
void BotController::NoticeEvent(Vector vPos, int iType, Entity *pEnt, float fDistanceSquared, float fRadiusSquared)
{
    Sentient *pSentOwner;
    float     fRangeFactor;
    Vector    delta1, delta2;

    // Apply skill-based hearing sensitivity
    // Higher skill = better hearing at range
    float skill = g_bot_skill->value;
    float hearingBonus = 1.0f + skill * 0.5f; // 1.0x to 1.5x effective hearing range
    float effectiveRadiusSq = fRadiusSquared * hearingBonus * hearingBonus;

    if (m_iCuriousTime) {
        delta1 = vPos - controlledEnt->origin;
        delta2 = m_vNewCuriousPos - controlledEnt->origin;
        // Only skip if new sound is further AND lower priority
        if (delta1.lengthSquared() > delta2.lengthSquared()) {
            // Check if new event is higher priority
            float newPriority = m_tactics.GetSoundPriority(iType);
            float oldPriority = m_tactics.GetSoundPriority(m_iCuriousEventType);
            if (newPriority <= oldPriority) {
                return;
            }
        }
    }

    fRangeFactor = 1.0 - (fDistanceSquared / effectiveRadiusSq);
    
    // Lower skill bots have lower chance to notice sounds
    float skillMod = 0.5f + skill * 0.5f; // 50%-100%
    fRangeFactor *= skillMod;

    if (fRangeFactor < random()) {
        return;
    }

    if (pEnt->IsSubclassOfSentient()) {
        pSentOwner = static_cast<Sentient *>(pEnt);
    } else if (pEnt->IsSubclassOfVehicleTurretGun()) {
        VehicleTurretGun *pVTG = static_cast<VehicleTurretGun *>(pEnt);
        pSentOwner             = pVTG->GetSentientOwner();
    } else if (pEnt->IsSubclassOfItem()) {
        Item *pItem = static_cast<Item *>(pEnt);
        pSentOwner  = pItem->GetOwner();
    } else if (pEnt->IsSubclassOfProjectile()) {
        Projectile *pProj = static_cast<Projectile *>(pEnt);
        pSentOwner        = pProj->GetOwner();
    } else {
        pSentOwner = NULL;
    }

    if (pSentOwner) {
        if (pSentOwner == controlledEnt) {
            // Ignore self
            return;
        }

        if ((pSentOwner->flags & FL_NOTARGET) || pSentOwner->getSolidType() == SOLID_NOT) {
            return;
        }

        // Ignore teammates
        if (pSentOwner->IsSubclassOfPlayer()) {
            Player *p = static_cast<Player *>(pSentOwner);

            if (g_gametype->integer >= GT_TEAM && p->GetTeam() == controlledEnt->GetTeam()) {
                return;
            }
        }
    }

    // Store sound in tactics memory for coordination
    int sourceEntNum = pSentOwner ? pSentOwner->entnum : -1;
    m_tactics.ProcessSound(vPos, iType, sourceEntNum);

    switch (iType) {
    case AI_EVENT_MISC:
    case AI_EVENT_MISC_LOUD:
        break;
    case AI_EVENT_WEAPON_FIRE:
    case AI_EVENT_WEAPON_IMPACT:
    case AI_EVENT_EXPLOSION:
    case AI_EVENT_AMERICAN_VOICE:
    case AI_EVENT_GERMAN_VOICE:
    case AI_EVENT_AMERICAN_URGENT:
    case AI_EVENT_GERMAN_URGENT:
    case AI_EVENT_FOOTSTEP:
    case AI_EVENT_GRENADE:
    default:
        // Duration varies by sound type and skill
        // Important sounds = longer curiosity
        float priority = m_tactics.GetSoundPriority(iType);
        int baseDuration = (int)(10000 + priority * 15000); // 10-25 sec
        int skillBonus = (int)(skill * 5000); // 0-5 sec extra for skilled bots
        
        m_iCuriousTime   = level.inttime + baseDuration + skillBonus;
        m_vNewCuriousPos = vPos;
        m_iCuriousEventType = iType;  // Track what sound we're investigating
        break;
    }
}

void BotController::ProcessVoiceCommand(const char *cmd, Entity *commander, bool teamOnly)
{
    if (!commander || !cmd || !controlledEnt) return;

    if (teamOnly) {
        if (!commander->isSubclassOf(Player)) return;
        Player *pSpeaker = (Player *)commander;
        Player *pMe = controlledEnt;
        if (pSpeaker->GetTeam() != pMe->GetTeam()) return;
    }

    BotOrder order = ORDER_NONE;
    if (!Q_stricmp(cmd, "follow")) order = ORDER_FOLLOW;
    else if (!Q_stricmp(cmd, "hold")) order = ORDER_HOLD;
    else if (!Q_stricmp(cmd, "attack")) order = ORDER_ATTACK;
    else if (!Q_stricmp(cmd, "report")) {
        float hPercent = (float)controlledEnt->health / (float)controlledEnt->max_health * 100.0f;
        SendCommand(va("say_team Status: Health %.0f%%, Weapon %s", hPercent, "Ready"));
        return;
    }

    if (order != ORDER_NONE) {
        m_tactics.SetOrder(order, commander);
        
        // Acknowledge chance
        if (random() < 0.3f) {
             SendCommand("say_team Roger.");
        }
    }
}

/*
====================
ClearEnemy

Clear the bot's enemy
====================
*/
void BotController::ClearEnemy(void)
{
    m_iAttackTime   = 0;
    m_pEnemy        = NULL;
    m_iEnemyEyesTag = -1;
    m_vOldEnemyPos  = vec_zero;
    m_vLastEnemyPos = vec_zero;
}

/*
====================
Bot states
--------------------
____________________
--------------------
____________________
--------------------
____________________
--------------------
____________________
====================
*/

void BotController::CheckStates(void)
{
    m_StateCount = 0;

    for (int i = 0; i < MAX_BOT_FUNCTIONS; i++) {
        botfunc_t *func = &botfuncs[i];

        if (func->CheckCondition) {
            if ((this->*func->CheckCondition)()) {
                if (!(m_StateFlags & (1 << i))) {
                    m_StateFlags |= 1 << i;

                    if (func->BeginState) {
                        (this->*func->BeginState)();
                    }
                }

                if (func->ThinkState) {
                    m_StateCount++;
                    (this->*func->ThinkState)();
                }
            } else {
                if ((m_StateFlags & (1 << i))) {
                    m_StateFlags &= ~(1 << i);

                    if (func->EndState) {
                        (this->*func->EndState)();
                    }
                }
            }
        } else {
            if (func->ThinkState) {
                m_StateCount++;
                (this->*func->ThinkState)();
            }
        }
    }

    assert(m_StateCount);
    if (!m_StateCount) {
        gi.DPrintf("*** WARNING *** %s was stuck with no states !!!", controlledEnt->client->pers.netname);
        State_Reset();
    }
}

/*
====================
Default state


====================
*/
void BotController::State_DefaultBegin(void)
{
    movement.ClearMove();
}

void BotController::State_DefaultEnd(void) {}

void BotController::State_Reset(void)
{
    m_iCuriousTime    = 0;
    m_iAttackTime     = 0;
    m_vLastCuriousPos = vec_zero;
    m_vOldEnemyPos    = vec_zero;
    m_vLastEnemyPos   = vec_zero;
    m_vLastDeathPos   = vec_zero;
    m_pEnemy          = NULL;
    m_iEnemyEyesTag   = -1;
}

/*
====================
Idle state

Make the bot move to random directions
====================
*/
void BotController::InitState_Idle(botfunc_t *func)
{
    func->CheckCondition = &BotController::CheckCondition_Idle;
    func->BeginState     = &BotController::State_BeginIdle;
    func->ThinkState     = &BotController::State_Idle;
}

void BotController::State_BeginIdle(void)
{
    State_DefaultBegin();
    G_ScriptEvent("bot_roam", static_cast<Player*>(controlledEnt));
}

bool BotController::CheckCondition_Idle(void)
{
    if (m_iCuriousTime) {
        return false;
    }

    if (m_iAttackTime) {
        return false;
    }

    return true;
}

void BotController::State_Idle(void)
{
    if (CheckWindows()) {
        m_botCmd.buttons ^= BUTTON_ATTACKLEFT;
        m_iLastFireTime = level.inttime;
    } else {
        m_botCmd.buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
        CheckReload();
    }

    AimAtAimNode();

    if (!movement.MoveToBestAttractivePoint() && !movement.IsMoving()) {
        if (m_vLastDeathPos != vec_zero) {
            movement.MoveTo(m_vLastDeathPos);

            if (movement.MoveDone()) {
                m_vLastDeathPos = vec_zero;
            }
        } else {
            Vector randomDir(G_CRandom(16), G_CRandom(16), G_CRandom(16));
            Vector preferredDir;
            float  radius = 512 + G_Random(2048);

            preferredDir += Vector(controlledEnt->orientation[0]) * (rand() % 5 ? 1024 : -1024);
            preferredDir += Vector(controlledEnt->orientation[2]) * (rand() % 5 ? 1024 : -1024);
            movement.AvoidPath(controlledEnt->origin + randomDir, radius, preferredDir);
        }
    }
}

/*
====================
Curious state

Forward to the last event position
====================
*/
void BotController::InitState_Curious(botfunc_t *func)
{
    func->CheckCondition = &BotController::CheckCondition_Curious;
    func->BeginState     = &BotController::State_BeginCurious;
    func->ThinkState     = &BotController::State_Curious;
}

void BotController::State_BeginCurious(void)
{
    State_DefaultBegin();
    G_ScriptEvent("bot_curious", static_cast<Player*>(controlledEnt));
}

bool BotController::CheckCondition_Curious(void)
{
    if (m_iAttackTime) {
        m_iCuriousTime = 0;
        return false;
    }

    if (level.inttime > m_iCuriousTime) {
        if (m_iCuriousTime) {
            movement.ClearMove();
            m_iCuriousTime = 0;
        }

        return false;
    }

    return true;
}

void BotController::State_Curious(void)
{
    if (CheckWindows()) {
        m_botCmd.buttons ^= BUTTON_ATTACKLEFT;
        m_iLastFireTime = level.inttime;
    } else {
        m_botCmd.buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
    }

    AimAtAimNode();
    
    // Clear old sounds from memory
    m_tactics.ClearOldSounds();
    
    // Check sound priority for tactical approach
    float soundPriority = m_tactics.GetSoundPriority(m_iCuriousEventType);
    float skill = g_bot_skill->value;
    
    // High priority sounds (gunfire, explosions) warrant tactical approach
    // Higher skill bots are more tactical
    bool useTacticalApproach = (soundPriority >= 0.6f) && (skill >= 0.4f || random() < skill);

    if (!movement.MoveToBestAttractivePoint(3) && (!movement.IsMoving() || m_vLastCuriousPos != m_vNewCuriousPos)) {
        if (useTacticalApproach) {
            // Use cover-to-cover movement for high-priority sounds
            if (!m_tactics.AdvanceThroughCover(m_vNewCuriousPos)) {
                // No cover available, move directly
                movement.MoveTo(m_vNewCuriousPos);
            }
        } else {
            // Direct movement for lower priority sounds (footsteps, misc)
            movement.MoveTo(m_vNewCuriousPos);
        }
        m_vLastCuriousPos = m_vNewCuriousPos;
    }

    if (movement.MoveDone()) {
        m_iCuriousTime = 0;
        m_iCuriousEventType = AI_EVENT_NONE;
    }
}

/*
====================
Attack state

Attack the enemy
====================
*/
void BotController::InitState_Attack(botfunc_t *func)
{
    func->CheckCondition = &BotController::CheckCondition_Attack;
    func->BeginState     = &BotController::State_BeginAttack;
    func->EndState       = &BotController::State_EndAttack;
    func->ThinkState     = &BotController::State_Attack;
}

void BotController::State_BeginAttack(void)
{
    State_DefaultBegin();
    G_ScriptEvent("bot_attack", static_cast<Player*>(controlledEnt));
}

static Vector bot_origin;

static int sentients_compare(const void *elem1, const void *elem2)
{
    Entity *e1, *e2;
    float   delta[3];
    float   d1, d2;

    e1 = *(Entity **)elem1;
    e2 = *(Entity **)elem2;

    VectorSubtract(bot_origin, e1->origin, delta);
    d1 = VectorLengthSquared(delta);

    VectorSubtract(bot_origin, e2->origin, delta);
    d2 = VectorLengthSquared(delta);

    if (d2 <= d1) {
        return d1 > d2;
    } else {
        return -1;
    }
}

bool BotController::IsValidEnemy(Sentient *sent) const
{
    if (sent == controlledEnt) {
        return false;
    }

    if (sent->hidden() || (sent->flags & FL_NOTARGET)) {
        // Ignore hidden / non-target enemies
        return false;
    }

    if (sent->IsDead()) {
        // Ignore dead enemies
        return false;
    }

    if (sent->getSolidType() == SOLID_NOT) {
        // Ignore non-solid, like spectators
        return false;
    }

    if (sent->IsSubclassOfPlayer()) {
        Player *player = static_cast<Player *>(sent);

        if (g_gametype->integer >= GT_TEAM && player->GetTeam() == controlledEnt->GetTeam()) {
            return false;
        }
    } else {
        if (sent->m_Team == controlledEnt->m_Team) {
            return false;
        }
    }

    return true;
}

bool BotController::CheckCondition_Attack(void)
{
    Container<Sentient *> sents       = SentientList;
    float                 maxDistance = 0;

    bot_origin = controlledEnt->origin;
    sents.Sort(sentients_compare);

    for (int i = 1; i <= sents.NumObjects(); i++) {
        Sentient *sent = sents.ObjectAt(i);

        if (!IsValidEnemy(sent)) {
            continue;
        }

        maxDistance = Q_min(world->m_fAIVisionDistance, world->farplane_distance * 0.828);

        if (controlledEnt->CanSee(sent, 80, maxDistance, false)) {
            if (m_pEnemy != sent) {
                m_iEnemyEyesTag = -1;
            }

            if (!m_pEnemy) {
                m_iLastUnseenTime = level.inttime;
            }

            m_pEnemy        = sent;
            m_vLastEnemyPos = m_pEnemy->origin;
        }

        if (m_pEnemy) {
            m_iAttackTime = level.inttime + 1000;
            return true;
        }
    }

    if (level.inttime > m_iAttackTime) {
        if (m_iAttackTime) {
            movement.ClearMove();
            m_iAttackTime = 0;
        }

        return false;
    }

    return true;
}

void BotController::State_EndAttack(void)
{
    m_botCmd.buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
    controlledEnt->ZoomOff();
}

void BotController::State_Attack(void)
{
    bool    bMelee              = false;
    bool    bCanSee             = false;
    bool    bCanAttack          = false;
    float   fMinDistance        = 128;
    float   fMinDistanceSquared = fMinDistance * fMinDistance;
    float   fEnemyDistanceSquared;
    Weapon *pWeap   = controlledEnt->GetActiveWeapon(WEAPON_MAIN);
    bool    bNoMove = false;
    bool    bFiring = false;

    if (!m_pEnemy || !IsValidEnemy(m_pEnemy)) {
        // Ignore dead enemies
        m_iAttackTime = 0;
        return;
    }
    float fDistanceSquared = (m_pEnemy->origin - controlledEnt->origin).lengthSquared();

    m_vOldEnemyPos = m_vLastEnemyPos;

    bCanSee =
        controlledEnt->CanSee(m_pEnemy, 20, Q_min(world->m_fAIVisionDistance, world->farplane_distance * 0.828), false);

    if (bCanSee) {
        if (!pWeap) {
            return;
        }

        bCanAttack = true;
        if (m_iLastUnseenTime) {
            const float reactionTime = Q_min(1000 * Q_min(1, fDistanceSquared / Square(2048)), 1000);
            const unsigned int minDelay = g_bot_attack_react_min_delay->value * 1000;
            const unsigned int randomDelay = g_bot_attack_react_random_delay->value * 1000;
            if (level.inttime <= m_iLastUnseenTime + minDelay + G_Random(randomDelay)) {
                bCanAttack = false;
            } else {
                m_iLastUnseenTime = 0;
            }
        }

        if (bCanAttack) {
            const int fireDelay                    = pWeap->FireDelay(FIRE_PRIMARY) * 1000;
            float     fPrimaryBulletRange          = pWeap->GetBulletRange(FIRE_PRIMARY) / 1.25f;
            float     fPrimaryBulletRangeSquared   = fPrimaryBulletRange * fPrimaryBulletRange;
            float     fSecondaryBulletRange        = pWeap->GetBulletRange(FIRE_SECONDARY);
            float     fSecondaryBulletRangeSquared = fSecondaryBulletRange * fSecondaryBulletRange;
            float     fSpreadFactor                = pWeap->GetSpreadFactor(FIRE_PRIMARY);

            const int maxcontinuousFireTime = fireDelay + g_bot_attack_continuousfire_min_firetime->value * 1000
                                           + G_Random(g_bot_attack_continuousfire_random_firetime->value * 1000);
            const int maxBurstTime = fireDelay + g_bot_attack_burst_min_time->value * 1000
                                   + G_Random(g_bot_attack_burst_random_delay->value * 1000);

            //
            // check the fire movement speed if the weapon has a max fire movement
            //
            if (pWeap->GetMaxFireMovement() < 1 && pWeap->HasAmmoInClip(FIRE_PRIMARY)) {
                float length;

                length = controlledEnt->velocity.length();
                if ((length / sv_runspeed->value) > (pWeap->GetMaxFireMovementMult())) {
                    bNoMove = true;
                    movement.ClearMove();
                }
            }

            fMinDistance = fPrimaryBulletRange;

            if (fMinDistance > 256) {
                fMinDistance = 256;
            }

            fMinDistanceSquared = fMinDistance * fMinDistance;

            if (controlledEnt->client->ps.stats[STAT_AMMO] <= 0
                && controlledEnt->client->ps.stats[STAT_CLIPAMMO] <= 0) {
                m_botCmd.buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
                controlledEnt->ZoomOff();
            } else if (fDistanceSquared > fPrimaryBulletRangeSquared) {
                m_botCmd.buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
                controlledEnt->ZoomOff();
            } else {
                //
                // Attacking
                //

                if (pWeap->IsSemiAuto()) {
                    if (controlledEnt->client->ps.iViewModelAnim != VM_ANIM_IDLE
                        && (controlledEnt->client->ps.iViewModelAnim < VM_ANIM_IDLE_0
                            || controlledEnt->client->ps.iViewModelAnim > VM_ANIM_IDLE_2)) {
                        m_botCmd.buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
                        controlledEnt->ZoomOff();
                    } else if (fSpreadFactor < 0.25) {
                        bFiring = true;
                        m_botCmd.buttons ^= BUTTON_ATTACKLEFT;
                        if (pWeap->GetZoom()) {
                            if (!controlledEnt->IsZoomed()) {
                                m_botCmd.buttons |= BUTTON_ATTACKRIGHT;
                            } else {
                                m_botCmd.buttons &= ~BUTTON_ATTACKRIGHT;
                            }
                        }
                    } else {
                        bNoMove = true;
                        movement.ClearMove();
                    }
                } else {
                    bFiring = true;
                    m_botCmd.buttons |= BUTTON_ATTACKLEFT;
                }
            }

            //
            // Burst
            //

            if (m_iLastBurstTime) {
                if (level.inttime > m_iLastBurstTime + maxBurstTime) {
                    m_iLastBurstTime      = 0;
                    m_iContinuousFireTime = 0;
                } else {
                    m_botCmd.buttons &= ~BUTTON_ATTACKLEFT;
                }
            } else {
                if (bFiring) {
                    m_iContinuousFireTime += level.intframetime;
                } else {
                    m_iContinuousFireTime = 0;
                }

                if (!m_iLastBurstTime && m_iContinuousFireTime > maxcontinuousFireTime) {
                    m_iLastBurstTime      = level.inttime;
                    m_iContinuousFireTime = 0;
                }
            }

            m_iLastFireTime = level.inttime;

            if (pWeap->GetFireType(FIRE_SECONDARY) == FT_MELEE) {
                if (controlledEnt->client->ps.stats[STAT_AMMO] <= 0
                    && controlledEnt->client->ps.stats[STAT_CLIPAMMO] <= 0) {
                    bMelee = true;
                } else if (fDistanceSquared <= fSecondaryBulletRangeSquared) {
                    bMelee = true;
                }
            }

            if (bMelee) {
                m_botCmd.buttons &= ~BUTTON_ATTACKLEFT;

                if (fDistanceSquared <= fSecondaryBulletRangeSquared) {
                    m_botCmd.buttons ^= BUTTON_ATTACKRIGHT;
                } else {
                    m_botCmd.buttons &= ~BUTTON_ATTACKRIGHT;
                }
            }

            m_iAttackTime        = level.inttime + 1000;
            m_iAttackStopAimTime = level.inttime + 3000;
            m_iLastSeenTime      = level.inttime;
            m_vLastEnemyPos      = m_pEnemy->origin;
        }
    } else {
        m_botCmd.buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
        fMinDistanceSquared = 0;

        if (level.inttime > m_iLastSeenTime + 2000) {
            m_iLastUnseenTime = level.inttime;
        }
    }

    if (bCanSee || level.inttime < m_iAttackStopAimTime) {
        Vector        vRandomOffset;
        Vector        vTarget;
        orientation_t eyes_or;

        if (m_iEnemyEyesTag == -1) {
            // Cache the tag
            m_iEnemyEyesTag = gi.Tag_NumForName(m_pEnemy->edict->tiki, "eyes bone");
        }

        if (m_iEnemyEyesTag != -1) {
            // Use the enemy's eyes bone
            m_pEnemy->GetTag(m_iEnemyEyesTag, &eyes_or);

            //vRandomOffset = Vector(G_CRandom(8), G_CRandom(8), -G_Random(32));
            vTarget = eyes_or.origin;
        } else {
            //vRandomOffset = Vector(G_CRandom(8), G_CRandom(8), 16 + G_Random(m_pEnemy->viewheight - 16));
            vTarget = m_pEnemy->origin;
        }

        if (level.inttime >= m_iLastAimTime + 100) {
            if (m_iEnemyEyesTag != -1) {
                m_vAimOffset[0] = G_CRandom((m_pEnemy->maxs.x - m_pEnemy->mins.x) * 0.5);
                m_vAimOffset[1] = G_CRandom((m_pEnemy->maxs.y - m_pEnemy->mins.y) * 0.5);
                m_vAimOffset[2] = -G_Random(m_pEnemy->maxs.z * 0.5);
            } else {
                m_vAimOffset[0] = G_CRandom((m_pEnemy->maxs.x - m_pEnemy->mins.x) * 0.5);
                m_vAimOffset[1] = G_CRandom((m_pEnemy->maxs.y - m_pEnemy->mins.y) * 0.5);
                m_vAimOffset[2] = 16 + G_Random(m_pEnemy->viewheight - 16);
            }
            m_iLastAimTime = level.inttime;
        }

        Vector predictedPos = m_tactics.GetPredictedPos();
        if (predictedPos != vec_zero) {
            vTarget = predictedPos;
        }

        // Skill affects aim accuracy: lower skill = more spread
        // Effective spread = base spread * (2.0 - skill)
        // skill 0.0 -> 2.0x spread, skill 0.5 -> 1.5x, skill 1.0 -> 1.0x
        float skillMod = 2.0f - g_bot_skill->value;
        if (skillMod < 1.0f) skillMod = 1.0f;
        rotation.AimAt(vTarget + m_vAimOffset * g_bot_attack_spreadmult->value * skillMod);
    } else {
        AimAtAimNode();
    }

    if (bNoMove) {
        return;
    }

    fEnemyDistanceSquared = (controlledEnt->origin - m_vLastEnemyPos).lengthSquared();

    if ((!movement.MoveToBestAttractivePoint(5) && !movement.IsMoving())
        || (m_vOldEnemyPos != m_vLastEnemyPos && !movement.MoveDone()) || fEnemyDistanceSquared < fMinDistanceSquared) {
        if (!bMelee || !bCanSee) {
            if (fEnemyDistanceSquared < fMinDistanceSquared) {
                Vector vDir = controlledEnt->origin - m_vLastEnemyPos;
                VectorNormalizeFast(vDir);

                movement.AvoidPath(m_vLastEnemyPos, fMinDistance, Vector(controlledEnt->orientation[1]) * 512);
            } else {
                movement.MoveTo(m_vLastEnemyPos);
            }

            if (!bCanSee && movement.MoveDone()) {
                // Lost track of the enemy
                ClearEnemy();
                return;
            }
        } else {
            movement.MoveTo(m_vLastEnemyPos);
        }
    }

    if (movement.IsMoving()) {
        m_iAttackTime = level.inttime + 1000;
    }
}

/*
====================
Grenade state

Avoid any grenades and react to nearby explosives
====================
*/
void BotController::InitState_Grenade(botfunc_t *func)
{
    func->CheckCondition = &BotController::CheckCondition_Grenade;
    func->BeginState     = &BotController::State_BeginGrenade;
    func->EndState       = &BotController::State_EndGrenade;
    func->ThinkState     = &BotController::State_Grenade;
}

bool BotController::CheckCondition_Grenade(void)
{
    Vector fleeDir;
    float danger;
    
    // Use the tactical system to detect grenades
    if (m_tactics.ShouldFleeFromGrenade(fleeDir, danger)) {
        // Only enter grenade state if danger is significant
        if (danger > 0.3f) {
            m_vGrenadeFleeDir = fleeDir;
            m_fGrenadeDanger = danger;
            return true;
        }
    }
    
    return false;
}

void BotController::State_BeginGrenade(void)
{
    State_DefaultBegin();
    m_iGrenadeFleeStartTime = level.inttime;
    
    // Stop any current movement to recalculate flee path
    movement.ClearMove();
}

void BotController::State_EndGrenade(void)
{
    m_vGrenadeFleeDir = vec_zero;
    m_fGrenadeDanger = 0;
}

void BotController::State_Grenade(void)
{
    // Update flee direction each frame
    Vector fleeDir;
    float danger;
    
    if (!m_tactics.ShouldFleeFromGrenade(fleeDir, danger)) {
        // Grenade is gone or we're safe
        return;
    }
    
    m_vGrenadeFleeDir = fleeDir;
    m_fGrenadeDanger = danger;
    
    // Calculate flee destination
    float fleeDist = 400.0f * danger; // Flee further if more dangerous
    Vector fleeTarget = controlledEnt->origin + m_vGrenadeFleeDir * fleeDist;
    
    // Move away from grenade
    movement.MoveTo(fleeTarget);
    
    // Sprint while fleeing
    m_botCmd.buttons |= BUTTON_RUN;
    
    // Don't shoot while fleeing from grenades - focus on survival
    m_botCmd.buttons &= ~(BUTTON_ATTACKLEFT | BUTTON_ATTACKRIGHT);
    
    // Look where we're going
    AimAtAimNode();
    
    // Time limit on grenade flee (don't get stuck)
    if (level.inttime > m_iGrenadeFleeStartTime + 3000) {
        // Force exit grenade state after 3 seconds
        m_vGrenadeFleeDir = vec_zero;
        m_fGrenadeDanger = 0;
    }
}

Weapon *BotController::FindWeaponWithAmmo()
{
    Weapon               *next;
    int                   n;
    int                   j;
    int                   bestrank;
    Weapon               *bestweapon;
    const Container<int>& inventory = controlledEnt->getInventory();

    n = inventory.NumObjects();

    // Search until we find the best weapon with ammo
    bestweapon = NULL;
    bestrank   = -999999;

    for (j = 1; j <= n; j++) {
        next = (Weapon *)G_GetEntity(inventory.ObjectAt(j));

        assert(next);
        if (!next->IsSubclassOfWeapon() || next->IsSubclassOfInventoryItem()) {
            continue;
        }

        if (next->GetWeaponClass() & WEAPON_CLASS_THROWABLE) {
            continue;
        }

        if (next->GetRank() < bestrank) {
            continue;
        }

        if (!next->HasAmmo(FIRE_PRIMARY)) {
            continue;
        }

        bestweapon = (Weapon *)next;
        bestrank   = bestweapon->GetRank();
    }

    return bestweapon;
}

Weapon *BotController::FindMeleeWeapon()
{
    Weapon               *next;
    int                   n;
    int                   j;
    int                   bestrank;
    Weapon               *bestweapon;
    const Container<int>& inventory = controlledEnt->getInventory();

    n = inventory.NumObjects();

    // Search until we find the best weapon with ammo
    bestweapon = NULL;
    bestrank   = -999999;

    for (j = 1; j <= n; j++) {
        next = (Weapon *)G_GetEntity(inventory.ObjectAt(j));

        assert(next);
        if (!next->IsSubclassOfWeapon() || next->IsSubclassOfInventoryItem()) {
            continue;
        }

        if (next->GetRank() < bestrank) {
            continue;
        }

        if (next->GetFireType(FIRE_SECONDARY) != FT_MELEE) {
            continue;
        }

        bestweapon = (Weapon *)next;
        bestrank   = bestweapon->GetRank();
    }

    return bestweapon;
}

/*
====================
Tactical state

Advanced tactical behavior when engaged in combat
====================
*/
void BotController::InitState_Tactical(botfunc_t *func)
{
    func->CheckCondition = &BotController::CheckCondition_Tactical;
    func->BeginState     = &BotController::State_BeginTactical;
    func->EndState       = &BotController::State_EndTactical;
    func->ThinkState     = &BotController::State_Tactical;
}

bool BotController::CheckCondition_Tactical(void)
{
    // Active when we have an enemy or recently had one (but not at level start)
    return m_pEnemy != NULL || (m_iAttackTime != 0 && level.inttime < m_iAttackTime + 5000);
}

void BotController::State_BeginTactical(void)
{
    m_iTacticalStateTime = level.inttime;
    m_iTacticalMode = 0; // Reset tactical mode
}

void BotController::State_EndTactical(void)
{
    m_iTacticalMode = 0;
}

void BotController::State_Tactical(void)
{
    // This state works alongside Attack state to provide additional tactical behavior
    // The behavior tree in BotTactics handles most of the tactical decision making
    
    if (!controlledEnt) return;
    
    // Determine tactical mode based on situation
    float healthPercent = (float)controlledEnt->health / (float)controlledEnt->max_health;
    
    // Low health = defensive mode
    if (healthPercent < 0.3f) {
        m_iTacticalMode = 1; // Defensive
    }
    // High health with enemy = aggressive mode
    else if (healthPercent > 0.7f && m_pEnemy) {
        m_iTacticalMode = 2; // Aggressive
    }
    // Default = balanced mode
    else {
        m_iTacticalMode = 0; // Balanced
    }
    
    // Tactical mode influences movement behavior in Attack state via m_tactics
}

void BotController::UseWeaponWithAmmo()
{
    Weapon *bestWeapon = FindWeaponWithAmmo();
    if (!bestWeapon) {
        //
        // If there is no weapon with ammo, fallback to a weapon that can melee
        //
        bestWeapon = FindMeleeWeapon();
    }

    if (!bestWeapon || bestWeapon == controlledEnt->GetActiveWeapon(WEAPON_MAIN)) {
        return;
    }

    controlledEnt->useWeapon(bestWeapon, WEAPON_MAIN);
}

void BotController::Spawned(void)
{
    ClearEnemy();
    m_iCuriousTime   = 0;
    m_botCmd.buttons = 0;

    // HOOK: bot_spawn
    G_ScriptEvent("bot_spawn", controlledEnt);
}

void BotController::Think()
{
    usercmd_t  ucmd;
    usereyes_t eyeinfo;

    UpdateBotStates();
    m_tactics.Update(&m_botCmd);
    
    GetUsercmd(&ucmd);
    GetEyeInfo(&eyeinfo);

    G_ClientThink(controlledEnt->edict, &ucmd, &eyeinfo);
}

void BotController::Killed(const Event& ev)
{
    Entity *attacker;

    // HOOK: bot_killed
    attacker = ev.GetEntity(1);
    G_ScriptEvent("bot_killed", controlledEnt, attacker);

    // send the respawn buttons
    if (!(m_botCmd.buttons & BUTTON_ATTACKLEFT)) {
        m_botCmd.buttons |= BUTTON_ATTACKLEFT;
    } else {
        m_botCmd.buttons &= ~BUTTON_ATTACKLEFT;
    }

    m_botEyes.ofs[0]    = 0;
    m_botEyes.ofs[1]    = 0;
    m_botEyes.ofs[2]    = 0;
    m_botEyes.angles[0] = 0;
    m_botEyes.angles[1] = 0;

    attacker = ev.GetEntity(1);

    if (attacker && rand() % 5 == 0) {
        // 1/5 chance to go back to the attacker position
        m_vLastDeathPos = attacker->origin;
    } else {
        m_vLastDeathPos = vec_zero;
    }

    // Choose a new random primary weapon
    Event event(EV_Player_PrimaryDMWeapon);
    event.AddString("auto");

    controlledEnt->ProcessEvent(event);

    //
    // This is useful to change nationality in Spearhead and Breakthrough
    // this allows the AI to use more weapons
    //
    Info_SetValueForKey(controlledEnt->client->pers.userinfo, "dm_playermodel", G_GetRandomAlliedPlayerModel());
    Info_SetValueForKey(controlledEnt->client->pers.userinfo, "dm_playergermanmodel", G_GetRandomGermanPlayerModel());

    G_ClientUserinfoChanged(controlledEnt->edict, controlledEnt->client->pers.userinfo);
}

void BotController::GotKill(const Event& ev)
{
    ClearEnemy();
    m_iCuriousTime = 0;

    if (g_bot_instamsg_chance->integer && level.inttime >= m_iNextTauntTime && (rand() % g_bot_instamsg_chance->integer) == 0) {
        //
        // Randomly play a taunt
        //
        Event event("dmmessage");

        event.AddInteger(0);

        if (g_protocol >= protocol_e::PROTOCOL_MOHTA_MIN) {
            event.AddString("*5" + str(1 + (rand() % 8)));
        } else {
            event.AddString("*4" + str(1 + (rand() % 9)));
        }

        controlledEnt->ProcessEvent(event);

        m_iNextTauntTime = level.inttime + g_bot_instamsg_delay->integer;
    }
}

void BotController::EventStuffText(const str& text)
{
    SendCommand(text);
}

void BotController::Pain(Event *ev)
{
    Entity *attacker = ev->GetEntity(1);
    if (attacker && attacker->isSubclassOf(Sentient) && attacker != controlledEnt) {
        // React to being hit: always turn toward attacker first
        Sentient *sentAttacker = static_cast<Sentient *>(attacker);
        
        // Check if we should switch focus to this attacker
        bool shouldSwitchTarget = !m_pEnemy;
        if (!shouldSwitchTarget && m_pEnemy) {
            // Switch if new attacker is closer than current enemy
            float newDistSq = (attacker->origin - controlledEnt->origin).lengthSquared();
            float oldDistSq = (m_pEnemy->origin - controlledEnt->origin).lengthSquared();
            shouldSwitchTarget = (newDistSq < oldDistSq * 0.8f); // Only switch if significantly closer
        }
        
        if (shouldSwitchTarget) {
            m_pEnemy = sentAttacker;
            m_iEnemyEyesTag = -1;
        }
        
        // Always turn toward attacker when hit (even if not switching target)
        rotation.TurnTowards(attacker->origin);
        
        // Store attacker position so we can hunt them down
        m_vLastEnemyPos = attacker->origin;
        
        // Keep attack state active for 5 seconds after being shot
        // This keeps us hunting even if we lose sight briefly
        m_iAttackTime = level.inttime + 5000;
        
        // Also activate curious mode toward the attacker position
        // in case we lose them - we'll investigate their last known position
        m_iCuriousTime = level.inttime + 10000;
        m_vNewCuriousPos = attacker->origin;
    }
}

void BotController::setControlledEntity(Player *player)
{
    controlledEnt = player;
    movement.SetControlledEntity(player);
    rotation.SetControlledEntity(player);

    delegateHandle_gotKill =
        player->delegate_gotKill.Add(std::bind(&BotController::GotKill, this, std::placeholders::_1));
    delegateHandle_killed = player->delegate_killed.Add(std::bind(&BotController::Killed, this, std::placeholders::_1));
    delegateHandle_stufftext =
        player->delegate_stufftext.Add(std::bind(&BotController::EventStuffText, this, std::placeholders::_1));
    delegateHandle_spawned = player->delegate_spawned.Add(std::bind(&BotController::Spawned, this));
    delegateHandle_pain = player->delegate_pain.Add(std::bind(&BotController::Pain, this, std::placeholders::_1));

    m_tactics.Init(this);
}

Player *BotController::getControlledEntity() const
{
    return controlledEnt;
}

BotController *BotControllerManager::createController(Player *player)
{
    BotController *controller = new BotController();
    controller->setControlledEntity(player);

    controllers.AddObject(controller);

    return controller;
}

void BotControllerManager::removeController(BotController *controller)
{
    controllers.RemoveObject(controller);
    delete controller;
}

BotController *BotControllerManager::findController(Entity *ent)
{
    int i;

    for (i = 1; i <= controllers.NumObjects(); i++) {
        BotController *controller = controllers.ObjectAt(i);
        if (controller->getControlledEntity() == ent) {
            return controller;
        }
    }

    return nullptr;
}

const Container<BotController *>& BotControllerManager::getControllers() const
{
    return controllers;
}

BotControllerManager::~BotControllerManager()
{
    Cleanup();
}

void BotControllerManager::Init()
{
    BotController::Init();
}

void BotControllerManager::Cleanup()
{
    int i;

    BotController::Init();

    for (i = 1; i <= controllers.NumObjects(); i++) {
        BotController *controller = controllers.ObjectAt(i);
        delete controller;
    }

    controllers.FreeObjectList();
}

void BotControllerManager::ThinkControllers()
{
    int i;

    // Delete controllers that don't have associated player entity
    // This cannot happen unless some mods remove them
    for (i = controllers.NumObjects(); i > 0; i--) {
        BotController *controller = controllers.ObjectAt(i);
        if (!controller->getControlledEntity()) {
            gi.DPrintf(
                "Bot %d has no associated player entity. This shouldn't happen unless the entity has been removed by a "
                "script. The controller will be removed, please fix.\n",
                i
            );

            // Remove the controller, it will be recreated later to match `sv_numbots`
            delete controller;
            controllers.RemoveObjectAt(i);
        }
    }

    for (i = 1; i <= controllers.NumObjects(); i++) {
        BotController *controller = controllers.ObjectAt(i);
        controller->Think();
    }
}
