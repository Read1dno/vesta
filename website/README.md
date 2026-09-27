# Vesta landing page

Static GitHub Pages landing for Vesta. It intentionally has no runtime backend,
analytics, cookies or third-party JavaScript.

The landing includes downloadable, valid Vesta profiles and the production Web
Radar frontend running against a captured read-only snapshot. It also builds
`docs/api_docs` into a responsive HTML documentation site. No preview calls an
external service.

## Configure links

Edit `site-config.js` before publishing:

- `releaseUrl` and `sourceUrl` may stay empty on a GitHub project page; they are
  derived from `OWNER.github.io/REPOSITORY` automatically.

## Build

Run `npm run build` in this directory. The resulting `dist` folder contains the
entire static site. The build always copies the canonical portable Web Radar
script from `scripts/Vesta Web Radar.lua`, generates the three config presets
from the current root `legit.cfg`, and renders the Lua API Markdown files.

Run `npm run serve` and open `http://127.0.0.1:4173/` for local preview. Do not
open `index.html` directly: Firefox correctly blocks the radar demo's JSON
requests from a `file://` origin.

The repository workflow publishes `website/dist` to GitHub Pages after changes
to the landing page or Web Radar script reach `main`.
