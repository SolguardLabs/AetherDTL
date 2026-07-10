import { readdirSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
let lines = 0;
for (const file of readdirSync(join(root, "src"))) {
  if (!file.endsWith(".cpp") && !file.endsWith(".hpp")) continue;
  const text = readFileSync(join(root, "src", file), "utf8");
  lines += text.split(/\r?\n/).filter((line) => line.trim().length > 0).length;
}
console.log(lines);
