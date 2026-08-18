<div align="center">

<img src=".github/assets/vesta-header.gif" alt="VESTA" width="800">

**A feature-complete external cheat for Counter-Strike 2.**

[Website](https://read1dno.github.io/vesta/) · [Download](https://github.com/Read1dno/vesta/releases/latest) · [Lua API](lua/docs/en/README.md) · [Web Radar](lua/scripts/Vesta%20Web%20Radar.lua)

[UnknownCheats](https://www.unknowncheats.me/forum/counter-strike-2-a/764247-vesta-external.html) · [Telegram](https://t.me/readidno) · [Report a bug](https://github.com/Read1dno/vesta/issues/new/choose) · [Contribute](.github/CONTRIBUTING.md)

</div>

Vesta is a Windows x64 application written in C++23. It runs outside the game,
reads game state with Windows process APIs, renders through a DirectX 11 overlay,
and sends input through the external Windows input gateway. The native executable
does not inject code into CS2, install a driver, or contain a network client.
Optional Lua scripts can start their own external helpers for integrations such
as Web Radar.

| Native application | Visual pipeline | Configuration | Extensions |
|---|---|---|---|
| External and read-only game access | DX11 overlay, synchronized ESP and per-pixel chams | Weapon profiles, ESP Editor and downloadable presets | Sandboxed Lua 5.4 API and portable scripts |

## Before you launch

> [!IMPORTANT]
> Start Vesta while CS2 is running in **Windowed** or **Fullscreen Windowed**
> mode. Starting it while the game is already in exclusive fullscreen can make
> CS2 hang while Windows switches presentation overlays. After Vesta has
> attached successfully, you can switch to the fullscreen overlay mode.

> [!TIP]
> If ESP trails behind player models, flickers under load, or Vesta reports low
> rendering FPS, use the lowest practical in-game graphics settings and run the
> overlay in fullscreen mode. This leaves more CPU/GPU time for synchronized ESP
> and chams without disabling visual features.

## Quick start

1. Download `vesta.exe` from the [latest GitHub Release](https://github.com/Read1dno/vesta/releases/latest).
2. Start Counter-Strike 2 in a windowed mode, then run Vesta and approve the
   elevation prompt.
3. Press `Insert` to open or close the menu. Press `End` to exit safely.
4. Open **Misc → Configs** to create, save, load, import, or export a profile.
5. Configure features for the current weapon group, or enable its global-profile
   switch to inherit the shared combat settings.

### Verify the official build

Release binaries are compiled by GitHub Actions and receive a signed build
provenance attestation that identifies this repository, the release workflow,
the source commit, and the tag. Published releases are immutable: GitHub locks
their tag and assets against replacement or deletion. After downloading
`vesta.exe`, verify it with
[GitHub CLI](https://cli.github.com/):

```powershell
gh attestation verify .\vesta.exe --repo Read1dno/vesta
```

A successful result proves that the binary is the artifact produced by the
repository's GitHub Actions workflow, rather than a manually substituted file.
The accompanying `SHA256SUMS.txt` can additionally detect download corruption.

Runtime files are stored in `%TEMP%\vesta`. Vesta creates a named configuration
only when requested from the configuration interface. Current CS2 offsets are
resolved at runtime; after a game update, rebuild before treating unexpected
behavior as a configuration problem.

## Features

The sections below describe the complete user-facing feature set. Open only the
part you need.

<details>
<summary><strong>Aimbot</strong></summary>

- Hold or Always activation, with a bind exposed for immediate access.
- Airborne, flash, smoke, immunity, visibility, and penetration checks.
- Head, torso, arms, and legs as independent target groups.
- Fixed screen FOV, distance-scaled screen FOV, and a target-attached distance
  FOV; visualization color and detailed near/far limits are configurable.
- Center and scaled cap/side multipoints for head, body, and limbs.
- Simulation-tick prediction with sample-age compensation and bounded horizon.
- Humanizer with time-based curve, reaction range, wind, gravity, damping,
  overshoot, jitter, deadzone, and a live path preview.
- Smoothing, minimum penetration damage, visible-only policy, and Lethal Only;
  the head remains an allowed priority when no body point is lethal.
- Independent RCS with start bullet, pitch/yaw strength, response, randomness,
  and drift; when Aimbot is active both produce one combined aim solution.
- Per-weapon-group overrides for pistols, SMGs, rifles, shotguns, snipers, and
  heavy weapons.
- Grenade aim assistance for configured lineup points.

</details>

<details>
<summary><strong>Triggerbot</strong></summary>

- Hold or Always activation and direct bind control.
- Conventional and deterministic seed-based firing modes.
- Selectable hitbox groups, pre-shot delay, post-shot lockout, timing
  randomization, rare outliers, reaction time, and prediction.
- Hitchance and minimum damage for conventional mode; seed modes decide from the
  deterministic shot result instead of applying a second probability gate.
- Visible-only and penetration policies, smoke/flash/airborne checks, immunity
  rejection, Lethal Only, and head fallback for low-damage weapons.
- Shared predictive Auto Stop that prepares accuracy before the requested shot
  and releases synthetic movement immediately after resolution.
- R8 firing is handled by the trigger path without a separate user-facing mode.
- Per-weapon-group overrides matching Aimbot profiles.

</details>

<details>
<summary><strong>Player ESP and editor</strong></summary>

- Full, cornered, or bordered boxes with separate visible and occluded colors.
- Name, health, armor, skeleton, head marker, view line, weapon icon, ammo mask,
  distance, money, ping, and compact information flags.
- Off-screen arrows with distance placement, smooth antialiasing, color, size,
  and bloom controls.
- Threat hitboxes, sound ESP, spectator-aware synchronization, and immunity
  presentation.
- Legit Sync sources for direct visibility, radar spotting, and sound; smoke is
  included in direct visibility.
- Spectator Sync can suppress the entire player presentation while somebody is
  observing the local player.
- The ESP editor supports drag-and-drop placement, bar orientation anchors, and
  context popups for every visual component.

</details>

<details>
<summary><strong>Chams and model effects</strong></summary>

- Separate visible and occluded materials with configurable colors, alpha,
  wireframe, and material behavior.
- Per-pixel world-depth classification, multisampled color/depth targets, and a
  custom MSAA resolve preserve the visible/invisible boundary.
- Distance-stable model bloom, on-hit pose ghosts, and model-shaped death
  particles.
- Pose data is shared with 2D ESP so boxes, skeletons, and chams use the same
  player frame.
- Dynamic doors and destructible collision participate in visibility and
  penetration instead of changing the chams render pipeline.

</details>

<details>
<summary><strong>World, projectiles, bomb, and effects</strong></summary>

- Weapon and item ESP with icons and distances.
- Projectile icons, timers, bounce/end markers, grenade radii, inferno bounds,
  and configurable trajectory line/bloom styling.
- Planted-bomb world marker and compact UI panel with explosion, defuse, site,
  and local damage information.
- Bomb Safe Zone derived from baked blast damage and map collision geometry.
- No Flash visualization, sound ESP, bullet tracers, world hit markers, damage
  popups, hit sounds, on-hit chams, and kill effects.
- Crosshair reconstruction from game settings, including color, alpha, style,
  gap, size, thickness, outline, T-style, and penetration indication.

</details>

<details>
<summary><strong>Radar</strong></summary>

- In-game map overview with local, teammate, enemy, bomb, dropped-item, and
  projectile positions.
- Team colors, names, health, armor, weapons, money, defuse kits, and grenade
  inventory can be configured independently.
- Player view direction, planted sites, inferno bounds, projectile lifetime, and
  predicted grenade paths are drawn in radar coordinates.
- Always or Hold activation with a configurable bind.
- The Lua Web Radar publishes the same snapshot through an optional external
  helper and provides a private browser link; the script is not required by the
  native application.
- Cloudflare Tunnel is not directly reachable from Russia. Web Radar therefore
  requires an active VPN there; without it the public link cannot be created.

</details>

<details>
<summary><strong>Grenades and movement</strong></summary>

- Grenade trajectory prediction with collision bounces, endpoint, thickness,
  color, and bloom controls.
- Nade Helper shows known lineup positions, aligns the view, walks to the stand
  point when required, performs timed jumps, and releases the configured throw.
- Bunny Hop and Edge Jump use the shared physical/synthetic input state.
- Auto Stop performs camera-relative counter-strafing while preserving physical
  movement ownership after the shot.

</details>

<details>
<summary><strong>Interface and automation</strong></summary>

- Movable watermark, spectator list, keybind list, event log, and bomb panel.
- English/Russian localization, discrete DPI scaling, portable window layout,
  and frame-rate limiting.
- Auto Accept detects the Panorama confirmation button and performs one external
  click per appearance.
- Windowed, borderless fullscreen, and UIAccess fullscreen overlay lifecycle,
  with click-through behavior outside the open menu.
- Popup-based detailed settings keep common switches accessible without exposing
  every dependent parameter at once.

</details>

<details>
<summary><strong>Lua API</strong></summary>

- Sandboxed Lua 5.4 runtime with lifecycle events, hot reload, error isolation,
  budgets, storage, UI controls, drawing, and immutable game snapshots.
- Ready-to-load scripts live in [`lua/scripts`](lua/scripts); reusable development
  packages live in [`lua/packages`](lua/packages).
- The complete English and Russian reference lives in [`lua/docs`](lua/docs).
- [`Vesta Web Radar.lua`](lua/scripts/Vesta%20Web%20Radar.lua) is distributed as
  one portable Lua file.

</details>

## Configuration profiles

The repository includes three ready-to-use profiles. Download one directly or
open it through the configuration manager:

| File | Purpose |
|---|---|
| [`full-legit.cfg`](configs/full-legit.cfg) | Legit Sync ESP and basic functions that expose no additional game information. |
| [`legit.cfg`](configs/legit.cfg) | Wall information and a basic trigger configuration for playing against WH. |
| [`semi-rage.cfg`](configs/semi-rage.cfg) | Seed Trigger and aggressive settings tuned for maximum practical effectiveness, including jump shots. |

The [project website](https://read1dno.github.io/vesta/) publishes the same files
as direct downloads.

## Build from source

Requirements:

- Windows 10/11 x64;
- Visual Studio 2022 with **Desktop development with C++**;
- CMake 3.20 or newer and Git;
- network access during the first configure so CMake can fetch pinned upstream
  dependencies.

```powershell
cmake --preset release
cmake --build --preset release --parallel
ctest --test-dir build/release -C Release --output-on-failure
```

The executable is written to `build/release/bin/vesta.exe`. Dependencies such as
Dear ImGui, Lua, FreeType, JSON, LZ4, Zstandard, meshoptimizer, and xorstr are
downloaded by CMake and are not vendored into this repository.

<details>
<summary><strong>What is in tests/?</strong></summary>

`tests/penetration_segments.cpp` is a deterministic regression test for the
collision-span solver used by penetration. CTest builds it as a separate tiny
executable. It is not linked into `vesta.exe`, is not shipped in Releases, and
does not run while Vesta is active. Keeping it prevents known wall-geometry
failures from returning during public contributions.

</details>

## Repository layout

```text
src/                    application source and embedded runtime assets
lua/scripts/            single-file scripts ready to load in Vesta
lua/packages/           source packages used to build portable scripts
lua/docs/               English and Russian Lua API documentation
configs/                reviewed downloadable profiles
tests/                  deterministic CTest regression coverage
website/                GitHub Pages source and static-site builder
cmake/                   dependency patch/apply helpers
.github/workflows/       build, release, and Pages automation
```

## GitHub automation

- **Build** configures Release, compiles `vesta.exe`, runs CTest, and uploads the
  executable as a workflow artifact.
- **Release** repeats the verified build for version tags, signs its provenance
  through GitHub Artifact Attestations, validates every asset in a draft, and
  only then publishes an immutable GitHub Release.
- **Pages** builds the landing page, Lua documentation, Web Radar download, and
  available profiles, then deploys the static artifact.

## Troubleshooting

<details>
<summary><strong>The overlay is not visible</strong></summary>

Close Vesta, switch CS2 to Windowed or Fullscreen Windowed, and start the current
Release again. Approve the elevation prompt, press `Insert` once, and check that
another overlay or capture tool is not hiding the window. Switch presentation
mode only after the overlay has attached.

</details>

<details>
<summary><strong>ESP is delayed, flickers, or has low FPS</strong></summary>

Use the lowest practical CS2 graphics settings and select Vesta's fullscreen
overlay mode after a successful windowed startup. Include your CPU, GPU, display
mode, refresh rate, game FPS and Vesta FPS when reporting a persistent problem.

</details>

<details>
<summary><strong>Web Radar does not create a link in Russia</strong></summary>

Enable a VPN before starting Web Radar. Its portable helper creates a temporary
Cloudflare Tunnel, and that connection is not directly available from Russia.
After the VPN is connected, disable and enable the script once to restart the
tunnel cleanly.

</details>

<details>
<summary><strong>Aimbot or Triggerbot does not move/click on one PC</strong></summary>

Use Windows 10/11 x64, keep fire bound to the default `+attack` on mouse button
one, and temporarily disable kernel-level anti-cheats or security products that
filter synthetic input. Verify the feature activation mode and bind. Include the
Windows build and input-device/software details in a bug report if the problem
remains.

</details>

<details>
<summary><strong>Features stopped working after a CS2 update</strong></summary>

Build the latest source revision. Schema offsets and signatures can change with
the client, and old binaries are not expected to survive every game update.

</details>

<details>
<summary><strong>CMake cannot configure</strong></summary>

Check Visual Studio's C++ workload, CMake/Git availability, and first-run network
access. Remove only the local `build/release` directory, then run the preset
again; dependencies are pinned and fetched during configure.

</details>

## Support and contribution

| I want to… | Use this |
|---|---|
| Report a reproducible problem | [Open a bug report](https://github.com/Read1dno/vesta/issues/new?template=bug-report.yml) |
| Propose a focused feature | [Open a feature request](https://github.com/Read1dno/vesta/issues/new?template=feature-request.yml) |
| Submit code or documentation | Read [Contributing](.github/CONTRIBUTING.md), then open a pull request |
| Report a security problem | Follow the private process in [Security](.github/SECURITY.md) |
| Discuss Vesta with the community | Use the [UnknownCheats release thread](https://www.unknowncheats.me/forum/counter-strike-2-a/764247-vesta-external.html) |
| Contact the author | Telegram: [@readidno](https://t.me/readidno) |

Bug reports should include the Vesta version or commit, Windows build, hardware,
CS2 display mode, the shortest reproduction sequence, and only the relevant logs
or screenshots. Contributors should keep changes focused, preserve the external
read-only architecture, run the Release build and CTest, and explain observable
behavior changes in the pull request.

## License

Vesta's original code is licensed under the [Apache License 2.0](LICENSE).
Third-party components and assets keep their own licenses; see [`NOTICE`](NOTICE),
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and the notices stored beside
embedded assets.

## Disclaimer

This project is provided for educational and research purposes. Its use can
violate the terms of service of software it interacts with. You are responsible
for how you build and use it.
