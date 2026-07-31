# Requires -Version 5.0
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet("package", "clean")]
	[string] $Action,

	[Parameter(Mandatory = $true)]
	[string] $CProject,

	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]] $PassThrough
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $CProject)) {
	Write-Error "cproject not found: $CProject"
	exit 1
}

$jsonText = Get-Content -LiteralPath $CProject -Raw -Encoding UTF8
$data = $jsonText | ConvertFrom-Json
$engineRaw = [string]$data.EngineDirectory
if ([string]::IsNullOrWhiteSpace($engineRaw)) {
	Write-Error "EngineDirectory missing in $CProject"
	exit 1
}

$projectDir = Split-Path -Parent $CProject
if ([System.IO.Path]::IsPathRooted($engineRaw)) {
	$engine = [System.IO.Path]::GetFullPath($engineRaw)
} else {
	$engine = [System.IO.Path]::GetFullPath((Join-Path $projectDir $engineRaw))
}

# Installer layout: Tools\python\python.exe ; venv: Tools\python\Scripts\python.exe
$localPy = Join-Path $engine "Tools\python\python.exe"
if (-not (Test-Path -LiteralPath $localPy)) {
	$localPy = Join-Path $engine "Tools\python\Scripts\python.exe"
}
if (-not (Test-Path -LiteralPath $localPy)) {
	Write-Error "Engine local Python missing under Tools\python (or Scripts).`nRun setup.bat in the Maho engine root first."
	exit 1
}

if ($Action -eq "package") {
	# GUI via WScript + pythonw — no Python console attached to this process.
	$vbs = Join-Path $engine "Tools\launch_package.vbs"
	if (-not (Test-Path -LiteralPath $vbs)) {
		Write-Error "Missing package launcher: $vbs"
		exit 1
	}
	$wscript = Join-Path $env:SystemRoot "System32\wscript.exe"
	$p = Start-Process -FilePath $wscript -ArgumentList @("//nologo", $vbs, $CProject) -PassThru -Wait
	exit $p.ExitCode
}

# clean — console CLI via maho_python.bat
$mahoPython = Join-Path $engine "Tools\maho_python.bat"
if (-not (Test-Path -LiteralPath $mahoPython)) {
	Write-Error "Missing $mahoPython"
	exit 1
}

$cleanPy = Join-Path $engine "Tools\clean.py"
if (-not (Test-Path -LiteralPath $cleanPy)) {
	Write-Error "Missing clean script: $cleanPy"
	exit 1
}

function Quote-Arg([string] $Value) {
	if ($Value -match '[\s"]') {
		return '"' + ($Value -replace '"', '\"') + '"'
	}
	return $Value
}

$scriptArgs = @($cleanPy, $projectDir)
if ($PassThrough) {
	$scriptArgs += $PassThrough
}
$quoted = ($scriptArgs | ForEach-Object { Quote-Arg $_ }) -join " "
$cmdline = "`"$mahoPython`" $quoted"
cmd.exe /c $cmdline
exit $LASTEXITCODE
