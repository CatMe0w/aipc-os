import type { Plugin } from "vite";
import type { Lang } from "../src/data.js";
import {
  renderFooter,
  renderHardware,
  renderHead,
  renderHero,
  renderNav,
  renderOmt,
  renderRoadmap,
  renderSummary,
} from "../src/render.js";

export function prerender(): Plugin {
  return {
    name: "aipc-os-prerender",
    transformIndexHtml(html, ctx) {
      const lang: Lang = ctx.filename.endsWith("en.html") ? "en" : "zh";
      const sections: Record<string, string> = {
        head: renderHead(lang),
        nav: renderNav(lang),
        hero: renderHero(lang),
        summary: renderSummary(lang),
        roadmap: renderRoadmap(lang),
        hardware: renderHardware(lang),
        omt: renderOmt(lang),
        footer: renderFooter(lang),
      };
      return html.replace(/<!-- @([a-z-]+) -->/g, (match, name) => sections[name] ?? match);
    },
  };
}
