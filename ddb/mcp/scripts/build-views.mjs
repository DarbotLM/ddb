import { spawnSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(scriptDir, "..");
const viteBin = path.join(projectDir, "node_modules", ".bin", process.platform === "win32" ? "vite.cmd" : "vite");

const inputs = [
  "resources/card-viewer.html",
  "resources/graph-explorer.html",
  "resources/memory-dashboard.html",
  "resources/3dkg-viewer.html",
];

for (const input of inputs) {
  console.log(`Building ${input}`);
  const result = spawnSync(viteBin, ["build"], {
    stdio: "inherit",
    cwd: projectDir,
    env: { ...process.env, INPUT: input },
  });
  if ((result.status ?? 1) !== 0) {
    process.exit(result.status ?? 1);
  }
}