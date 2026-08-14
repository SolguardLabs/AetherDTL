import { readdirSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const minimumNonEmptyLines = 4_500;
let physical = 0;
let nonEmpty = 0;
let files = 0;
for (const file of readdirSync(join(root, "src"))) {
  if (!file.endsWith(".cpp") && !file.endsWith(".hpp")) continue;
  const text = readFileSync(join(root, "src", file), "utf8");
  const lines = text.split(/\r?\n/);
  files += 1;
  physical += lines.length;
  nonEmpty += lines.filter((line) => line.trim().length > 0).length;
}
console.log(`src_files=${files} physical_loc=${physical} non_empty_loc=${nonEmpty}`);
if (nonEmpty < minimumNonEmptyLines) {
  throw new Error(`source must contain at least ${minimumNonEmptyLines} non-empty lines`);
}
