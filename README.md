# TechnoViz (Unreal Engine 5.8)

A real-time visualizer built only for techno. It listens to what your PC is playing (WASAPI loopback) and/or an
input device (mixer, interface, mic), tracks kick, bass, hats, tempo and drops with ~1 ms analysis resolution, and
drives a single, dense scene: **the Reactor**.

## The Reactor scene

- Five counter-rotating emissive reactor rings around a pulsing core light (shadows cast across the room)
- 64 spectrum pillars, 4 LED walls (4,000 "pixels") with beat-synced patterns
- Box truss with 24 moving-head beams and 8 strobes in volumetric fog
- 20 lasers firing on the off-beat, sweeping, tunnelling and flipping by phrase
- 56 physics blocks (Chaos) launched by every kick and blown apart on drops
- Niagara: ground smoke, dust in the beams, orbiting embers, kick sparks, floor shockwaves, hat glitter,
  CO2 jets, drop blast (see `Docs/NIAGARA_FX.md`)
- Camera director: 5 shot types, cuts every 8 bars and on drops, kick shake and FOV punch
- Post: bloom, exposure punch, chromatic aberration on kicks, white-out on drops; palettes change every 16 bars

## Timing

Audio is captured on a time-critical MMCSS "Pro Audio" thread in event-driven shared mode (one device period,
usually 10 ms) and analysed immediately. Once the tempo locks, kicks are **predicted** on a phase-locked beat grid
(`Beat lookahead`, default 20 ms) so hits land on screen together with the sound instead of a frame late.
The game runs without vsync, frame cap or render-thread lag (`r.OneFrameThreadLag=0`, `r.GTSyncType=1`).

`Scripts/analyzer_test.cpp` checks the analysis offline (no Unreal needed): tempo, beat-grid error vs the true kicks,
and drop timing at 122-150 BPM.

## Controls

| Key | Action |
|---|---|
| 1 / 2 / 3 / 4 | Listen to PC output / input device / both / built-in demo loop |
| [ ] | Previous / next input device |
| ; ' | Previous / next output device |
| - = | Detection sensitivity |
| , . | Beat lookahead (sync fine-tune) |
| C / A | Next camera shot / toggle auto camera |
| P | Next colour palette |
| Up / Down | Punch (reaction strength) |
| PgUp / PgDn | Exposure |
| F11 | Fullscreen |
| H | Overlay |
| Esc | Quit |

Run with `-TechnoDemo` to start on the built-in loop.

## Building

```
Scripts\Build.bat editor     (compile the C++ module)
Scripts\Build.bat setup      (create materials and the L_Reactor level)
Scripts\Build.bat package    (Shipping build into Packaged\)
```
`UE_ROOT` defaults to `C:\Program Files\Epic Games\UE_5.8`.
The editor auto-starts the Unreal MCP server (`.mcp.json`), which is used to author the Niagara systems.
