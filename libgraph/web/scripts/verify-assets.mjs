import { createHash } from "node:crypto";
import { readdir, readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const webRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const assetRoot = resolve(webRoot, "../resources/web/assets");

async function inventory() {
  const names = (await readdir(assetRoot)).sort();
  const entries = [];
  for (const name of names) {
    const content = await readFile(resolve(assetRoot, name));
    entries.push(`${name}:${createHash("sha256").update(content).digest("hex")}`);
  }
  return entries;
}

const before = await inventory();
const build = spawnSync("npm", ["run", "build"], {
  cwd: webRoot,
  encoding: "utf8",
  stdio: "inherit",
});
if (build.status !== 0) {
  process.exit(build.status ?? 1);
}
const after = await inventory();
if (JSON.stringify(before) !== JSON.stringify(after)) {
  throw new Error(
    `checked-in dashboard assets are stale:\nexpected ${before.join("\n")}\n` +
      `rebuilt ${after.join("\n")}`,
  );
}
console.log(`verified ${after.length} reproducible dashboard assets`);
