import { content, drivers, flavors, roadmap, statusLabel } from "./data.js";
import type { Lang, RoadmapStatus } from "./data.js";

function escAttr(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/"/g, "&quot;");
}

export function renderHead(lang: Lang): string {
  const h = content[lang].head;
  return `<title>${h.title}</title>
    <meta name="description" content="${escAttr(h.description)}" />`;
}

export function renderNav(lang: Lang): string {
  const nav = content[lang].nav;
  const links = nav.links.map((l) => `<li><a href="${l.href}">${l.label}</a></li>`).join("");
  return `<nav>
      <a class="nav-site" href="https://catme0w.org">${nav.site}</a>
      <ul class="nav-links">${links}</ul>
    </nav>`;
}

export function renderHero(lang: Lang): string {
  const hero = content[lang].hero;
  const sub = hero.sub.map((s) => (s.hl ? `<span class="hl">${s.t}</span>` : s.t)).join("");
  return `<section class="hero">
      <div class="hero-content">
        <div class="hero-logo">${hero.logo}</div>
        <h1 class="hero-tagline">${hero.tagline}</h1>
        <p class="hero-sub">${sub}</p>
        <a class="hero-cta" href="${hero.ctaHref}">${hero.cta}</a>
        <a class="hero-lang" href="${hero.langHref}">${hero.langLabel}</a>
      </div>
    </section>`;
}

export function renderSummary(lang: Lang): string {
  const s = content[lang].summary;
  const paras = s.paragraphs.map((p) => `<p>${p}</p>`).join("\n        ");
  return `<div class="section" id="summary">
      <div class="section-eyebrow">${s.eyebrow}</div>
      <h2 class="section-title">${s.title}</h2>
      <div class="prose">
        ${paras}
      </div>
    </div>`;
}

function renderTag(status: RoadmapStatus): string {
  if (status === "wip") {
    const cells = '<span class="sc"></span>'.repeat(6);
    return `<span class="rm-tag rm-tag-wip">[<span class="sp-inner" id="spinner">${cells}</span>]</span>`;
  }
  return status === "ok" ? `<span class="rm-tag rm-tag-ok">[&nbsp;&nbsp;OK&nbsp;&nbsp;]</span>` : `<span class="rm-tag rm-tag-wait">[ WAIT ]</span>`;
}

export function renderRoadmap(lang: Lang): string {
  const s = content[lang].roadmap;
  const items = roadmap
    .map((item, i) => {
      const t = item[lang];
      const isLast = i === roadmap.length - 1;
      return `<div class="rm-item">
          <div class="rm-gutter">
            <div class="rm-node ${item.status}"></div>
            ${isLast ? "" : '<div class="rm-trail"></div>'}
          </div>
          <div class="rm-body">
            <div class="rm-status-line">
              ${renderTag(item.status)}
              <span class="rm-label">${t.label}</span>
            </div>
            <div class="rm-desc">${t.desc}</div>
          </div>
        </div>`;
    })
    .join("");
  return `<div class="section" id="roadmap">
      <div class="section-eyebrow">${s.eyebrow}</div>
      <h2 class="section-title">${s.title}</h2>

      <div class="roadmap">
        ${items}
      </div>
    </div>`;
}

export function renderHardware(lang: Lang): string {
  const s = content[lang].hardware;
  const cells = drivers
    .map((d) => {
      const st = statusLabel[d.status];
      return `<div class="driver-cell">
          <div class="driver-name">${d[lang]}</div>
          <div class="driver-status ${st.cls}"><span>${st.dot}</span>${st[lang]}</div>
        </div>`;
    })
    .join("");
  return `<div class="section" id="hardware">
      <div class="section-eyebrow">${s.eyebrow}</div>
      <h2 class="section-title">${s.title}</h2>

      <div class="driver-grid">
        ${cells}
      </div>
    </div>`;
}

export function renderOmt(lang: Lang): string {
  const s = content[lang].omt;
  const cards = flavors
    .map((f) => {
      const t = f[lang];
      const paras = t.body.map((p) => `<p>${p}</p>`).join("\n            ");
      return `<div class="flavor-card flavor-${f.id}">
          <div class="flavor-title"><span style="color: var(--${f.accent})">${t.lead}</span>: ${t.title}</div>
          <div class="flavor-body">
            ${paras}
          </div>
        </div>`;
    })
    .join("");
  return `<div class="section" id="omt">
      <div class="section-eyebrow">${s.eyebrow}</div>
      <h2 class="section-title">${s.title}</h2>
      <p class="omt-lead">${s.lead}</p>

      <div class="omt-flavors">
        ${cards}
      </div>
    </div>`;
}

export function renderFooter(lang: Lang): string {
  const f = content[lang].footer;
  return `<footer>
      <p class="footer-text">${f.prefix} <a href="${f.href}" class="footer-link">${f.label}</a></p>
    </footer>`;
}
