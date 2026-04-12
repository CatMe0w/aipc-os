import { defineConfig } from "vite";
import { ViteMinifyPlugin } from "vite-plugin-minify";
import { resolve } from "path";

export default defineConfig({
  root: ".",
  publicDir: "public",
  build: {
    outDir: "dist",
    rollupOptions: {
      input: {
        index: resolve(__dirname, "index.html"),
        en: resolve(__dirname, "en.html"),
      },
    },
  },
  plugins: [ViteMinifyPlugin({})],
});
