param(
    [string]$GamePath = 'D:\SteamLibrary\steamapps\common\Mafia10\Mafia\Game.exe',
    [string]$ProfilePath = (Join-Path $PSScriptRoot '..\config\mafia-1.0.profile.json')
)

$ErrorActionPreference = 'Stop'
$profile = Get-Content -LiteralPath $ProfilePath -Raw | ConvertFrom-Json
$bytes = [IO.File]::ReadAllBytes($GamePath)
if ((Get-FileHash -LiteralPath $GamePath -Algorithm SHA256).Hash -ne $profile.sha256) {
    throw 'Executable SHA-256 does not match the supported profile.'
}
if ([BitConverter]::ToUInt16($bytes, 0) -ne 0x5A4D) { throw 'Not a PE executable.' }
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { throw 'Invalid PE signature.' }
if ([BitConverter]::ToUInt16($bytes, $pe + 4) -ne $profile.machine) { throw 'Not the supported x86 machine.' }
$optional = $pe + 24
if ([BitConverter]::ToUInt16($bytes, $optional) -ne 0x10B) { throw 'Expected a PE32 image.' }
if ([BitConverter]::ToUInt32($bytes, $optional + 28) -ne [Convert]::ToUInt32($profile.imageBase, 16)) {
    throw 'Unexpected image base.'
}
$sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
$sections = $optional + [BitConverter]::ToUInt16($bytes, $pe + 20)
foreach ($symbol in $profile.symbols) {
    $rva = [Convert]::ToUInt32($symbol.rva, 16)
    $expected = @($symbol.bytes.Split(' ') | ForEach-Object { [Convert]::ToByte($_, 16) })
    $offset = -1
    for ($index = 0; $index -lt $sectionCount; ++$index) {
        $entry = $sections + 40 * $index
        $start = [BitConverter]::ToUInt32($bytes, $entry + 12)
        $size = [BitConverter]::ToUInt32($bytes, $entry + 16)
        if ($rva -ge $start -and $rva + $expected.Count -le $start + $size) {
            $offset = $rva - $start + [BitConverter]::ToUInt32($bytes, $entry + 20)
            break
        }
    }
    if ($offset -lt 0) { throw "Unmapped profile RVA: $($symbol.name)" }
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        if ($bytes[$offset + $index] -ne $expected[$index]) {
            throw "Signature mismatch: $($symbol.name) at $($symbol.rva)"
        }
    }
    Write-Output "PASS: $($symbol.name) $($symbol.rva)"
}
Write-Output "Verified $($profile.symbols.Count) symbols for $($profile.name)."