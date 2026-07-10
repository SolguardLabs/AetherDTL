import { existsSync, mkdirSync, readdirSync, rmSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const outDir = join(root, "build");
const exeName = process.platform === "win32" ? "aetherdtl.exe" : "aetherdtl";
const output = join(outDir, exeName);
const args = new Set(process.argv.slice(2));
const warnings = args.has("--warnings");
const clean = args.has("--clean");

function sources() {
  return readdirSync(join(root, "src"))
    .filter((file) => file.endsWith(".cpp"))
    .sort()
    .map((file) => join(root, "src", file));
}

function run(command, argv, options = {}) {
  return spawnSync(command, argv, {
    cwd: root,
    encoding: "utf8",
    stdio: options.stdio ?? "pipe",
    shell: false,
  });
}

function commandExists(command) {
  if (process.platform === "win32") {
    return run("where.exe", [command]).status === 0;
  }
  return spawnSync("sh", ["-c", `command -v "${command.replaceAll('"', '\\"')}"`], {
    cwd: root,
    encoding: "utf8",
    stdio: "pipe",
  }).status === 0;
}

function quote(value) {
  return `"${String(value).replaceAll('"', '\\"')}"`;
}

function findMsvcVcvars() {
  if (process.platform !== "win32") {
    return [];
  }
  const roots = [process.env.ProgramFiles, process.env["ProgramFiles(x86)"]].filter(Boolean);
  const candidates = [
    ...roots.flatMap((root) => [
      join(root, "Microsoft Visual Studio", "2022", "Community", "VC", "Auxiliary", "Build", "vcvars64.bat"),
      join(root, "Microsoft Visual Studio", "2022", "Professional", "VC", "Auxiliary", "Build", "vcvars64.bat"),
      join(root, "Microsoft Visual Studio", "2022", "Enterprise", "VC", "Auxiliary", "Build", "vcvars64.bat"),
      join(root, "Microsoft Visual Studio", "2022", "BuildTools", "VC", "Auxiliary", "Build", "vcvars64.bat"),
    ]),
  ];
  const vswhere = join(
    process.env["ProgramFiles(x86)"] ?? "",
    "Microsoft Visual Studio",
    "Installer",
    "vswhere.exe",
  );
  if (existsSync(vswhere)) {
    const found = run(vswhere, [
      "-latest",
      "-products",
      "*",
      "-requires",
      "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
      "-property",
      "installationPath",
    ]);
    const installPath = found.stdout.trim();
    if (found.status === 0 && installPath) {
      candidates.push(join(installPath, "VC", "Auxiliary", "Build", "vcvars64.bat"));
    }
  }
  return candidates.filter((path, index, all) => existsSync(path) && all.indexOf(path) === index);
}

function buildWithMsvc(command, src) {
  const flags = [
    "/nologo",
    "/std:c++20",
    "/EHsc",
    "/O2",
    "/D_CRT_SECURE_NO_WARNINGS",
    `/I${join(root, "src")}`,
    `/Fo${outDir.replaceAll("\\", "/")}/`,
    `/Fe:${output}`,
    ...src,
  ];
  if (warnings) {
    flags.splice(4, 0, "/W4", "/WX");
  }
  return run(command, flags, { stdio: "inherit" });
}

function buildWithMsvcVcvars(vcvars, src) {
  const flags = [
    "/nologo",
    "/std:c++20",
    "/EHsc",
    "/O2",
    "/D_CRT_SECURE_NO_WARNINGS",
    `/I${join(root, "src")}`,
    `/Fo${outDir.replaceAll("\\", "/")}/`,
    `/Fe:${output}`,
    ...src,
  ];
  if (warnings) {
    flags.splice(4, 0, "/W4", "/WX");
  }
  const line = `${quote(vcvars)} >nul && cl ${flags.map(quote).join(" ")}`;
  return spawnSync(line, {
    cwd: root,
    encoding: "utf8",
    shell: true,
    stdio: "inherit",
  });
}

function buildWithUnix(command, src) {
  const flags = ["-std=c++20", "-O2", "-I", join(root, "src"), ...src, "-o", output];
  if (warnings) {
    flags.splice(2, 0, "-Wall", "-Wextra", "-Werror", "-pedantic");
  }
  return run(command, flags, { stdio: "inherit" });
}

if (clean) {
  rmSync(outDir, { recursive: true, force: true });
}
mkdirSync(outDir, { recursive: true });

const src = sources();
const candidates = [
  ...(process.env.CXX ? [process.env.CXX] : []),
  ...(process.platform === "win32" ? ["clang++", "g++", "c++", "cl"] : ["c++", "g++", "clang++"]),
];

let attempted = [];
for (const candidate of candidates) {
  if (!candidate || !commandExists(candidate)) {
    continue;
  }
  attempted.push(candidate);
  const result = candidate === "cl" ? buildWithMsvc(candidate, src) : buildWithUnix(candidate, src);
  if (result.status === 0) {
    console.log(output);
    process.exit(0);
  }
}

for (const vcvars of findMsvcVcvars()) {
  attempted.push("msvc-vcvars");
  const result = buildWithMsvcVcvars(vcvars, src);
  if (result.status === 0) {
    console.log(output);
    process.exit(0);
  }
}

if (attempted.length === 0) {
  console.error("No C++ compiler found. Install g++, clang++, c++, or Visual Studio Build Tools.");
} else {
  console.error(`Compilation failed with: ${attempted.join(", ")}`);
}
process.exit(1);
