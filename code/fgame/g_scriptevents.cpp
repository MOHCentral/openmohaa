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
// Extended Script Events
//
// NOTE: The 6 core player events are defined in player.cpp:
//   - player_connected    (Player::scriptDelegate_connected)
//   - player_disconnecting (Player::scriptDelegate_disconnecting)
//   - player_spawned      (Player::scriptDelegate_spawned)
//   - player_damaged      (Player::scriptDelegate_damage)
//   - player_killed       (Player::scriptDelegate_kill)
//   - player_textMessage  (Player::scriptDelegate_textMessage)
//
// The following events are NON-OVERLAPPING with the core player events.
// Information like headshots, suicides, teamkills, death types, etc. can all
// be derived from the player_killed event parameters (location, meansofdeath, attacker vs victim).
//

// Player Movement Events (additional to core player events in player.cpp)
static ScriptDelegate sd_player_jump("player_jump", "Player Jumped");
static ScriptDelegate sd_player_crouch("player_crouch", "Player Crouched");
static ScriptDelegate sd_player_stand("player_stand", "Player Standing");
static ScriptDelegate sd_player_distance("player_distance", "Player Distance Threshold Reached");
static ScriptDelegate sd_ladder_mount("ladder_mount", "Player Mounted Ladder");
static ScriptDelegate sd_ladder_dismount("ladder_dismount", "Player Dismounted Ladder");

// Weapon Events
static ScriptDelegate sd_weapon_fire("weapon_fire", "Weapon Fired");
static ScriptDelegate sd_weapon_hit("weapon_hit", "Weapon Hit");
static ScriptDelegate sd_weapon_reload("weapon_reload", "Weapon Reloaded");
static ScriptDelegate sd_weapon_reload_done("weapon_reload_done", "Weapon Reload Done");
static ScriptDelegate sd_weapon_change("weapon_change", "Weapon Changed");
static ScriptDelegate sd_weapon_ready("weapon_ready", "Weapon Ready");
static ScriptDelegate sd_weapon_no_ammo("weapon_no_ammo", "Weapon No Ammo");
static ScriptDelegate sd_weapon_holster("weapon_holster", "Weapon Holstered");
static ScriptDelegate sd_weapon_raise("weapon_raise", "Weapon Raised");
static ScriptDelegate sd_weapon_drop("weapon_drop", "Weapon Dropped");

// Grenade Events
static ScriptDelegate sd_grenade_throw("grenade_throw", "Grenade Thrown");
static ScriptDelegate sd_grenade_explode("grenade_explode", "Grenade Exploded");

// Item Events
static ScriptDelegate sd_item_pickup("item_pickup", "Item Picked Up");
static ScriptDelegate sd_item_drop("item_drop", "Item Dropped");
static ScriptDelegate sd_item_respawn("item_respawn", "Item Respawned");
static ScriptDelegate sd_health_pickup("health_pickup", "Health Picked Up");
static ScriptDelegate sd_ammo_pickup("ammo_pickup", "Ammo Picked Up");
static ScriptDelegate sd_armor_pickup("armor_pickup", "Armor Picked Up");

// Team Events
static ScriptDelegate sd_team_join("team_join", "Team Joined");
static ScriptDelegate sd_team_win("team_win", "Team Won");

// Client Events (non-overlapping with player_connected/disconnecting)
static ScriptDelegate sd_client_userinfo_changed("client_userinfo_changed", "Client Userinfo Changed");

// Game Flow Events
static ScriptDelegate sd_game_start("game_start", "Game Started");
static ScriptDelegate sd_game_end("game_end", "Game Ended");
static ScriptDelegate sd_game_init("game_init", "Game Initialized");
static ScriptDelegate sd_round_start("round_start", "Round Started");
static ScriptDelegate sd_round_end("round_end", "Round Ended");
static ScriptDelegate sd_warmup_start("warmup_start", "Warmup Started");
static ScriptDelegate sd_warmup_end("warmup_end", "Warmup Ended");
static ScriptDelegate sd_intermission_start("intermission_start", "Intermission Started");
static ScriptDelegate sd_match_end("match_end", "Match Ended");

// Objective Events
static ScriptDelegate sd_objective_update("objective_update", "Objective Updated");
static ScriptDelegate sd_objective_capture("objective_capture", "Objective Captured");

// Map Events
static ScriptDelegate sd_map_init("map_init", "Map Initialization Started");
static ScriptDelegate sd_map_start("map_start", "Map Initialization Complete");
static ScriptDelegate sd_map_shutdown("map_shutdown", "Map Shutting Down");
static ScriptDelegate sd_map_ready("map_ready", "Map Ready (entities spawned)");
static ScriptDelegate sd_map_load_start("map_load_start", "Map Load Started");
static ScriptDelegate sd_map_load_end("map_load_end", "Map Load Ended");
static ScriptDelegate sd_map_restart("map_restart", "Map Restarted");
static ScriptDelegate sd_map_change_start("map_change_start", "Map Change Started");

// Server Events
static ScriptDelegate sd_server_console_command("server_console_command", "Server Console Command");
static ScriptDelegate sd_server_process_start("server_process_start", "Server Process Started (once per executable)");
static ScriptDelegate sd_server_process_quit("server_process_quit", "Server Process Quitting (once per executable)");

// Vote Events
static ScriptDelegate sd_vote_start("vote_start", "Vote Started");
static ScriptDelegate sd_vote_passed("vote_passed", "Vote Passed");
static ScriptDelegate sd_vote_failed("vote_failed", "Vote Failed");

// Vehicle Events
static ScriptDelegate sd_vehicle_death("vehicle_death", "Vehicle Destroyed");
static ScriptDelegate sd_vehicle_collision("vehicle_collision", "Vehicle Collision");

// Door Events
static ScriptDelegate sd_door_open("door_open", "Door Opened");
static ScriptDelegate sd_door_close("door_close", "Door Closed");

// Bot Events
static ScriptDelegate sd_bot_spawn("bot_spawn", "Bot Spawned");
static ScriptDelegate sd_bot_killed("bot_killed", "Bot Killed");
static ScriptDelegate sd_bot_state_idle("bot_roam", "Bot Roaming");
static ScriptDelegate sd_bot_state_curious("bot_curious", "Bot Curious");
static ScriptDelegate sd_bot_state_attack("bot_attack", "Bot Attacking");

// Actor Events (AI)
static ScriptDelegate sd_actor_spawn("actor_spawn", "Actor Spawned");
static ScriptDelegate sd_actor_killed("actor_killed", "Actor Killed");

// Misc Events
static ScriptDelegate sd_player_inactivity_drop("player_inactivity_drop", "Player Dropped for Inactivity");
static ScriptDelegate sd_explosion("explosion", "Explosion Occurred");