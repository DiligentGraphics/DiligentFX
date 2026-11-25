import {fileURLToPath} from "node:url";

// Bundle the same consumer as Vite, using TypeScript's emitted JavaScript.
// Runtime assets are copied separately and must remain native dynamic imports.
export default {
    mode: "production",
    target: ["web", "es2022"],
    entry: "./.webpack-src/main.js",
    output: {
        path: fileURLToPath(new URL("./dist/webpack/", import.meta.url)),
        filename: "app.js",
        publicPath: "/viewer/",
        clean: true,
    },
};
