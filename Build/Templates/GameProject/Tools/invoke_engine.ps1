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

$cattyPython = Join-Path $engine "Tools\catty_python.bat"
$localPy = Join-Path $engine "Tools\python\python.exe"
if (-not (Test-Path -LiteralPath $localPy)) {
	Write-Error "Engine local Python missing: $localPy`nRun setup.bat in the Catty engine root first."
	exit 1
}
if (-not (Test-Path -LiteralPath $cattyPython)) {
	Write-Error "Missing $cattyPython"
	exit 1
}

Write-Host "[Catty] Project : $CProject"
Write-Host "[Catty] Engine  : $engine"
Write-Host "[Catty] Action  : $Action"

$scriptArgs = @()
if ($Action -eq "package") {
	$ui = Join-Path $engine "Tools\package_ui.py"
	if (-not (Test-Path -LiteralPath $ui)) {
		Write-Error "Missing package UI: $ui"
		exit 1
	}
	$scriptArgs = @($ui, $CProject)
} elseif ($Action -eq "clean") {
	$cleanPy = Join-Path $engine "Tools\clean.py"
	if (-not (Test-Path -LiteralPath $cleanPy)) {
		Write-Error "Missing clean script: $cleanPy"
		exit 1
	}
	$scriptArgs = @($cleanPy, $projectDir)
	if ($PassThrough) {
		$scriptArgs += $PassThrough
	}
}

function Quote-Arg([string] $Value) {
	if ($Value -match '[\s"]') {
		return '"' + ($Value -replace '"', '\"') + '"'
	}
	return $Value
}

$quoted = ($scriptArgs | ForEach-Object { Quote-Arg $_ }) -join " "
$cmdline = "`"$cattyPython`" $quoted"
cmd.exe /c $cmdline
exit $LASTEXITCODE
