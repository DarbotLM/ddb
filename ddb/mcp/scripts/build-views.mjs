import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(scriptDir, "..");
const viteBin = path.join(projectDir, "node_modules", "vite", "bin", "vite.js");

const inputs = [
  "resources/card-viewer.html",
  "resources/graph-explorer.html",
  "resources/memory-dashboard.html",
  "resources/3dkg-viewer.html",
];

for (const input of inputs) {
  console.log(`Building ${input}`);
  const result = spawnSync(process.execPath, [viteBin, "build"], {
    stdio: "inherit",
    cwd: projectDir,
    env: { ...process.env, INPUT: input },
  });
  if (result.error) console.error(result.error.message);
  if ((result.status ?? 1) !== 0) {
    process.exit(result.status ?? 1);
  }
}