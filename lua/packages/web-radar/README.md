# Vesta Web Radar

An original opt-in example built only on the documented Vesta Lua API. It does
not copy code or assets from `cs2_webradar-main(1)`.

The distributable artifact is the single file `scripts/Vesta Web Radar.lua`. A user
only copies that file into Vesta's Lua scripts directory. On activation it
extracts its pinned helper and frontend into Vesta-managed temporary locations;
no companion download, manifest or manual installation step is required.
The files in this directory are the maintainable development sources. Run
`package-single-file.ps1` after changing them to regenerate the artifact.

Enable **Lua API** and **Vesta Web Radar**. The script card contains only the
runtime toggle; while it is enabled the card expands by one row with **Copy
link**. The button changes to **Copied** briefly after a successful clipboard
operation. No popup or diagnostic controls are exposed in the product menu.

The browser receives a compact 20 Hz snapshot with both teams, economy, armor,
defuse kits, full loadouts, projectiles, effect lifetimes, predicted trajectories,
inferno bounds, bomb state, and camera state. The script polls
`vesta.game.radar_snapshot()` instead of subscribing to the full
60 Hz `frame` event. Bones, hitboxes, screen-space ESP data and spectators are
never materialized as Lua tables for this exporter. JSON generation and file
publication run on the script's below-normal worker, never on the presentation
thread.

The background is not reconstructed with thousands of collision traces: Vesta reads
the official overview from the user's local read-only CS2 VPK and decodes it once
into the script's `%TEMP%` cache. No Valve map image is embedded in Vesta or in
this package.

The responsive broadcast-style page contains the map, T/CT economy rosters, a
small connection indicator, and an SVG settings button. Display choices are
private to the viewer and persist in the browser's local storage.

Security and networking:

- `vesta.exe` links no networking API; only this enabled package launches a
  helper;
- the HTTP listener binds to `127.0.0.1` and every run generates a 192-bit URL
  token;
- on first use the helper downloads the official portable `cloudflared` 2026.8.1
  Windows x64 executable from Cloudflare's GitHub release into the script's
  `%TEMP%` data directory;
- the executable is accepted only when its SHA-256 equals
  `8f1d6f87b8756dbf37064b16e2c8251b69d816305e4f4373e1b80efb28d13b83`;
  a partial or mismatching download is deleted and never executed;
- the helper opens an account-free Cloudflare Quick Tunnel and reconnects it
  automatically if the client exits; no alternate public tunnel is used;
- Quick Tunnel endpoints are temporary and their latency depends on Cloudflare
  and the viewer's route;
- unloading the script requests shutdown of the helper and tunnel;
- possession of the full private URL grants access while the session is alive.

For local development, `helper.ps1 -NoTunnel` disables all public transport.
