# Web Radar example

The user-facing distribution is one portable file: `scripts/Vesta Web Radar.lua`.
The maintainable sources are in `scripts/vesta_web_radar`; the generated Lua
embeds and extracts the helper and frontend automatically when enabled.

1. A below-normal Lua worker polls `vesta.game.radar_snapshot()` at 20 Hz and
   writes compact double-buffered JSON state in `vesta.api.data_dir`. It does
   not subscribe to the complete 60 Hz frame with ESP bones and hitboxes.
2. An external PowerShell helper binds a random loopback port and serves the
   static frontend plus the latest state.
3. On first use the helper downloads a pinned portable Windows x64 `cloudflared`
   release, verifies its SHA-256, and caches it in the script's private data
   directory.
4. The helper opens an account-free Cloudflare Quick Tunnel, publishes a
   token-protected path, and reconnects `cloudflared` if the tunnel exits.

The map appears immediately instead of being reconstructed by thousands of
collision queries. `vesta.game.radar_overview` decodes the official overview
from the user's local CS2 VPK into the script's cache.

The token is a 192-bit random bearer secret. It prevents discovery by guessing,
but anyone with the complete link can view the radar until the script stops.
The helper sends `no-store` and `no-referrer` headers and exposes no public route
without the token.
