# Bottom maps and party panel

`[touch] map_tap_move=1` (default) enables tap movement on the live dashboard
map and automap, and tap travel on the world map. Display preferences exposes
this switch. Panning and pinching do not issue movement commands. Local taps
use the player movement command and engine pathfinder; blocked destinations
are ignored. The amber ring marks the current walking destination. Tapping
an automap destination closes that paused screen so the walk can run.

In dual-screen mode, the world map and town entrance map use the bottom
surface. The dashboard MAP / PARTY navigation opens a scrolling companion
list with game-art portraits, HP, equipped weapon and armor, loaded ammo,
disposition and all six combat-control settings. Tapping a companion invokes
the game's normal interaction (talk/control or loot, according to that
critter). Companion dialogue opens directly on its existing control tab;
its barter and talk tabs remain available.

## Reproduce

Run these from the lane worktree with its existing private game-data rig:

```sh
timeout 245 .rig/run.sh mapparty-verified mappartyproof
timeout 245 .rig/run.sh worldmap-bottom dualworldmapproof
timeout 245 .rig/run.sh party-classic dualpartyproof bottom_theme=1
```

`mappartyproof` sends bottom-surface finger events to reachable live-map and
full-automap tiles, asserts the player moved, taps a visible wall and asserts
no movement, then equips Vic with a pistol and jacket, opens PARTY and taps
him into his game's control tab. The last check asserts weapon, armor and
12 loaded rounds. `dualpartyproof` runs that companion portion alone.
`dualworldmapproof` exercises town entrances, map dragging, towns, menu,
then taps terrain on the active map surface and asserts travel began.

Screenshots are under `.rig/shots/<run>/`; every capture has a top PNG and
`_bottom.png`. Useful captures: `mp01_target_bottom.png`,
`mp03b_full_map_bottom.png`, `mp09_party_bottom.png`,
`mp12_controls_bottom.png`, and `w13_travelling_bottom.png`.
No game assets or generated runtime files belong in the commit.
