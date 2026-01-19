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
static ScriptDelegate sd_client_userinfo_changed("client_userinfo_changed", "Client Userinfo Changed");
static ScriptDelegate sd_team_join("team_join", "Team Joined");
static ScriptDelegate sd_player_say("player_say", "Player Said");

// Player - Death Types
static ScriptDelegate sd_player_suicide("player_suicide", "Player Suicided");
static ScriptDelegate sd_player_crushed("player_crushed", "Player Crushed");
static ScriptDelegate sd_player_telefragged("player_telefragged", "Player Telefragged");
static ScriptDelegate sd_player_roadkill("player_roadkill", "Player Road Killed");
static ScriptDelegate sd_player_bash("player_bash", "Player Bashed");
static ScriptDelegate sd_player_teamkill("player_teamkill", "Player Team Killed");

// Player - Actions
static ScriptDelegate sd_player_spawn("player_spawn", "Player Spawned");
static ScriptDelegate sd_player_pain("player_pain", "Player Took Pain");
static ScriptDelegate sd_player_spectate("player_spectate", "Player Became Spectator");
static ScriptDelegate sd_player_freeze("player_freeze", "Player Frozen/Unfrozen (arg: frozen state)");
static ScriptDelegate sd_player_use_object_start("player_use_object_start", "Player Started Using Object");
static ScriptDelegate sd_player_use_object_finish("player_use_object_finish", "Player Finished Using Object");

// Game Flow
static ScriptDelegate sd_game_start("game_start", "Game Started");
static ScriptDelegate sd_game_end("game_end", "Game Ended");
static ScriptDelegate sd_game_init("game_init", "Game Initialized");
static ScriptDelegate sd_round_start("round_start", "Round Started");
static ScriptDelegate sd_round_end("round_end", "Round Ended");
static ScriptDelegate sd_team_win("team_win", "Team Won");
static ScriptDelegate sd_objective_update("objective_update", "Objective Updated");

// Weapon - Extended
static ScriptDelegate sd_weapon_reload_done("weapon_reload_done", "Weapon Reload Done");
static ScriptDelegate sd_weapon_ready("weapon_ready", "Weapon Ready");
static ScriptDelegate sd_weapon_no_ammo("weapon_no_ammo", "Weapon No Ammo");
static ScriptDelegate sd_weapon_holster("weapon_holster", "Weapon Holstered");
static ScriptDelegate sd_weapon_raise("weapon_raise", "Weapon Raised");
static ScriptDelegate sd_weapon_drop("weapon_drop", "Weapon Dropped");

// Vehicle & Turret
static ScriptDelegate sd_vehicle_enter("vehicle_enter", "Vehicle Entered");
static ScriptDelegate sd_vehicle_exit("vehicle_exit", "Vehicle Exited");
static ScriptDelegate sd_vehicle_death("vehicle_death", "Vehicle Destroyed");
static ScriptDelegate sd_vehicle_collision("vehicle_collision", "Vehicle Collision");
static ScriptDelegate sd_turret_enter("turret_enter", "Turret Entered");
static ScriptDelegate sd_turret_exit("turret_exit", "Turret Exited");

// World
static ScriptDelegate sd_door_open("door_open", "Door Opened");
static ScriptDelegate sd_door_close("door_close", "Door Closed");

// Items
static ScriptDelegate sd_health_pickup("health_pickup", "Health Picked Up");
static ScriptDelegate sd_ammo_pickup("ammo_pickup", "Ammo Picked Up");
static ScriptDelegate sd_armor_pickup("armor_pickup", "Armor Picked Up");

// Bot Events
static ScriptDelegate sd_bot_spawn("bot_spawn", "Bot Spawned");
static ScriptDelegate sd_bot_killed("bot_killed", "Bot Killed");
static ScriptDelegate sd_bot_state_idle("bot_roam", "Bot Roaming");
static ScriptDelegate sd_bot_state_curious("bot_curious", "Bot Curious");
static ScriptDelegate sd_bot_state_attack("bot_attack", "Bot Attacking");
