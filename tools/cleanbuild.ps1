param([switch]$Test)

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "$env:SystemRoot\System32\cmd.exe"
$psi.Arguments = if ($Test) { '/c build.bat test' } else { '/c build.bat' }
$psi.WorkingDirectory = $root
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.Environment.Clear()
$sys = "$env:SystemRoot\System32"
$psi.Environment['SystemRoot'] = $env:SystemRoot
$psi.Environment['windir'] = $env:SystemRoot
$psi.Environment['ComSpec'] = "$sys\cmd.exe"
$psi.Environment['PATH'] = "$sys;$env:SystemRoot;$sys\Wbem"
$psi.Environment['TEMP'] = $env:TEMP
$psi.Environment['TMP'] = $env:TEMP
$psi.Environment['USERPROFILE'] = $env:USERPROFILE
$psi.Environment['ProgramFiles'] = ${env:ProgramFiles}
$psi.Environment['ProgramFiles(x86)'] = ${env:ProgramFiles(x86)}
$psi.Environment['ProgramData'] = $env:ProgramData
$psi.Environment['LOCALAPPDATA'] = $env:LOCALAPPDATA
$psi.Environment['APPDATA'] = $env:APPDATA
$psi.Environment['PROCESSOR_ARCHITECTURE'] = 'AMD64'
$process = [System.Diagnostics.Process]::Start($psi)
$stdout = $process.StandardOutput.ReadToEnd()
$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()
$stdout
if ($stderr) { "STDERR: $stderr" }
"exit code: $($process.ExitCode)"
if ($process.ExitCode -ne 0) { exit $process.ExitCode }