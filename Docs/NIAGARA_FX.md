# Niagara FX for the Reactor scene

The stage director (`ATechnoStageDirector`) loads these systems by path and drives them.
A missing system is skipped, so they can be built one at a time. All live in `/Game/TechnoViz/FX/`.

## Parameters every system receives

Add the ones a system needs as **User** parameters (exact names, Niagara user namespace):

| Name | Type | Meaning |
|---|---|---|
| Kick, Bass, Mid, Hats | float | band envelopes, auto-gained 0..1, already scaled by Punch |
| KickPulse | float | 1 on the kick (predicted on the beat grid when locked), decays over ~1/3 beat |
| HatPulse | float | 1 on a hat/clap transient, decays in ~50 ms |
| DropPulse | float | 1 when the kick returns after a breakdown, decays over ~3 s |
| Breakdown | float | 0..1 while the kick is gone |
| Energy, Intensity | float | loudness / how hard the section is, 0..1 |
| BeatPhase, BarPhase | float | 0..1 position inside the beat / 4-beat bar |
| Bpm | float | tempo |
| Strength | float | how hard this trigger hit (bursts), 0..1+ |
| KickCount, BeatCount | int | counters |
| ColorA, ColorB | linear color | current palette (A = main, B = accent) |
| Spectrum | Array Float data interface | 32 log bands 30 Hz to 16 kHz, 0..1 |

Continuous systems also get `CustomTimeDilation = 1 + KickPulse * pump`, so their whole sim surges on every kick.
Burst systems are spawned fresh (pooled) on their trigger with parameters already set; make them one-shot
(spawn burst, finite lifetime, system completes and frees itself).

## Systems

Scale: 1 unit = 1 cm. Reactor core at (0,0,520), rings radius 420-560, spectrum pillars at radius 1150,
truss square ±1500 at z 1900, LED walls at ±3400. Lights are mostly narrow spot beams in volumetric fog, so
lit translucent particles look great when beams pass through them.

1. **NS_GroundSmoke** (continuous, 6 emitters at radius 850 on the floor, facing outward)
   Thick, low-lying stage smoke rolling out from the reactor. Lit translucent sprites with soft particles,
   large (300-900), slow rotation, lifetime 6-8 s, drag, stays below ~250 cm. Spawn 15 + 60*Kick per second,
   outward velocity 40-120 plus 300*KickPulse. Tint slightly by ColorA (x0.15). Must not wash the scene white.

2. **NS_BeamDust** (continuous, one at (0,0,900))
   GPU dust motes filling the room (box 6000 x 6000 x 2400), ~20k particles, size 0.8-2.5, lit translucent so
   they sparkle inside light beams. Slow curl noise whose strength scales with 1 + 2*Bass.

3. **NS_ReactorEmbers** (continuous, at the core)
   GPU embers orbiting the core (vortex around Z, radius 300-900), rising slowly, curl noise. Spawn 200/s
   plus a burst of 600*KickPulse. Emissive ColorA * 30, size 2-6, lifetime 2-4 s. Optional light renderer
   on a few percent of particles.

4. **NS_KickSparks** (burst on every kick, at the top reactor ring, 3 of 8 points per kick)
   Hot sparks: GPU, 150 + 400*Strength particles, velocity in an upward/outward cone 800-2500, gravity
   -980, collision with the scene (depth buffer is fine) bounce 0.35, velocity-aligned stretched sprites or
   ribbons. Colour white-hot fading to ColorA, emissive ~50. Lifetime 1-2.5 s.

5. **NS_FloorShockwave** (burst on every kick, floor centre)
   A thin bright ring on the floor expanding 0 to 2500 cm in 0.5 s and fading, plus ~300 dust puffs
   kicked up along the ring. Colour ColorB * 8.

6. **NS_HatGlitter** (burst on hats when Intensity > 0.6, under the truss)
   20-40 tiny sharp flashes that fall and twinkle out within 0.3 s. Emissive ColorB.

7. **NS_CO2Jet** (burst on bar starts when Intensity > 0.8 and on every drop, 4 floor corners at ±1500)
   A violent vertical CO2 cannon: white smoke column shooting up 2500-4000 cm/s, widening, 1.2 s spawn,
   dissipating over 3 s. Lit translucent, very bright where beams hit it.

8. **NS_DropBlast** (burst on the drop, at the core)
   The big moment: 15-25k GPU sparks in a radial explosion (speed 1500-6000, drag, gravity, floor collision),
   an expanding shock sphere, a smoke ring rolling outward, and a short intense light flash. Colours from
   ColorA/ColorB, white core.

Keep each system GPU-sim where particle counts are high, set fixed bounds, and enable scalability culling off
(the camera is always inside the show).
