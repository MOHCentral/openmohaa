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

#include "g_scriptevents.h"
#include "entity.h"
#include "../script/scriptvariable.h"
#include <cstdarg>

//
// Register all the events here
//

// Combat
static ScriptDelegate sd_player_kill("player_kill", "Player Killed");
static ScriptDelegate sd_player_death("player_death", "Player Died");
static ScriptDelegate sd_player_damage("player_damage", "Player Damaged");
static ScriptDelegate sd_weapon_fire("weapon_fire", "Weapon Fired");
static ScriptDelegate sd_weapon_hit("weapon_hit", "Weapon Hit");
static ScriptDelegate sd_player_headshot("player_headshot", "Player Headshot");
static ScriptDelegate sd_weapon_reload("weapon_reload", "Weapon Reloaded");
static ScriptDelegate sd_weapon_change("weapon_change", "Weapon Changed");
static ScriptDelegate sd_grenade_throw("grenade_throw", "Grenade Thrown");
static ScriptDelegate sd_grenade_explode("grenade_explode", "Grenade Exploded");

// Movement
static ScriptDelegate sd_player_jump("player_jump", "Player Jumped");
static ScriptDelegate sd_player_land("player_land", "Player Landed");
static ScriptDelegate sd_player_crouch("player_crouch", "Player Crouched");
static ScriptDelegate sd_player_prone("player_prone", "Player Proned");
static ScriptDelegate sd_player_distance("player_distance", "Player Distance Traveled");

// Interaction
static ScriptDelegate sd_ladder_mount("ladder_mount", "Ladder Mounted");
static ScriptDelegate sd_ladder_dismount("ladder_dismount", "Ladder Dismounted");
static ScriptDelegate sd_item_pickup("item_pickup", "Item Picked Up");
static ScriptDelegate sd_item_drop("item_drop", "Item Dropped");
static ScriptDelegate sd_player_use("player_use", "Player Used Something");

// Session
static ScriptDelegate sd_client_connect("client_connect", "Client Connected");
static ScriptDelegate sd_client_disconnect("client_disconnect", "Client Disconnected");
static ScriptDelegate sd_client_begin("client_begin", "Client Began");
static ScriptDelegate sd_team_join("team_join", "Team Joined");
static ScriptDelegate sd_player_say("player_say", "Player Said");


void G_ScriptEvent(const char* eventName, Entity* entity, const char* format, ...)
{
    ScriptDelegate* delegate = ScriptDelegate::GetScriptDelegate(eventName);
    if (!delegate) {
        // Warning?
        return;
    }

    Event ev;
    va_list argptr;
    va_start(argptr, format);

    if (format) {
        const char* p = format;
        while (*p) {
            switch (*p) {
            case 's':
                ev.AddString(va_arg(argptr, char *));
                break;
            case 'i':
                ev.AddInteger(va_arg(argptr, int));
                break;
            case 'f':
                ev.AddFloat((float)va_arg(argptr, double)); // float is promoted to double in varargs
                break;
            case 'v':
                ev.AddVector(*va_arg(argptr, Vector *));
                break;
            case 'e':
                ev.AddEntity(va_arg(argptr, Entity *));
                break;
            default:
                break;
            }
            p++;
        }
    }

    va_end(argptr);

    // Trigger
    if (entity) {
        delegate->Trigger(entity, ev);
    } else {
        delegate->Trigger(ev);
    }
}
