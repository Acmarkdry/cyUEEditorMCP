[CmdletBinding(PositionalBinding = $false)]
param(
	[string] $ProjectRoot
)

$ErrorActionPreference = "Stop"

$PluginRoot = Split-Path -Parent $PSScriptRoot
if (-not $ProjectRoot) {
	$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PluginRoot)
}

$ProjectRoot = (Resolve-Path $ProjectRoot).Path
$PluginRoot = (Resolve-Path $PluginRoot).Path

$ps1 = @"
[CmdletBinding(PositionalBinding = `$false)]
param(
	[Parameter(ValueFromRemainingArguments = `$true)]
	[string[]] `$Arguments,

	[Parameter(ValueFromPipeline = `$true)]
	[AllowEmptyString()]
	[string] `$PipelineInput
)

begin {
	`$ErrorActionPreference = "Stop"
	`$InputLines = New-Object System.Collections.Generic.List[string]
}

process {
	if (`$PSBoundParameters.ContainsKey("PipelineInput")) {
		`$InputLines.Add(`$PipelineInput)
	}
}

end {
	`$PluginRoot = Join-Path `$PSScriptRoot "Plugins\UEEditorMCP"
	if (`$InputLines.Count -gt 0) {
		(`$InputLines -join [Environment]::NewLine) | & (Join-Path `$PluginRoot "ue.ps1") @Arguments
	} else {
		& (Join-Path `$PluginRoot "ue.ps1") @Arguments
	}
	exit `$LASTEXITCODE
}
"@

$cmd = @"
@echo off
setlocal
set "PLUGIN_ROOT=%~dp0Plugins\UEEditorMCP"
call "%PLUGIN_ROOT%\ue.cmd" %*
exit /b %ERRORLEVEL%
"@

Set-Content -Path (Join-Path $ProjectRoot "ue.ps1") -Value $ps1 -Encoding UTF8
Set-Content -Path (Join-Path $ProjectRoot "ue.cmd") -Value $cmd -Encoding ASCII

Write-Host "Installed project launchers:"
Write-Host "  $(Join-Path $ProjectRoot 'ue.ps1')"
Write-Host "  $(Join-Path $ProjectRoot 'ue.cmd')"
