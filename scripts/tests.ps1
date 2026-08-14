$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

& node scripts\build.mjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node --test "tests/node/*.test.ts"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& node scripts\smoke.mjs
exit $LASTEXITCODE
