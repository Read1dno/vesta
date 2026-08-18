import { cp, mkdir, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { dirname, extname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..");
const output = join(here, "dist");
const docsSource = join(root, "lua", "docs");
const docsOutput = join(output, "docs", "api_docs");
const required = ["index.html", "styles.css", "app.js", "site-config.js", "robots.txt", ".nojekyll"];
const escapeHtml = value => String(value)
  .replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;")
  .replaceAll('"', "&quot;").replaceAll("'", "&#39;");
const slugify = value => value.toLowerCase().trim()
  .replace(/[^\p{L}\p{N}]+/gu, "-").replace(/^-|-$/g, "") || "section";

function rewriteHref(href) {
  const [path, hash = ""] = href.split("#", 2);
  if (!path.toLowerCase().endsWith(".md")) return href;
  const rewritten = /(^|\/)readme\.md$/i.test(path)
    ? path.replace(/readme\.md$/i, "")
    : path.replace(/\.md$/i, ".html");
  return `${rewritten}${hash ? `#${hash}` : ""}`;
}

function renderInline(source) {
  const code = [];
  let value = escapeHtml(source).replace(/`([^`]+)`/g, (_, text) => {
    const key = `@@CODE${code.length}@@`;
    code.push(`<code>${text}</code>`);
    return key;
  });
  value = value
    .replace(/\[([^\]]+)\]\(([^)\s]+)(?:\s+&quot;.*?&quot;)?\)/g, (_, label, href) => `<a href="${escapeHtml(rewriteHref(href))}">${label}</a>`)
    .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
    .replace(/__([^_]+)__/g, "<strong>$1</strong>")
    .replace(/\*([^*]+)\*/g, "<em>$1</em>");
  code.forEach((fragment, index) => { value = value.replace(`@@CODE${index}@@`, fragment); });
  return value;
}

function renderMarkdown(markdown) {
  const lines = markdown.replaceAll("\r\n", "\n").split("\n");
  const html = [];
  let paragraph = [];
  let list = null;
  let quote = [];
  const flushParagraph = () => {
    if (!paragraph.length) return;
    html.push(`<p>${renderInline(paragraph.join(" "))}</p>`);
    paragraph = [];
  };
  const flushList = () => {
    if (!list) return;
    html.push(`<${list.type}>${list.items.map(item => `<li>${renderInline(item)}</li>`).join("")}</${list.type}>`);
    list = null;
  };
  const flushQuote = () => {
    if (!quote.length) return;
    html.push(`<blockquote><p>${renderInline(quote.join(" "))}</p></blockquote>`);
    quote = [];
  };
  const flushAll = () => { flushParagraph(); flushList(); flushQuote(); };

  for (let index = 0; index < lines.length; index++) {
    const line = lines[index];
    if (line.startsWith("```")) {
      flushAll();
      const language = line.slice(3).trim();
      const body = [];
      while (++index < lines.length && !lines[index].startsWith("```")) body.push(lines[index]);
      html.push(`<div class="code-block"><button class="copy-code" type="button">Copy</button><pre><code${language ? ` class="language-${escapeHtml(language)}"` : ""}>${escapeHtml(body.join("\n"))}</code></pre></div>`);
      continue;
    }
    const heading = /^(#{1,6})\s+(.+)$/.exec(line);
    if (heading) {
      flushAll();
      const level = heading[1].length;
      const title = heading[2].replace(/\s+#+$/, "");
      html.push(`<h${level} id="${slugify(title)}">${renderInline(title)}</h${level}>`);
      continue;
    }
    if (/^\s*([-*_])(?:\s*\1){2,}\s*$/.test(line)) { flushAll(); html.push("<hr>"); continue; }
    if (line.startsWith(">")) { flushParagraph(); flushList(); quote.push(line.replace(/^>\s?/, "")); continue; }
    if (quote.length) flushQuote();
    const unordered = /^\s*[-*+]\s+(.+)$/.exec(line);
    const ordered = /^\s*\d+[.)]\s+(.+)$/.exec(line);
    if (unordered || ordered) {
      flushParagraph();
      const type = ordered ? "ol" : "ul";
      if (list && list.type !== type) flushList();
      list ||= { type, items: [] };
      list.items.push((ordered || unordered)[1]);
      continue;
    }
    if (list) flushList();
    if (line.includes("|") && index + 1 < lines.length && /^\s*\|?\s*:?-+/.test(lines[index + 1])) {
      flushParagraph();
      const cells = row => row.trim().replace(/^\||\|$/g, "").split("|").map(cell => cell.trim());
      const headers = cells(line);
      index++;
      const rows = [];
      while (index + 1 < lines.length && lines[index + 1].includes("|") && lines[index + 1].trim()) rows.push(cells(lines[++index]));
      html.push(`<table><thead><tr>${headers.map(cell => `<th>${renderInline(cell)}</th>`).join("")}</tr></thead><tbody>${rows.map(row => `<tr>${row.map(cell => `<td>${renderInline(cell)}</td>`).join("")}</tr>`).join("")}</tbody></table>`);
      continue;
    }
    if (!line.trim()) { flushAll(); continue; }
    paragraph.push(line.trim());
  }
  flushAll();
  return html.join("\n");
}

async function walk(directory) {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) files.push(...await walk(path));
    else files.push(path);
  }
  return files;
}

const pageTitles = {
  en: { "README.md":"Overview", "api-reference.md":"API reference", "config.md":"Configuration", "events.md":"Events", "game-snapshot.md":"Game snapshot", "input.md":"Input", "runtime-and-lifecycle.md":"Runtime & lifecycle", "storage-and-helpers.md":"Storage & modules", "testing.md":"Runtime testing", "ui-and-drawing.md":"UI & drawing", "examples/web-radar.md":"Web Radar example" },
  ru: { "README.md":"Обзор", "api-reference.md":"Справочник API", "config.md":"Конфигурация", "events.md":"События", "game-snapshot.md":"Game snapshot", "input.md":"Input", "runtime-and-lifecycle.md":"Runtime и lifecycle", "storage-and-helpers.md":"Storage и modules", "testing.md":"Проверка runtime", "ui-and-drawing.md":"UI и отрисовка", "examples/web-radar.md":"Пример Web Radar" }
};
const pageTarget = relativePath => relativePath === "README.md" ? "index.html" : relativePath.replace(/\.md$/i, ".html");

function docsNavigation(active, prefix, language) {
  const titles = pageTitles[language];
  const core = Object.entries(titles).filter(([path]) => !path.startsWith("examples/"));
  const examples = Object.entries(titles).filter(([path]) => path.startsWith("examples/"));
  const group = (title, pages) => `<div class="nav-group"><p class="docs-nav-title">${title}</p>${pages.map(([path, label]) => {
    const href = prefix + pageTarget(path);
    return `<a${path === active ? ' class="active" aria-current="page"' : ""} href="${href}">${label}</a>`;
  }).join("")}</div>`;
  return group(language === "en" ? "Documentation" : "Документация", core) + group(language === "en" ? "Examples" : "Примеры", examples);
}

async function buildDocsLanguage(sourceRoot, language) {
  const files = (await walk(sourceRoot)).filter(source => language !== "ru" || !relative(sourceRoot, source).replaceAll("\\", "/").startsWith("en/"));
  const languageOutput = join(docsOutput, language);
  for (const source of files) {
    const relativePath = relative(sourceRoot, source).replaceAll("\\", "/");
    if (extname(source).toLowerCase() !== ".md") {
      const target = join(languageOutput, relativePath);
      await mkdir(dirname(target), { recursive: true });
      await cp(source, target);
      continue;
    }
    const markdown = await readFile(source, "utf8");
    const title = /^#\s+(.+)$/m.exec(markdown)?.[1] || pageTitles[language][relativePath] || "Lua API";
    const targetRelative = pageTarget(relativePath);
    const depth = targetRelative.split("/").length - 1;
    const prefix = depth ? "../".repeat(depth) : "";
    const alternateLanguage = language === "en" ? "ru" : "en";
    const backLabel = language === "en" ? "Back to product" : "Вернуться к продукту";
    const generatedLabel = language === "en" ? "generated from the repository documentation" : "собрано из документации репозитория";
    const document = `<!doctype html>
<html lang="${language}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#09090c"><meta name="description" content="Vesta Lua API documentation"><title>${escapeHtml(title)} — Vesta Lua API</title><link rel="stylesheet" href="${prefix}../docs.css"><script src="${prefix}../docs.js" defer></script></head>
<body><header class="docs-header"><a class="docs-brand" href="${prefix}../../../"><strong>VESTA</strong><span>Lua API documentation</span></a><div class="docs-header-actions"><a class="docs-back" href="${prefix}../../../"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m15 18-6-6 6-6"/></svg>${backLabel}</a><a class="docs-language" data-doc-language="${alternateLanguage}" href="${prefix}../${alternateLanguage}/${targetRelative}">${alternateLanguage.toUpperCase()}</a></div></header><div class="docs-shell"><nav class="docs-nav" aria-label="Documentation">${docsNavigation(relativePath, prefix, language)}</nav><main class="docs-main">${renderMarkdown(markdown)}<footer class="doc-footer">Vesta Lua API 1.0 · ${generatedLabel}</footer></main></div></body></html>`;
    const target = join(languageOutput, targetRelative);
    await mkdir(dirname(target), { recursive: true });
    await writeFile(target, document, "utf8");
  }
}

async function buildDocs() {
  await mkdir(docsOutput, { recursive: true });
  await cp(join(here, "docs.css"), join(docsOutput, "docs.css"));
  await cp(join(here, "docs.js"), join(docsOutput, "docs.js"));
  await buildDocsLanguage(docsSource, "ru");
  await buildDocsLanguage(join(docsSource, "en"), "en");
  for (const source of (await walk(join(docsSource, "examples"))).filter(file => extname(file).toLowerCase() !== ".md")) {
    const relativeAsset = relative(docsSource, source);
    for (const language of ["en", "ru"]) {
      const target = join(docsOutput, language, relativeAsset);
      await mkdir(dirname(target), { recursive: true });
      await cp(source, target);
    }
  }
  await writeFile(join(docsOutput, "index.html"), `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Vesta Lua API</title><script>const saved=localStorage.getItem('vesta-language');const lang=saved||((navigator.language||'en').toLowerCase().startsWith('ru')?'ru':'en');location.replace(lang+'/');</script></head><body><a href="en/">Open Vesta Lua API documentation</a></body></html>`, "utf8");
}

await rm(output, { recursive: true, force: true });
await mkdir(output, { recursive: true });
for (const file of required) await cp(join(here, file), join(output, file));
const featureTreeSource = await readFile(join(here, "feature-tree-data.js"), "utf8");
await writeFile(join(output, "feature-tree.js"), featureTreeSource, "utf8");
await cp(join(here, "assets"), join(output, "assets"), { recursive: true });
await cp(join(here, "radar"), join(output, "radar"), { recursive: true });
const configNames = ["full-legit.cfg", "legit.cfg", "semi-rage.cfg"];
const configAvailability = {};
await mkdir(join(output, "configs"), { recursive: true });
for (const name of configNames) {
  try {
    await cp(join(root, "configs", name), join(output, "configs", name));
    configAvailability[name] = true;
  } catch (error) {
    if (error?.code !== "ENOENT") throw error;
    configAvailability[name] = false;
  }
}
await writeFile(join(output, "config-status.js"),
  `window.VESTA_CONFIGS = ${JSON.stringify(configAvailability)};\n`, "utf8");
await mkdir(join(output, "downloads"), { recursive: true });
await cp(join(root, "lua", "scripts", "Vesta Web Radar.lua"), join(output, "downloads", "Vesta-Web-Radar.lua"));
await buildDocs();

const html = await readFile(join(output, "index.html"), "utf8");
for (const asset of ["styles.css", "app.js", "site-config.js", "feature-tree.js", "config-status.js", "assets/vesta-interface.webp", "radar/index.html", "docs/api_docs/index.html"]) {
  if (!html.includes(asset) && !asset.startsWith("docs/")) throw new Error(`Landing page does not reference ${asset}`);
  const info = await stat(join(output, ...asset.split("/")));
  if (info.size === 0) throw new Error(`${asset} is empty`);
}
console.log(`Vesta landing built at ${output}`);
