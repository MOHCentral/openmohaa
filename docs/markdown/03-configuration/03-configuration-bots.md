# Bot settings

## Global settings

### `g_bot_initial_spawn_delay`

- **Default**: 0
- **Type**: float (seconds)

#### Description

This sets how long the game should wait before spawning bots after loading a new map.

#### Usage

- `0`: Bots spawn instantly at map start (default).
- `5`: Bots spawn 5 seconds after the map begins.

#### Notes

- Applies only once when a new map has finished loading. It is not triggered on restarts or between rounds.
- Doesn't affect individual bot respawns during gameplay.

## Skill system

### `g_bot_skill`

- **Default**: 0.5
- **Type**: float (0.0 - 1.0)

#### Description

Controls the overall bot skill level, affecting multiple AI behaviors:

- **Aim accuracy**: Lower skill = more aim spread
- **Reaction time**: Higher skill = faster burst cooldowns
- **Burst control**: Higher skill = longer, more controlled bursts
- **Strafe dodging**: Higher skill = faster, more unpredictable movement
- **Jump dodging**: Higher skill = more frequent jump-dodges
- **Voice communication**: Higher skill = more frequent tactical callouts
- **Suppressive fire**: Higher skill = denser, more sustained suppression
- **Grenade decisions**: Lower skill = more hesitation with grenades

#### Usage

- `0.0`: Easiest - slow reactions, poor aim, predictable movement
- `0.5`: Medium difficulty (default)
- `1.0`: Hardest - fast reactions, accurate aim, aggressive tactics

#### Notes

This setting works in combination with other bot attack settings like `g_bot_attack_spreadmult`. The effective spread multiplier is calculated as:
```
effective_spread = g_bot_attack_spreadmult * (2.0 - g_bot_skill)
```

## Altering behavior

There is no skill system yet, however some settings can be modified to alter bot difficulty:

### `g_bot_attack_burst_min_time`

- **Default**: 0.1
- **Type**: float (seconds)

#### Description

Minimum time to pause firing (burst).

### `g_bot_attack_burst_random_delay`

- **Default**: 0.5
- **Type**: float (seconds)

#### Description

Random time added to pause firing (burst).

### `g_bot_attack_continuousfire_min_firetime`

- **Default**: 0.5
- **Type**: float (seconds)

#### Description

Minimum duration of continuous firing.

### `g_bot_attack_continuousfire_random_firetime`

- **Default**: 1.5
- **Type**: float (seconds)

#### Description

Random time added to the continuous firing duration.

### `g_bot_attack_react_min_delay`

- **Default**: 0.2
- **Type**: float (seconds)

#### Description

The minimum delay before shooting the enemy.

### `g_bot_attack_react_random_delay`

- **Default**: 1.0
- **Type**: float (seconds)

#### Description

Random delay added before shooting the enemy.

### `g_bot_attack_spreadmult`

- **Default**: 1.0
- **Type**: float

#### Description

Controls how accurate bots are when shooting.

#### Usage

- Lower values (< 1.0): More accurate, more likely to land headshots.
- Higher values (> 1.0): Less accurate, more likely to miss their target.

### `g_bot_turn_speed`

- **Default**: 15
- **Type**: float (degrees)

#### Description

The rate of degrees per second when turning.

### `g_bot_instamsg_chance`

- **Default**: 5
- **Type**: integer

#### Description

The chance at which the bot sends an instant message when shooting.

#### Usage

- 0: Disable.
- higher values: Less frequent messages.

### `g_bot_instamsg_delay`

- **Default**: 5.0
- **Type**: float (seconds)

#### Description

The minimum delay between instant messages.
## Tactical AI features

Bots have an advanced tactical AI system that makes them behave more like real soldiers. These behaviors are automatically enabled and scale with `g_bot_skill`:

### Combat behaviors

- **Strafe dodging**: Bots strafe left/right during combat to make themselves harder targets
- **Jump dodging**: Occasionally jump when changing strafe direction
- **Burst fire control**: Fire in controlled bursts rather than continuous streams
- **Retreat behavior**: Low health bots will retreat and seek cover
- **Smart reloading**: Reload only when safe (in cover, enemy far away, or no enemy)

### Grenade usage

Bots use grenades tactically:

- **Cluster targets**: Only throw grenades when 2+ enemies are grouped together
- **Room clearing**: Throw grenades into rooms where enemies are hiding (bot must be safely outside)
- **Smoke grenades**: Use smoke for cover when flanking or advancing across open ground
- **Grenade avoidance**: Flee from incoming grenades when detected

### Squad coordination

When multiple bots are on the same team, they coordinate attacks:

- **SUPPRESS role**: Stay in place and provide covering fire
- **FLANK_LEFT/RIGHT roles**: Attempt flanking maneuvers around enemy positions
- **ASSAULT role**: Direct engagement

### Voice callouts

Bots communicate using voice messages:

- **"Enemy spotted!"**: When first detecting an enemy
- **"Grenade! Take Cover!"**: When throwing a grenade
- **"Reloading!"**: When reloading their weapon
- **"Suppressing!"**: When providing suppressive fire
- **"Popping smoke!"**: When throwing smoke grenades

Voice callout frequency is affected by `g_bot_skill` - higher skill bots communicate more often.

### Suppressive fire

When bots lose sight of an enemy, they will:

1. Remember the last known enemy position
2. Fire at that position for 1.5-3 seconds (duration scales with skill)
3. Announce "Suppressing!" or "Covering!" to teammates

### Weapon-specific combat positioning

Bots adjust their preferred combat distance based on their equipped weapon:

- **Sniper rifles** (Kar98, Springfield, Mosin): Prefer 1200 units, will back away if enemy closes in
- **Standard rifles**: Prefer 800 units
- **SMGs** (Thompson, MP40, etc.): Prefer 400 units, will actively close distance to enemy
- **Machine guns**: Prefer 500 units
- **Pistols**: Prefer 300 units

### Pickup awareness

Bots will seek out health and ammo pickups when needed:

- **Health seeking**: When below 50% health, bots will look for nearby health pickups (within 1500 units)
- **Ammo seeking**: When clip is below 25% and no reserve ammo, bots seek ammo pickups
- **Combat priority**: Bots won't seek ammo if enemies are too close (< 500 units)

### Cover-to-cover movement

When advancing on an enemy position in open terrain, bots will:

1. Detect if they are in open terrain (few obstructions nearby)
2. Find cover positions ahead along their path
3. Move from cover to cover rather than running directly at enemies
4. Pause briefly at each cover position before advancing to the next

### Sound awareness

Bots can hear sounds and react to them even without visual contact:

- **Sound types**: Bots respond to weapon fire, explosions, footsteps, voice callouts, and grenades
- **Priority system**: Different sounds have different priorities:
  - Explosions: Highest priority (1.0)
  - Weapon fire: Very high (0.9)
  - Grenades: High (0.85)
  - Weapon impacts: Medium (0.7)
  - Urgent voices: Medium (0.6)
  - Normal voices: Low (0.4)
  - Footsteps: Low (0.2)
- **Skill-based hearing**: Higher skill bots have better hearing range (1.0x to 1.5x) and are more likely to detect quiet sounds
- **Investigation behavior**: When bots hear a significant sound and have no visual enemy:
  - They enter a "curious" state and investigate the sound source
  - High-priority sounds (gunfire, explosions) trigger tactical investigation using cover
  - Low-priority sounds (footsteps) result in simpler direct investigation
- **Sound memory**: Bots remember multiple sounds (up to 5) for 15 seconds
- **Squad sharing**: Sound locations are available for squad coordination

## Performance considerations

The bot AI system is designed for efficiency:

### Update staggering

Expensive operations are staggered across frames based on entity number:
- Not all bots update their full AI on every frame
- Critical systems (enemy scanning, projectile dodging) run every frame
- Non-critical systems (squad coordination, weapon selection) run less frequently

### Distance pre-filtering

Before expensive visibility checks, bots use squared distance comparisons:
- Players beyond 4096 units are skipped for visibility checks
- This avoids expensive trace operations for distant players

### Timing-based throttling

All subsystems have configurable timing intervals:
- Enemy memory updates: 100ms
- Projectile scanning: 200ms (safety-critical)
- Pickup scanning: 500ms
- Weapon selection: 1000ms
- Squad coordination: 2000ms

### Early-out conditions

Bots skip processing when:
- Dead or spectating
- No valid enemies in range
- Already in a high-priority state (fleeing grenade, etc.)
