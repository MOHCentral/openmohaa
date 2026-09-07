# Script events

OpenMoHAA introduces a new way for scripts to monitor for specific events, like players spawning or getting killed. Multiple scripts can subscribe to the same events.

## Subscribing

Commands related to events:
```cpp
event_subscribe event_name script_label
    Subscribes to an event, script_label will be called when the event is triggered

event_unsubscribe event_name script_label
    Unsubscribe from an event, script_label will not be called anymore
```

When an event is not needed anymore, make sure to call `event_unsubscribe` with the same parameters used when subscribing to the event.

### Example

```cpp
main:
    event_subscribe "player_spawned" event_player_spawned
    // Can specify another script:
    //event_subscribe "player_spawned" global/test_script::event_player_spawned
end

event_player_spawned:
    iprintlnbold("player entity number " + self.entnum + " just spawned!")
end
```

## List of events

### Player events

The `self` object is the player object for all triggered player events.

#### player_connected

The player entered the game.

Called when:
- When a client spawns for the first time
- When the map restarts, or when the map changes (for all players)
- On the next round (for all players)

This is called after the player finished spawning, and before `player_spawned` event.

#### player_damaged

The player just got hit.

The parameters are the same as the `damage` command:
```
player_damaged local.attacker local.damage local.inflictor local.position local.direction local.normal local.knockback local.damageflags local.meansofdeath local.location
```

#### player_disconnecting

The player is disconnecting.

#### player_killed

The player got killed.

The parameters are the same as the `killed` command:
```
player_killed local.attacker local.damage local.inflictor local.position local.direction local.normal local.knockback local.damageflags local.meansofdeath local.location
```

#### player_spawned

The player just spawned.

Called when:
- The player has entered the battle
- The player respawned or spawned with weapons

This is called after the player finished spawning.

The event can be called even for spectators (when the spectator gets respawned).

#### player_textMessage

The player sent a text message.

```
player_textMessage local.text local.is_team
```

Parameters:
- local.text: The raw text message the client sent to the server
- local.is_team: `1` if it's a team message. `0` otherwise (everyone)

The script can optionally return the following value:
- `0`: the message won't be sent.
- Any string value: the message won't be sent and a reply will be sent to the player.

---

## Extended Events

The following events use the same mechanism as the core player events above. For player-related events, `self` is the player object.

### Combat Events

#### player_kill

Triggered when a player kills another player. `self` is the **attacker**.

```
player_kill local.attacker local.victim local.inflictor local.location local.meansofdeath
```

#### player_death

Triggered when a player dies. `self` is the **victim**.

```
player_death local.inflictor
```

#### player_damage

Triggered when a player takes damage. `self` is the player who was damaged.

```
player_damage local.attacker local.damage local.meansofdeath
```

#### player_headshot

Triggered when a player scores a headshot. `self` is the **attacker**.

```
player_headshot local.victim local.weapon_name
```

#### player_pain

Triggered when a player takes pain. `self` is the player.

```
player_pain local.attacker local.damage local.meansofdeath local.location
```

#### player_suicide

Triggered when a player commits suicide. `self` is the player.

#### player_crushed

Triggered when a player is crushed. `self` is the victim.

```
player_crushed local.attacker
```

#### player_telefragged

Triggered when a player is telefragged. `self` is the victim.

```
player_telefragged local.attacker
```

#### player_roadkill

Triggered when a player is killed by a vehicle. `self` is the **attacker**.

```
player_roadkill local.victim
```

#### player_bash

Triggered when a player is melee bashed. `self` is the **attacker**.

```
player_bash local.victim
```

#### player_teamkill

Triggered when a player kills a teammate. `self` is the **killer**.

```
player_teamkill local.victim
```

### Weapon Events

For weapon events, `self` is the player who owns the weapon.

#### weapon_fire

Triggered when a weapon is fired.

```
weapon_fire local.weapon_name local.ammo_remaining
```

#### weapon_hit

Triggered when a weapon hits a target.

```
weapon_hit local.target local.hit_type
```

- local.hit_type: Either a body location (e.g., "head") or "projectile"

#### weapon_reload

Triggered when a weapon starts reloading.

```
weapon_reload local.weapon_name
```

#### weapon_reload_done

Triggered when a weapon finishes reloading.

```
weapon_reload_done local.weapon_name
```

#### weapon_change

Triggered when a player changes weapons.

```
weapon_change local.old_weapon local.new_weapon local.clientnum
```

#### weapon_ready

Triggered when a weapon becomes ready to fire.

```
weapon_ready local.weapon_name
```

#### weapon_no_ammo

Triggered when a weapon runs out of ammo.

```
weapon_no_ammo local.weapon_name
```

#### weapon_holster

Triggered when a weapon is holstered.

```
weapon_holster local.weapon_name
```

#### weapon_raise

Triggered when a weapon is raised.

```
weapon_raise local.weapon_name
```

#### weapon_drop

Triggered when a weapon is dropped. `self` is the previous owner.

```
weapon_drop local.weapon_entity
```

#### grenade_throw

Triggered when a grenade is thrown. `self` is the thrower.

```
grenade_throw local.projectile
```

#### grenade_explode

Triggered when a grenade explodes. `self` is the owner.

```
grenade_explode local.projectile
```

### Movement Events

For movement events, `self` is the player.

#### player_jump

Triggered when a player jumps.

#### player_land

Triggered when a player lands.

```
player_land local.fall_velocity
```

#### player_crouch

Triggered when a player crouches.

#### player_prone

Triggered when a player goes prone.

#### player_stand

Triggered when a player stands up.

#### player_distance

Triggered periodically with distance traveled. Also triggered on disconnect.

```
player_distance local.walked local.sprinted local.swam local.driven
```

### Interaction Events

For interaction events, `self` is the player.

#### ladder_mount

Triggered when a player mounts a ladder.

```
ladder_mount local.ladder
```

#### ladder_dismount

Triggered when a player dismounts a ladder.

```
ladder_dismount local.ladder
```

#### item_pickup

Triggered when a player picks up an item.

```
item_pickup local.item_name local.amount
```

#### item_drop

Triggered when a player drops an item.

```
item_drop local.item_name
```

#### player_use

Triggered when a player uses something.

```
player_use local.entity
```

#### player_use_object_start

Triggered when a player starts using an object (e.g., planting bomb).

```
player_use_object_start local.useobject
```

#### player_use_object_finish

Triggered when a player finishes using an object.

```
player_use_object_finish local.useobject
```

### Pickup Events

For pickup events, `self` is the player.

#### health_pickup

Triggered when a player picks up health.

```
health_pickup local.amount
```

#### ammo_pickup

Triggered when a player picks up ammo.

```
ammo_pickup local.item_name local.amount
```

### Session Events

#### client_connect

Triggered when a client connects. `self` is the player.

```
client_connect local.clientnum
```

#### client_disconnect

Triggered when a client disconnects. `self` is the player.

#### client_begin

Triggered when a client begins. `self` is the player.

#### client_userinfo_changed

Triggered when client userinfo changes.

#### team_join

Triggered when a player joins a team. `self` is the player.

```
team_join local.old_team local.new_team
```

#### player_say

Triggered when a player sends a chat message. `self` is the player.

```
player_say local.message
```

#### player_spawn

Triggered when a player spawns. `self` is the player.

#### player_respawn

Triggered when a player respawns. `self` is the player.

#### player_spectate

Triggered when a player becomes a spectator. `self` is the player.

#### player_freeze

Triggered when a player is frozen or unfrozen. `self` is the player.

```
player_freeze local.frozen_state
```

- local.frozen_state: `1` if frozen, `0` if unfrozen

#### player_inactivity_drop

Triggered when a player is dropped for inactivity. `self` is the player.

#### score_change

Triggered when a player's score changes. `self` is the player.

```
score_change local.type local.delta local.new_value
```

- local.type: "kills" or "deaths"

#### teamkill_kick

Triggered when a player is kicked for team killing. `self` is the player.

```
teamkill_kick local.teamkill_count
```

### Vehicle & Turret Events

For these events, `self` is the player.

#### vehicle_enter

Triggered when a player enters a vehicle.

```
vehicle_enter local.vehicle
```

#### vehicle_exit

Triggered when a player exits a vehicle.

```
vehicle_exit local.vehicle
```

#### vehicle_death

Triggered when a vehicle is destroyed.

#### vehicle_collision

Triggered when a vehicle collides.

#### turret_enter

Triggered when a player enters a turret.

```
turret_enter local.turret
```

#### turret_exit

Triggered when a player exits a turret.

```
turret_exit local.turret
```

### Game Flow Events

For game flow events, `self` is `NULL` (use `level` or `game` objects instead).

#### game_init

Triggered when the game initializes.

```
game_init local.gametype
```

#### game_start

Triggered when the game starts.

#### game_end

Triggered when the game ends.

#### round_start

Triggered when a round starts.

#### round_end

Triggered when a round ends.

#### warmup_start

Triggered when warmup starts.

```
warmup_start local.mapname
```

#### warmup_end

Triggered when warmup ends.

```
warmup_end local.mapname
```

#### team_win

Triggered when a team wins.

```
team_win local.team_number
```

#### objective_update

Triggered when an objective is updated.

#### objective_capture

Triggered when an objective is captured.

```
objective_capture local.controller
```

### Map Events

For map events, `self` is `NULL`.

#### map_init

Triggered when map initialization starts.

#### map_start

Triggered when map initialization is complete.

#### map_ready

Triggered when map is ready (all entities spawned).

```
map_ready local.mapname local.gametype
```

#### map_load_start

Triggered when map loading starts.

```
map_load_start local.mapname
```

#### map_load_end

Triggered when map loading ends.

```
map_load_end local.mapname local.gametype
```

#### map_restart

Triggered when the map is restarted.

```
map_restart local.mapname
```

#### map_change_start

Triggered when map change begins.

```
map_change_start local.current_map local.next_map
```

#### map_shutdown

Triggered when the map is shutting down.

### Match Events

#### match_end

Triggered when a match ends.

```
match_end local.mapname local.gametype local.winning_team
```

#### intermission_start

Triggered when intermission starts.

```
intermission_start local.mapname local.gametype
```

### Vote Events

#### vote_start

Triggered when a vote is called. `self` is the player who called the vote.

```
vote_start local.vote_name local.vote_string
```

#### vote_passed

Triggered when a vote passes.

```
vote_passed local.vote_name local.vote_string local.yes_count local.no_count
```

#### vote_failed

Triggered when a vote fails.

```
vote_failed local.vote_name local.reason local.yes_count local.no_count
```

- local.reason: "timeout" or "rejected"

### Server Events

These events are triggered once per server process, not per map.

#### server_process_start

Triggered when the server process starts (once per executable run).

#### server_process_quit

Triggered when the server process is quitting.

#### server_console_command

Triggered when a server console command is executed.

### Actor Events

For actor events, `self` is the actor.

#### actor_spawn

Triggered when an actor spawns.

#### actor_killed

Triggered when an actor is killed.

```
actor_killed local.attacker
```

### Bot Events

For bot events, `self` is the bot player entity.

#### bot_spawn

Triggered when a bot spawns.

#### bot_killed

Triggered when a bot is killed.

```
bot_killed local.attacker
```

#### bot_roam

Triggered when a bot enters roaming/idle state.

#### bot_curious

Triggered when a bot enters curious state.

#### bot_attack

Triggered when a bot enters attack state.

