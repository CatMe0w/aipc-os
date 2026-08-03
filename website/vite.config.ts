import { defineConfig } from "vite";
import { ViteMinifyPlugin } from "vite-plugin-minify";
import { resolve } from "path";
import { prerender } from "./plugins/prerender.js";

export default defineConfig({
  root: ".",
  publicDir: "public",
  build: {
    outDir: "dist",
    rollupOptions: {
      input: {
        index: resolve(import.meta.dirname, "index.html"),
        en: resolve(import.meta.dirname, "en.html"),
      },
    },
    modulePreload: false,
  },
  plugins: [prerender(), ViteMinifyPlugin({})],
});
