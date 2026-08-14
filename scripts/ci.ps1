$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

$npm = if (Get-Command npm.cmd -ErrorAction SilentlyContinue) { "npm.cmd" } else { "npm" }

& $npm ci
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $npm run format:check
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node scripts\build.mjs --warnings
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node --test "tests/node/*.test.ts"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node scripts\smoke.mjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node scripts\check-loc.mjs
exit $LASTEXITCODE
