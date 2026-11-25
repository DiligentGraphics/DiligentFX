import {defineConfig} from "vite";
export default defineConfig({
    base: "/viewer/",
    build: {outDir: "dist/vite", rollupOptions: {output: {entryFileNames: "app.js"}}},
});
