import react from "@vitejs/plugin-react";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const webRoot = fileURLToPath(new URL(".", import.meta.url));

export default defineConfig({
  plugins: [react()],
  define: {
    "process.env.NODE_ENV": JSON.stringify("production"),
  },
  build: {
    outDir: fileURLToPath(new URL("../resources/web/assets", import.meta.url)),
    emptyOutDir: true,
    sourcemap: false,
    lib: {
      entry: `${webRoot}src/main.tsx`,
      formats: ["es"],
      fileName: () => "graphx-dashboard.js",
    },
    rollupOptions: {
      output: {
        assetFileNames: (asset) =>
          asset.names.some((name) => name.endsWith(".css"))
            ? "graphx-dashboard.css"
            : "[name][extname]",
      },
    },
  },
});
