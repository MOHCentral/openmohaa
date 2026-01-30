# OpenMoHAA Event Subscribe Reference

This document lists all events that are **actually implemented** in the codebase and can be used with `event_subscribe`.

**Total Implemented Events: 101**

---

## How to Use

Subscribe to events in MorpheusScript:
```
main:
    event_subscribe "event_name" handler_function
    // Can specify another script:
    //event_subscribe "event_name" global/my_script.scr::handler_function
end
```

---

# Original Player Events (6 events)

These are the original 6 player events from the base game. The `self` object is always the player.

---

## player_connected

```
main:
    event_subscribe "player_connected" event_player_connected
end

event_player_connected:
    iprintlnbold("Player " + self.entnum + " connected!")
end
```

**Description:** The player just connected to the server.

**Called when:**
- The player entity has been created and connected
- This is called once per connection

**Parameters:** None

**self:** The player object

---

## player_disconnecting

```
main:
    event_subscribe "player_disconnecting" event_player_disconnecting
end

event_player_disconnecting:
    iprintlnbold("Player " + self.entnum + " is disconnecting!")
end
```

**Description:** The player is disconnecting from the server.

**Called when:**
- The player is about to leave the server
- Called before the player entity is removed

**Parameters:** None

**self:** The player object

---

## player_spawned

```
main:
    event_subscribe "player_spawned" event_player_spawned
end

event_player_spawned:
    iprintlnbold("Player " + self.entnum + " just spawned!")
end
```

**Description:** The player has spawned.

**Called when:**
- The player has entered the battle
- The player respawned or spawned with weapons
- This is called after the player finished spawning
- Can be called even for spectators (when the spectator gets respawned)

**Parameters:** None

**self:** The player object

---

## player_damaged

```
main:
    event_subscribe "player_damaged" event_player_damaged
end

event_player_damaged local.attacker local.damage local.inflictor local.position local.direction local.normal local.knockback local.damageflags local.meansofdeath local.location:
    iprintlnbold("Player " + self.entnum + " took " + local.damage + " damage!")
end
```

**Description:** The player got hit and took damage.

**Called when:**
- The player receives damage from any source

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | The entity that caused the damage |
| local.damage | Amount of damage taken |
| local.inflictor | The entity that directly caused the damage (projectile, explosion, etc.) |
| local.position | Vector position where damage was applied |
| local.direction | Direction vector of the damage |
| local.normal | Surface normal at impact point |
| local.knockback | Knockback force applied |
| local.damageflags | Damage type flags |
| local.meansofdeath | Means of death identifier |
| local.location | Hit location on body |

**self:** The player object (victim)

---

## player_killed

```
main:
    event_subscribe "player_killed" event_player_killed
end

event_player_killed local.attacker local.damage local.inflictor local.position local.direction local.normal local.knockback local.damageflags local.meansofdeath local.location:
    iprintlnbold("Player " + self.entnum + " was killed by " + local.attacker.entnum)
end
```

**Description:** The player got killed.

**Called when:**
- The player's health reaches zero or below

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | The entity that caused the killing blow |
| local.damage | Amount of damage that killed the player |
| local.inflictor | The entity that directly caused the death |
| local.position | Vector position where fatal damage was applied |
| local.direction | Direction vector of the fatal damage |
| local.normal | Surface normal at impact point |
| local.knockback | Knockback force applied |
| local.damageflags | Damage type flags |
| local.meansofdeath | Means of death identifier |
| local.location | Hit location on body |

**self:** The player object (victim)

---

## player_textMessage

```
main:
    event_subscribe "player_textMessage" event_player_textMessage
end

event_player_textMessage local.text local.is_team:
    iprintlnbold("Player " + self.entnum + " said: " + local.text)
    // Return 0 to block the message
    // Return a string to block and reply to player
end
```

**Description:** The player sent a text message.

**Called when:**
- The player sends a chat message (team or global)

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.text | The raw text message the client sent |
| local.is_team | 1 if team message, 0 if global |

**self:** The player object

**Return Values:**
- `0`: The message won't be sent
- Any string: The message won't be sent and a reply will be sent to the player

---

# Extended Combat Events (23 events)

---

## player_kill

```
main:
    event_subscribe "player_kill" event_player_kill
end

event_player_kill local.attacker local.victim local.inflictor local.location local.meansofdeath:
    iprintlnbold(self.netname + " killed " + local.victim.netname)
end
```

**Description:** A player killed another player.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | The killer (duplicate of self) |
| local.victim | The player who died |
| local.inflictor | Entity that directly caused death |
| local.location | Hit location |
| local.meansofdeath | Means of death identifier |

**self:** The attacker

---

## player_death

```
main:
    event_subscribe "player_death" event_player_death
end

event_player_death local.inflictor:
    iprintlnbold("Player " + self.netname + " died")
end
```

**Description:** A player died.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.inflictor | Entity that caused death |

**self:** The victim

---

## player_damage

```
main:
    event_subscribe "player_damage" event_player_damage
end

event_player_damage local.attacker local.damage local.meansofdeath:
    iprintlnbold(self.netname + " took " + local.damage + " damage")
end
```

**Description:** A player took damage.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | The attacker entity |
| local.damage | Amount of damage |
| local.meansofdeath | Means of death identifier |

**self:** The victim

---

## player_pain

```
main:
    event_subscribe "player_pain" event_player_pain
end

event_player_pain local.attacker local.damage local.meansofdeath local.hit_location:
    iprintlnbold(self.netname + " hit in " + local.hit_location)
end
```

**Description:** A player experienced pain with hit location info.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | The attacker entity |
| local.damage | Amount of damage |
| local.meansofdeath | Means of death identifier |
| local.hit_location | Body part hit (integer) |

**self:** The victim

---

## player_headshot

```
main:
    event_subscribe "player_headshot" event_player_headshot
end

event_player_headshot local.victim local.weapon_name:
    iprintlnbold(self.netname + " headshot " + local.victim.netname + " with " + local.weapon_name)
end
```

**Description:** A player got a headshot kill.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.victim | The player who was headshot |
| local.weapon_name | Name of the weapon used |

**self:** The attacker

---

## player_suicide

```
main:
    event_subscribe "player_suicide" event_player_suicide
end

event_player_suicide:
    iprintlnbold(self.netname + " committed suicide")
end
```

**Description:** A player killed themselves.

**Parameters:** None

**self:** The player who suicided

---

## player_crushed

```
main:
    event_subscribe "player_crushed" event_player_crushed
end

event_player_crushed local.attacker:
    iprintlnbold(self.netname + " was crushed")
end
```

**Description:** A player was crushed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | Entity that crushed them |

**self:** The victim

---

## player_telefragged

```
main:
    event_subscribe "player_telefragged" event_player_telefragged
end

event_player_telefragged local.attacker:
    iprintlnbold(self.netname + " was telefragged by " + local.attacker.netname)
end
```

**Description:** A player was telefragged.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | Player who telefragged them |

**self:** The victim

---

## player_roadkill

```
main:
    event_subscribe "player_roadkill" event_player_roadkill
end

event_player_roadkill local.victim:
    iprintlnbold(self.netname + " ran over " + local.victim.netname)
end
```

**Description:** A player was killed by a vehicle.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.victim | The player who was run over |

**self:** The attacker

---

## player_bash

```
main:
    event_subscribe "player_bash" event_player_bash
end

event_player_bash local.victim:
    iprintlnbold(self.netname + " bashed " + local.victim.netname)
end
```

**Description:** A player got a melee bash kill.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.victim | The player who was bashed |

**self:** The attacker

---

## player_teamkill

```
main:
    event_subscribe "player_teamkill" event_player_teamkill
end

event_player_teamkill local.victim:
    iprintlnbold(self.netname + " team killed " + local.victim.netname)
end
```

**Description:** A player killed a teammate.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.victim | The teammate who was killed |

**self:** The killer

---

## weapon_fire

```
main:
    event_subscribe "weapon_fire" event_weapon_fire
end

event_weapon_fire local.weapon_name local.ammo_remaining:
    // self is the weapon owner
end
```

**Description:** A weapon was fired.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.weapon_name | Name of the weapon |
| local.ammo_remaining | Ammo left after firing |

**self:** The weapon owner

---

## weapon_hit

```
main:
    event_subscribe "weapon_hit" event_weapon_hit
end

event_weapon_hit local.target local.hit_location_or_type:
    iprintlnbold(self.netname + " hit " + local.target.netname)
end
```

**Description:** A weapon hit a target.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.target | The entity that was hit |
| local.hit_location_or_type | Hit location or "projectile" for projectile hits |

**self:** The weapon owner

---

## weapon_reload

```
main:
    event_subscribe "weapon_reload" event_weapon_reload
end

event_weapon_reload local.weapon_name:
    // Player started reloading
end
```

**Description:** A weapon started reloading.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.weapon_name | Name of the weapon |

**self:** The weapon owner

---

## weapon_reload_done

```
main:
    event_subscribe "weapon_reload_done" event_weapon_reload_done
end

event_weapon_reload_done local.weapon_name:
    // Reload complete
end
```

**Description:** A weapon finished reloading.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.weapon_name | Name of the weapon |

**self:** The weapon owner

---

## weapon_change

```
main:
    event_subscribe "weapon_change" event_weapon_change
end

event_weapon_change local.old_weapon local.new_weapon local.client_num:
    iprintlnbold(self.netname + " switched from " + local.old_weapon + " to " + local.new_weapon)
end
```

**Description:** A player switched weapons.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.old_weapon | Previous weapon name (or "none") |
| local.new_weapon | New weapon name |
| local.client_num | Client number |

**self:** The weapon owner

---

## weapon_ready

```
main:
    event_subscribe "weapon_ready" event_weapon_ready
end

event_weapon_ready local.weapon_name:
    // Weapon is ready to fire
end
```

**Description:** A weapon became ready.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.weapon_name | Name of the weapon |

**self:** The weapon owner

---

## weapon_no_ammo

```
main:
    event_subscribe "weapon_no_ammo" event_weapon_no_ammo
end

event_weapon_no_ammo local.weapon_name:
    iprintlnbold(self.netname + " is out of ammo for " + local.weapon_name)
end
```

**Description:** A weapon ran out of ammo.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.weapon_name | Name of the weapon |

**self:** The weapon owner

---

## weapon_holster

```
main:
    event_subscribe "weapon_holster" event_weapon_holster
end

event_weapon_holster local.weapon_name:
    // Weapon holstered
end
```

**Description:** A weapon was holstered.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.weapon_name | Name of the weapon |

**self:** The weapon owner

---

## weapon_raise

```
main:
    event_subscribe "weapon_raise" event_weapon_raise
end

event_weapon_raise local.weapon_name:
    // Weapon raised
end
```

**Description:** A weapon was raised.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.weapon_name | Name of the weapon |

**self:** The weapon owner

---

## weapon_drop

```
main:
    event_subscribe "weapon_drop" event_weapon_drop
end

event_weapon_drop local.weapon_entity:
    iprintlnbold(self.netname + " dropped a weapon")
end
```

**Description:** A weapon was dropped.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.weapon_entity | The dropped weapon entity |

**self:** The previous owner

---

## grenade_throw

```
main:
    event_subscribe "grenade_throw" event_grenade_throw
end

event_grenade_throw local.projectile_entity:
    iprintlnbold(self.netname + " threw a grenade")
end
```

**Description:** A grenade was thrown.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.projectile_entity | The grenade entity |

**self:** The thrower

---

## grenade_explode

```
main:
    event_subscribe "grenade_explode" event_grenade_explode
end

event_grenade_explode local.grenade_entity:
    // Grenade exploded
end
```

**Description:** A grenade exploded.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.grenade_entity | The grenade entity |

**self:** The thrower

---

# Movement Events (10 events)

---

## player_spawn

```
main:
    event_subscribe "player_spawn" event_player_spawn
end

event_player_spawn:
    iprintlnbold(self.netname + " spawned")
end
```

**Description:** A player spawned.

**Parameters:** None

**self:** The player

---

## player_respawn

```
main:
    event_subscribe "player_respawn" event_player_respawn
end

event_player_respawn:
    iprintlnbold(self.netname + " respawned")
end
```

**Description:** A player respawned.

**Parameters:** None

**self:** The player

---

## player_jump

```
main:
    event_subscribe "player_jump" event_player_jump
end

event_player_jump:
    // Player jumped
end
```

**Description:** A player jumped.

**Parameters:** None

**self:** The player

---

## player_land

```
main:
    event_subscribe "player_land" event_player_land
end

event_player_land local.fall_velocity:
    // Player landed with velocity local.fall_velocity
end
```

**Description:** A player landed after falling/jumping.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.fall_velocity | Z velocity when landing |

**self:** The player

---

## player_crouch

```
main:
    event_subscribe "player_crouch" event_player_crouch
end

event_player_crouch:
    // Player crouched
end
```

**Description:** A player crouched.

**Parameters:** None

**self:** The player

---

## player_prone

```
main:
    event_subscribe "player_prone" event_player_prone
end

event_player_prone:
    // Player went prone
end
```

**Description:** A player went prone.

**Parameters:** None

**self:** The player

---

## player_stand

```
main:
    event_subscribe "player_stand" event_player_stand
end

event_player_stand:
    // Player stood up
end
```

**Description:** A player stood up.

**Parameters:** None

**self:** The player

---

## player_distance

```
main:
    event_subscribe "player_distance" event_player_distance
end

event_player_distance local.walked local.sprinted local.swam local.driven:
    iprintlnbold(self.netname + " walked " + local.walked + " units")
end
```

**Description:** Player distance traveled update.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.walked | Distance walked |
| local.sprinted | Distance sprinted |
| local.swam | Distance swam |
| local.driven | Distance driven in vehicles |

**self:** The player

---

## ladder_mount

```
main:
    event_subscribe "ladder_mount" event_ladder_mount
end

event_ladder_mount local.ladder_entity:
    // Player got on a ladder
end
```

**Description:** A player mounted a ladder.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.ladder_entity | The ladder entity |

**self:** The player

---

## ladder_dismount

```
main:
    event_subscribe "ladder_dismount" event_ladder_dismount
end

event_ladder_dismount local.ladder_entity:
    // Player got off a ladder
end
```

**Description:** A player dismounted a ladder.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.ladder_entity | The ladder entity |

**self:** The player

---

# Interaction Events (6 events)

---

## player_use

```
main:
    event_subscribe "player_use" event_player_use
end

event_player_use local.target_entity:
    iprintlnbold(self.netname + " used something")
end
```

**Description:** A player used something.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.target_entity | The entity that was used |

**self:** The player

---

## player_use_object_start

```
main:
    event_subscribe "player_use_object_start" event_player_use_object_start
end

event_player_use_object_start local.use_object:
    // Player started using an object
end
```

**Description:** A player started using an object.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.use_object | The use object entity |

**self:** The player

---

## player_use_object_finish

```
main:
    event_subscribe "player_use_object_finish" event_player_use_object_finish
end

event_player_use_object_finish local.use_object:
    // Player finished using an object
end
```

**Description:** A player finished using an object.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.use_object | The use object entity |

**self:** The player

---

## player_spectate

```
main:
    event_subscribe "player_spectate" event_player_spectate
end

event_player_spectate:
    iprintlnbold(self.netname + " became a spectator")
end
```

**Description:** A player became a spectator.

**Parameters:** None

**self:** The player

---

## player_freeze

```
main:
    event_subscribe "player_freeze" event_player_freeze
end

event_player_freeze local.frozen_state:
    if (local.frozen_state == 1)
        iprintlnbold(self.netname + " is frozen")
    else
        iprintlnbold(self.netname + " is unfrozen")
    end
end
```

**Description:** A player was frozen or unfrozen.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.frozen_state | 1 if frozen, 0 if unfrozen |

**self:** The player

---

## player_say

```
main:
    event_subscribe "player_say" event_player_say
end

event_player_say local.message_text:
    iprintlnbold(self.netname + " said: " + local.message_text)
end
```

**Description:** A player sent a chat message (extended event).

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.message_text | The chat message text |

**self:** The player

---

# Item Events (6 events)

---

## item_pickup

```
main:
    event_subscribe "item_pickup" event_item_pickup
end

event_item_pickup local.item_name local.amount:
    iprintlnbold(self.netname + " picked up " + local.item_name)
end
```

**Description:** A player picked up an item.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.item_name | Name of the item |
| local.amount | Amount picked up |

**self:** The player

---

## item_drop

```
main:
    event_subscribe "item_drop" event_item_drop
end

event_item_drop local.item_name:
    iprintlnbold(self.netname + " dropped " + local.item_name)
end
```

**Description:** A player dropped an item.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.item_name | Name of the item |

**self:** The previous owner

---

## item_respawn

```
main:
    event_subscribe "item_respawn" event_item_respawn
end

event_item_respawn local.item_name:
    // Item respawned on map
end
```

**Description:** An item respawned on the map.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.item_name | Name of the item |

**self:** The item entity

---

## health_pickup

```
main:
    event_subscribe "health_pickup" event_health_pickup
end

event_health_pickup local.heal_amount:
    iprintlnbold(self.netname + " picked up " + local.heal_amount + " health")
end
```

**Description:** A player picked up health.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.heal_amount | Amount of health restored |

**self:** The player

---

## ammo_pickup

```
main:
    event_subscribe "ammo_pickup" event_ammo_pickup
end

event_ammo_pickup local.ammo_name local.amount:
    iprintlnbold(self.netname + " picked up " + local.amount + " " + local.ammo_name)
end
```

**Description:** A player picked up ammo.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.ammo_name | Name of the ammo type |
| local.amount | Amount picked up |

**self:** The player

---

## armor_pickup

```
main:
    event_subscribe "armor_pickup" event_armor_pickup
end

event_armor_pickup local.armor_amount:
    iprintlnbold(self.netname + " picked up " + local.armor_amount + " armor")
end
```

**Description:** A player picked up armor.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.armor_amount | Amount of armor |

**self:** The player

---

# Vehicle/Turret Events (6 events)

---

## vehicle_enter

```
main:
    event_subscribe "vehicle_enter" event_vehicle_enter
end

event_vehicle_enter local.vehicle_entity:
    iprintlnbold(self.netname + " entered a vehicle")
end
```

**Description:** A player entered a vehicle.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.vehicle_entity | The vehicle entity |

**self:** The player

---

## vehicle_exit

```
main:
    event_subscribe "vehicle_exit" event_vehicle_exit
end

event_vehicle_exit local.vehicle_entity:
    iprintlnbold(self.netname + " exited a vehicle")
end
```

**Description:** A player exited a vehicle.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.vehicle_entity | The vehicle entity |

**self:** The player

---

## vehicle_death

```
main:
    event_subscribe "vehicle_death" event_vehicle_death
end

event_vehicle_death local.attacker:
    iprintlnbold("Vehicle destroyed")
end
```

**Description:** A vehicle was destroyed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | Entity that destroyed the vehicle |

**self:** The vehicle entity

---

## vehicle_collision

```
main:
    event_subscribe "vehicle_collision" event_vehicle_collision
end

event_vehicle_collision local.other_entity:
    // Vehicle collided with something
end
```

**Description:** A vehicle collided with something.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.other_entity | Entity the vehicle collided with |

**self:** The vehicle entity

---

## turret_enter

```
main:
    event_subscribe "turret_enter" event_turret_enter
end

event_turret_enter local.turret_entity:
    iprintlnbold(self.netname + " entered a turret")
end
```

**Description:** A player entered a turret.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.turret_entity | The turret entity |

**self:** The player

---

## turret_exit

```
main:
    event_subscribe "turret_exit" event_turret_exit
end

event_turret_exit local.turret_entity:
    iprintlnbold(self.netname + " exited a turret")
end
```

**Description:** A player exited a turret.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.turret_entity | The turret entity |

**self:** The player

---

# Server Events (3 events)

---

## server_console_command

```
main:
    event_subscribe "server_console_command" event_server_console_command
end

event_server_console_command local.command_string:
    iprintlnbold("Server command: " + local.command_string)
end
```

**Description:** A console command was executed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.command_string | The command that was executed |

**self:** None (use level or world)

---

## server_process_start

```
main:
    event_subscribe "server_process_start" event_server_process_start
end

event_server_process_start:
    // Server executable just started (called once per process)
end
```

**Description:** The server process just started.

**Called when:** Once per server executable start (not per map)

**Parameters:** None

**self:** None

---

## server_process_quit

```
main:
    event_subscribe "server_process_quit" event_server_process_quit
end

event_server_process_quit:
    // Server executable is quitting (called once per process)
end
```

**Description:** The server process is quitting.

**Called when:** Once when the server executable is shutting down

**Parameters:** None

**self:** None

---

# Map Events (8 events)

---

## map_init

```
main:
    event_subscribe "map_init" event_map_init
end

event_map_init:
    // Map initialization started
end
```

**Description:** Map initialization has started.

**Parameters:** None

**self:** None

---

## map_start

```
main:
    event_subscribe "map_start" event_map_start
end

event_map_start:
    // Map initialization complete
end
```

**Description:** Map initialization is complete.

**Parameters:** None

**self:** None

---

## map_shutdown

```
main:
    event_subscribe "map_shutdown" event_map_shutdown
end

event_map_shutdown:
    // Map is shutting down
end
```

**Description:** The map is shutting down.

**Parameters:** None

**self:** None

---

## map_ready

```
main:
    event_subscribe "map_ready" event_map_ready
end

event_map_ready local.map_name local.gametype:
    iprintlnbold("Map " + local.map_name + " is ready, gametype " + local.gametype)
end
```

**Description:** The map is ready and all entities have spawned.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.map_name | Name of the map |
| local.gametype | Game type integer |

**self:** None

---

## map_load_start

```
main:
    event_subscribe "map_load_start" event_map_load_start
end

event_map_load_start local.map_name:
    iprintlnbold("Loading map " + local.map_name)
end
```

**Description:** A map is starting to load.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.map_name | Name of the map |

**self:** None

---

## map_load_end

```
main:
    event_subscribe "map_load_end" event_map_load_end
end

event_map_load_end local.map_name local.gametype:
    iprintlnbold("Map " + local.map_name + " loaded")
end
```

**Description:** A map finished loading.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.map_name | Name of the map |
| local.gametype | Game type integer |

**self:** None

---

## map_change_start

```
main:
    event_subscribe "map_change_start" event_map_change_start
end

event_map_change_start local.current_map local.next_map:
    iprintlnbold("Changing from " + local.current_map + " to " + local.next_map)
end
```

**Description:** A map change has started.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.current_map | Current map name |
| local.next_map | Next map name |

**self:** None

---

## map_restart

```
main:
    event_subscribe "map_restart" event_map_restart
end

event_map_restart local.map_name:
    iprintlnbold("Map " + local.map_name + " restarted")
end
```

**Description:** The map was restarted.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.map_name | Name of the map |

**self:** None

---

# Game Flow Events (11 events)

---

## game_init

```
main:
    event_subscribe "game_init" event_game_init
end

event_game_init local.gametype:
    iprintlnbold("Game initialized with gametype " + local.gametype)
end
```

**Description:** The game has been initialized.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.gametype | Game type integer |

**self:** None

---

## game_start

```
main:
    event_subscribe "game_start" event_game_start
end

event_game_start:
    iprintlnbold("Game started!")
end
```

**Description:** The game has started.

**Parameters:** None

**self:** None

---

## game_end

```
main:
    event_subscribe "game_end" event_game_end
end

event_game_end:
    iprintlnbold("Game ended!")
end
```

**Description:** The game has ended.

**Parameters:** None

**self:** None

---

## round_start

```
main:
    event_subscribe "round_start" event_round_start
end

event_round_start:
    iprintlnbold("Round started!")
end
```

**Description:** A round has started.

**Parameters:** None

**self:** None

---

## round_end

```
main:
    event_subscribe "round_end" event_round_end
end

event_round_end:
    iprintlnbold("Round ended!")
end
```

**Description:** A round has ended.

**Parameters:** None

**self:** None

---

## team_win

```
main:
    event_subscribe "team_win" event_team_win
end

event_team_win local.winning_team_num:
    iprintlnbold("Team " + local.winning_team_num + " wins!")
end
```

**Description:** A team has won.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.winning_team_num | The winning team number |

**self:** None

---

## match_end

```
main:
    event_subscribe "match_end" event_match_end
end

event_match_end local.map_name local.gametype local.winning_team:
    iprintlnbold("Match ended on " + local.map_name)
end
```

**Description:** The match has ended.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.map_name | Name of the map |
| local.gametype | Game type integer |
| local.winning_team | The winning team |

**self:** None

---

## intermission_start

```
main:
    event_subscribe "intermission_start" event_intermission_start
end

event_intermission_start local.map_name local.gametype:
    // Intermission has begun
end
```

**Description:** Intermission has begun.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.map_name | Name of the map |
| local.gametype | Game type integer |

**self:** None

---

## warmup_start

```
main:
    event_subscribe "warmup_start" event_warmup_start
end

event_warmup_start local.map_name:
    iprintlnbold("Warmup started on " + local.map_name)
end
```

**Description:** Warmup period has begun.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.map_name | Name of the map |

**self:** None

---

## warmup_end

```
main:
    event_subscribe "warmup_end" event_warmup_end
end

event_warmup_end local.map_name:
    iprintlnbold("Warmup ended on " + local.map_name)
end
```

**Description:** Warmup period has ended.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.map_name | Name of the map |

**self:** None

---

## objective_update

```
main:
    event_subscribe "objective_update" event_objective_update
end

event_objective_update local.objective_index local.new_status:
    iprintlnbold("Objective " + local.objective_index + " changed to " + local.new_status)
end
```

**Description:** An objective status has changed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.objective_index | Index of the objective |
| local.new_status | New status value |

**self:** None

---

# Team/Vote Events (5 events)

---

## team_join

```
main:
    event_subscribe "team_join" event_team_join
end

event_team_join local.old_team local.new_team:
    iprintlnbold(self.netname + " changed from team " + local.old_team + " to " + local.new_team)
end
```

**Description:** A player changed teams.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.old_team | Previous team |
| local.new_team | New team |

**self:** The player

---

## vote_start

```
main:
    event_subscribe "vote_start" event_vote_start
end

event_vote_start local.vote_name local.vote_string:
    iprintlnbold(self.netname + " started vote: " + local.vote_name)
end
```

**Description:** A vote has started.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.vote_name | Name of the vote type |
| local.vote_string | The vote string |

**self:** The player who started the vote

---

## vote_passed

```
main:
    event_subscribe "vote_passed" event_vote_passed
end

event_vote_passed local.vote_name local.vote_string local.yes_count local.no_count:
    iprintlnbold("Vote passed: " + local.vote_name + " (" + local.yes_count + " yes, " + local.no_count + " no)")
end
```

**Description:** A vote has passed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.vote_name | Name of the vote type |
| local.vote_string | The vote string |
| local.yes_count | Number of yes votes |
| local.no_count | Number of no votes |

**self:** None

---

## vote_failed

```
main:
    event_subscribe "vote_failed" event_vote_failed
end

event_vote_failed local.vote_name local.fail_reason local.yes_count local.no_count:
    iprintlnbold("Vote failed: " + local.vote_name + " (" + local.fail_reason + ")")
end
```

**Description:** A vote has failed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.vote_name | Name of the vote type |
| local.fail_reason | Reason for failure ("timeout", "rejected") |
| local.yes_count | Number of yes votes |
| local.no_count | Number of no votes |

**self:** None

---

## objective_capture

```
main:
    event_subscribe "objective_capture" event_objective_capture
end

event_objective_capture local.controller_team:
    iprintlnbold("Objective captured by team " + local.controller_team)
end
```

**Description:** A TOW objective was captured.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.controller_team | The team that captured it |

**self:** The objective entity

---

# Client Events (5 events)

---

## client_connect

```
main:
    event_subscribe "client_connect" event_client_connect
end

event_client_connect local.client_num:
    iprintlnbold("Client " + local.client_num + " connected")
end
```

**Description:** A client connected.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.client_num | Client number |

**self:** None (player entity not yet created)

---

## client_begin

```
main:
    event_subscribe "client_begin" event_client_begin
end

event_client_begin:
    iprintlnbold(self.netname + " began")
end
```

**Description:** A client began (entered the game).

**Parameters:** None

**self:** The player entity

---

## client_userinfo_changed

```
main:
    event_subscribe "client_userinfo_changed" event_client_userinfo_changed
end

event_client_userinfo_changed:
    iprintlnbold(self.netname + " changed userinfo")
end
```

**Description:** A client's userinfo changed.

**Parameters:** None

**self:** The player entity

---

## client_disconnect

```
main:
    event_subscribe "client_disconnect" event_client_disconnect
end

event_client_disconnect:
    iprintlnbold(self.netname + " disconnected")
end
```

**Description:** A client disconnected.

**Parameters:** None

**self:** The player entity

---

## player_inactivity_drop

```
main:
    event_subscribe "player_inactivity_drop" event_player_inactivity_drop
end

event_player_inactivity_drop:
    iprintlnbold(self.netname + " was dropped for inactivity")
end
```

**Description:** A player was dropped for inactivity.

**Parameters:** None

**self:** The player entity

---

# World Events (3 events)

---

## door_open

```
main:
    event_subscribe "door_open" event_door_open
end

event_door_open local.activator:
    // Door was opened
end
```

**Description:** A door was opened.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.activator | Entity that opened the door |

**self:** The door entity

---

## door_close

```
main:
    event_subscribe "door_close" event_door_close
end

event_door_close:
    // Door was closed
end
```

**Description:** A door was closed.

**Parameters:** None

**self:** The door entity

---

## explosion

```
main:
    event_subscribe "explosion" event_explosion
end

event_explosion local.attacker local.damage:
    // Explosion occurred
end
```

**Description:** An explosion occurred.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | Entity that caused the explosion |
| local.damage | Explosion damage |

**self:** The explosion entity

---

# AI/Actor Events (7 events)

---

## actor_spawn

```
main:
    event_subscribe "actor_spawn" event_actor_spawn
end

event_actor_spawn:
    // AI actor spawned
end
```

**Description:** An AI actor spawned.

**Parameters:** None

**self:** The actor entity

---

## actor_killed

```
main:
    event_subscribe "actor_killed" event_actor_killed
end

event_actor_killed local.attacker:
    // AI actor was killed
end
```

**Description:** An AI actor was killed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | Entity that killed the actor |

**self:** The actor entity

---

## bot_spawn

```
main:
    event_subscribe "bot_spawn" event_bot_spawn
end

event_bot_spawn:
    iprintlnbold("Bot spawned")
end
```

**Description:** A bot spawned.

**Parameters:** None

**self:** The bot player entity

---

## bot_killed

```
main:
    event_subscribe "bot_killed" event_bot_killed
end

event_bot_killed local.attacker:
    iprintlnbold("Bot killed by " + local.attacker.netname)
end
```

**Description:** A bot was killed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.attacker | Entity that killed the bot |

**self:** The bot player entity

---

## bot_roam

```
main:
    event_subscribe "bot_roam" event_bot_roam
end

event_bot_roam:
    // Bot entered roaming state
end
```

**Description:** A bot entered roaming state.

**Parameters:** None

**self:** The bot player entity

---

## bot_curious

```
main:
    event_subscribe "bot_curious" event_bot_curious
end

event_bot_curious:
    // Bot entered curious state
end
```

**Description:** A bot entered curious state.

**Parameters:** None

**self:** The bot player entity

---

## bot_attack

```
main:
    event_subscribe "bot_attack" event_bot_attack
end

event_bot_attack:
    // Bot entered attack state
end
```

**Description:** A bot entered attack state.

**Parameters:** None

**self:** The bot player entity

---

# Score/Admin Events (2 events)

---

## score_change

```
main:
    event_subscribe "score_change" event_score_change
end

event_score_change local.score_type local.delta local.new_total:
    iprintlnbold(self.netname + " " + local.score_type + " changed by " + local.delta + " to " + local.new_total)
end
```

**Description:** A player's score changed.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.score_type | "kills" or "deaths" |
| local.delta | Amount changed |
| local.new_total | New total value |

**self:** The player

---

## teamkill_kick

```
main:
    event_subscribe "teamkill_kick" event_teamkill_kick
end

event_teamkill_kick local.teamkill_count:
    iprintlnbold(self.netname + " kicked for " + local.teamkill_count + " team kills")
end
```

**Description:** A player was kicked for excessive team killing.

**Parameters:**
| Parameter | Description |
|-----------|-------------|
| local.teamkill_count | Number of team kills |

**self:** The player who was kicked
