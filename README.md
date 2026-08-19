<div align="center">

<img src=".github/assets/vesta-header.gif" alt="VESTA" width="800">

**A feature-complete external cheat for Counter-Strike 2.**

<p>
  <a href="https://github.com/Read1dno/vesta/releases/latest"><img src=".github/assets/icon-download.svg" width="16" height="16" alt=""> <strong>Download</strong></a>
  &nbsp;&nbsp;&nbsp;
  <a href="https://read1dno.github.io/vesta/"><img src=".github/assets/icon-website.svg" width="16" height="16" alt=""> <strong>Website</strong></a>
  &nbsp;&nbsp;&nbsp;
  <a href="lua/docs/en/README.md"><img src=".github/assets/icon-docs.svg" width="16" height="16" alt=""> <strong>Lua API</strong></a>
</p>

</div>

Vesta is a configurable Windows x64 project with combat assistance, a complete
visual system, movement tools, grenade lineups, an in-game radar, and a
sandboxed Lua API. The native client stays external: no injection and no driver.

## Quick start

1. Download [`vesta.exe`](https://github.com/Read1dno/vesta/releases/latest).
2. Start Counter-Strike 2.
3. Run `vesta.exe`.
4. Press `Insert` to open the menu. Press `End` to close Vesta.

That is all you need. Profiles are managed from **Misc → Configs**. On a compact
keyboard, change the menu and exit keys in `%TEMP%\vesta\hotkeys.cfg`, then
restart Vesta.

## Features

Open a section only when you need its settings.

<details>
<summary><strong>Aimbot and RCS</strong></summary>

- Always, hold, or toggle activation with human-readable binds.
- Fixed, distance-scaled, and target-attached FOV modes.
- Configurable hitboxes, multipoints, prediction, smoothing, and humanizer.
- Visibility, smoke, flash, airborne, immunity, penetration, and lethal checks.
- Independent recoil control that also combines with the active aim solution.
- Global settings and overrides for every weapon group.

</details>

<details>
<summary><strong>Triggerbot and Auto Stop</strong></summary>

- Conventional and deterministic seed-based firing.
- Pre-shot delay, post-shot lockout, timing randomization, and rare outliers.
- Hitbox groups, prediction, penetration, minimum damage, and lethal logic.
- The head remains available when a low-damage weapon has no lethal body point.
- Predictive Auto Stop prepares weapon accuracy before a requested shot.
- Shared global and per-weapon profiles.

</details>

<details>
<summary><strong>Player ESP and Chams</strong></summary>

- Boxes, skeleton, head marker, view line, name, health, armor, weapon, ammo,
  distance, money, ping, and information flags.
- Drag-and-drop ESP Editor with bar anchors and contextual settings.
- Off-screen arrows, sound ESP, hitboxes, bloom, on-hit poses, and death effects.
- Separate visible and occluded chams materials with per-pixel visibility.
- Legit Sync for direct visibility, radar, sound, and smoke.
- Spectator Sync can hide the complete player presentation while observed.

</details>

<details>
<summary><strong>World, projectiles, and bomb</strong></summary>

- Dropped weapons and items with icons and distance.
- Projectile timers, bounce points, predicted paths, grenade radii, and inferno
  bounds.
- Planted-bomb world marker and compact timer/defuse panel.
- Bomb Safe Zone calculated from blast damage and map collision.
- Bullet tracers, hit markers, hit sounds, damage popups, and event log.
- Crosshair reconstruction from the active in-game crosshair settings.

</details>

<details>
<summary><strong>Radar</strong></summary>

- Players, view direction, health, armor, weapons, money, kits, grenades, bomb,
  dropped items, projectiles, and inferno bounds.
- Predicted grenade paths and accurate projectile lifetime.
- Independent visual controls and Always, Hold, or Toggle activation.
- Optional browser radar through the portable Lua script.

</details>

<details>
<summary><strong>Grenades and movement</strong></summary>

- Configurable grenade trajectory, bounces, endpoint, thickness, color, and
  bloom.
- Nade Helper lineup database with aim alignment, movement, timed jump, and
  automatic release.
- Bunny Hop, Edge Jump, and predictive Auto Stop share one input controller.

</details>

<details>
<summary><strong>Interface and automation</strong></summary>

- Movable watermark, spectator list, keybind list, event log, and bomb panel.
- English and Russian interface, DPI scaling, custom palette, and portable
  layouts.
- Popup-based advanced settings and configurable lifecycle hotkeys.
- Auto Accept performs a single external confirmation click per match prompt.

</details>

<details>
<summary><strong>Lua API</strong></summary>

- Sandboxed Lua 5.4 runtime with hot reload, isolated errors, resource budgets,
  storage, UI controls, drawing, and immutable game snapshots.
- Read the [API documentation](lua/docs/en/README.md) or browse the
  [script directory](lua/scripts).
- [Vesta Web Radar](lua/scripts/Vesta%20Web%20Radar.lua) is distributed as one
  portable Lua file. Its Cloudflare tunnel requires a VPN in Russia.

</details>

## Profiles

| Profile | Use case |
|---|---|
| [`Full legit`](configs/full-legit.cfg) | Legit Sync ESP and basic assistance without additional hidden information. |
| [`Legit`](configs/legit.cfg) | Wall information and a basic trigger configuration for playing against WH. |
| [`Semi rage`](configs/semi-rage.cfg) | Seed Trigger and aggressive settings, including deterministic jump shots. |

Download a profile and import it from **Misc → Configs**.

## Official builds

Release binaries are compiled by GitHub Actions from the tagged source and
published with signed build provenance. Verify a downloaded executable with
[GitHub CLI](https://cli.github.com/):

```powershell
gh attestation verify .\vesta.exe --repo Read1dno/vesta
```

<details>
<summary><strong>Build from source</strong></summary>

Install Visual Studio 2022 with **Desktop development with C++**, CMake 3.20+
and Git, then run:

```powershell
cmake --preset release
cmake --build --preset release --parallel
ctest --test-dir build/release -C Release --output-on-failure
```

The executable is written to `build/release/bin/vesta.exe`. CMake downloads the
pinned third-party dependencies during the first configure.

</details>

## Help and development

- Found a reproducible problem? [Report a bug](https://github.com/Read1dno/vesta/issues/new?template=bug-report.yml).
- Have a focused idea? [Request a feature](https://github.com/Read1dno/vesta/issues/new?template=feature-request.yml).
- Want to submit a change? Read [Contributing](.github/CONTRIBUTING.md).
- Community discussion lives in the [UnknownCheats thread](https://www.unknowncheats.me/forum/counter-strike-2-a/764247-vesta-external.html).

For persistent ESP delay or low overlay FPS, lower the in-game graphics settings
before disabling visual features. After a CS2 update, use the latest Vesta build
before reporting changed offsets or signatures.

## License

Licensed under [Apache 2.0](LICENSE). Third-party notices are listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Educational and research use only. You are responsible for complying with the
terms of the software you use it with.

<div align="center">

[Website](https://read1dno.github.io/vesta/) · [UnknownCheats](https://www.unknowncheats.me/forum/counter-strike-2-a/764247-vesta-external.html) · [Telegram](https://t.me/readidno)

</div>
