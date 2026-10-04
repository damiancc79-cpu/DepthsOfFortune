# Depths of Fortune

A complete, asset-free 3D terminal roguelike written in standard C++17.

## Play

Double-click `play.command`, or run:

```sh
./play.command
```

The launcher compiles the single source file when needed, then starts the game.

## Controls

- `W/S` — move forward/back
- `A/D` or arrow keys — turn
- `Space` — attack the enemy in your crosshair
- `E` — buy from an adjacent merchant
- `R` — restart after victory or death
- `Q` — quit

Fight through five freshly generated dungeon floors. Enemies drop gold; merchants
sell stronger weapons; stairs (`>`) lead deeper. The right-hand map shows explored
geometry and nearby creatures. Everything is rendered with terminal characters:
there are no external assets, packages, or game-engine dependencies.
