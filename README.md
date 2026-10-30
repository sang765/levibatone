# LeviBatone

A Baritone-style pathfinding bot for **Minecraft: Bedrock Edition**, packaged as a native mod for **LeviLauncher** (and its forks). It is a ground-up C++ port of Baritone's core pathing ideas onto the Bedrock client — not a line-by-line translation — built on the verified 1.26.60 arm64 SDK layer from [BedrockTools](https://github.com/QYCottage/BedrockTools).

## Features

- **goto** — A\* pathing to a position (`goto <x> [y] <z>`), with best-so-far partial paths when the destination is out of the search budget
- **mine** — find blocks by name in a radius, path to them and break them (`mine <block> [count]`), with unreachable-target blacklisting; scanning runs on a worker thread
- **getto** — path to the nearest block of a type without breaking it (`getto <block>`)
- **follow** — follow the nearest player (`follow`) or a player by name (`follow <name>`, matched against the player list name, color codes stripped)
- **Movements** — walk, diagonal (corner-cut safe), ascend (jump), fall (≤ 3 blocks, land in water allowed), mine-down, pillar (place under feet), bridge (sneak-place over gaps with a safe landing below), ladder up/down, swim up
- **Block interaction** — breaks and places blocks through the game's own GameMode functions, honoring `allowBreak` / `allowPlace`
- **Costs** — Baritone's exact ActionCosts model (walk 20/4.317, jump = fall(1.25) − fall(0.25), fall simulation, placement/break/jump penalties) and the 3.563 cost heuristic
- **HUD** — status overlay (mode, path progress, mined count, messages) plus a floating STOP button
- **Settings** — Baritone-named settings (`set allowBreak false`, `set maxNodes 20000`, ...)

## Install

1. Build or download `LeviBatone.levipack` (see below).
2. In LeviLauncher: **Mods → Add Mod**, import the `.levipack`.
3. Launch the game, open the mod menu, enable **LeviBatone**.
4. **Important:** interact with the world once (break or place any block) — the mod learns your GameMode instance from the first interaction, which unlocks bot block breaking/placing.
5. In the LeviBatone module, type a command into the *Command* field prefixed with `!`, e.g. `!goto 100 64 -200`, `!mine iron_ore`, `!stop`.

## Commands

| Command | Effect |
|---|---|
| `goto <x> [y] <z>` | Path to a position (y optional → GoalXZ) |
| `mine <block> [count]` | Find and mine blocks by name (`iron_ore` also matches `deepslate_iron_ore`) |
| `getto <block>` | Walk to the nearest block of a type without mining it |
| `follow` | Follow the nearest player (keep `followRadius` distance) |
| `stop` / `cancel` | Cancel the bot |
| `status` | Current mode, goal, path progress |
| `set <key> <value>` | Change a setting |
| `settings` | List all settings |

## Settings (defaults = Baritone)

`allowBreak=true`, `allowPlace=true`, `allowSprint=true`, `allowDownward=true`, `maxFallHeightNoWater=3`, `costHeuristic=3.563`, `blockPlacementPenalty=20`, `blockBreakAdditionalPenalty=2`, `jumpPenalty=2`, `walkOnWaterOnePenalty=3`, `mineGoalUpdateInterval=5`, `movementTimeoutTicks=100`, `maxNodes=15000`, `primaryTimeoutMs=300`, `failureTimeoutMs=1500`, `mineScanRadius=24`, `followRadius=3`, `followRange=30`, `followUpdateInterval=10`

## Build

### On-device (Termux, arm64 Android)

```sh
pkg install clang python git
git clone --depth 1 https://github.com/LiteLDev/preloader-android ../sdk-preloader
# vendored deps (fmt + entt) into third_party/:
git clone --depth 1 --branch 11.2.0 https://github.com/fmtlib/fmt.git levibatone/third_party/fmt
git clone --depth 1 https://github.com/skypjack/entt.git levibatone/third_party/entt
cd levibatone && ./build.sh
```

Produces `build/liblevibatone.so` and `build/LeviBatone.levipack`.

### CI / desktop

`xmake f -y -p android -a arm64-v8a -m release --ndk=<ndk> && xmake -y` (GitHub Actions workflow included).

## Compatibility

Target: **Minecraft 1.26.60** (arm64-v8a) via LeviLauncher. All signature patterns were verified to resolve uniquely against a real 1.26.60.4 `libminecraftpe.so` with `verify/scan_fast.py` (point it at the extracted game library). Signatures and offsets come from the BedrockTools 1.26.60 SDK layer; other Minecraft versions are not covered by `minecraft_versions: ["1.26.60.X"]`.

## Not ported (yet)

Baritone has far more than the core; these are intentionally out of scope for v1: builder/schematics, farm, elytra, parkour and diagonal-descend movements, inventory/auto-tool management, cached-chunk pathing, and world-space path rendering. `mine` auto-explores toward unexplored ground when no target is in range (`exploreForBlocks`, up to 12 attempts). Settings persist to `config/settings.txt`.

## Credits

- [Baritone](https://github.com/cabaletta/baritone) (LGPL-3.0) — the original pathfinding bot; cost model and behavior model are ported from it
- [BedrockTools](https://github.com/QYCottage/BedrockTools) (MIT) — the verified 1.26.60 arm64 signature/offset SDK layer this mod builds on
- [LiteLDev/preloader-android](https://github.com/LiteLDev/preloader-android) — the LeviLauncher native mod runtime

## License

LGPL-3.0 (see LICENSE) — matching Baritone, from which the pathing model is derived. See NOTICE for attribution details.
