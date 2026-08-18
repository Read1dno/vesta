# Vesta website

Static GitHub Pages site with no analytics, cookies, backend, or third-party
JavaScript. It contains the product landing page, a captured Web Radar preview,
profile downloads, and the HTML build of the Lua API documentation.

## Local preview

```powershell
cd website
npm run build
npm run serve
```

Open `http://127.0.0.1:4173/`. Do not open `index.html` through a `file://` URL:
Firefox blocks the radar preview's JSON requests for local files.

The build copies:

- the feature catalogue from `website/feature-tree-data.js`;
- profiles that exist in `configs/`;
- `lua/scripts/Vesta Web Radar.lua`;
- Markdown documentation from `lua/docs`.

Missing profiles are rendered as unavailable instead of producing broken links.
Edit `site-config.js` only when explicit release/source URLs are required; on a
GitHub project page they are derived from the current URL.
