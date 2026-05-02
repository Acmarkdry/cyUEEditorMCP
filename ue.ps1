[CmdletBinding(PositionalBinding = $false)]
param(
	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]] $Arguments,

	[Parameter(ValueFromPipeline = $true)]
	[AllowEmptyString()]
	[string] $PipelineInput
)

begin {
	$ErrorActionPreference = "Stop"
	$InputLines = New-Object System.Collections.Generic.List[string]
}

process {
	if ($PSBoundParameters.ContainsKey("PipelineInput")) {
		$InputLines.Add($PipelineInput)
	}
}

end {
	$PluginRoot = $PSScriptRoot
	$VenvPython = Join-Path $PluginRoot "Python\.venv\Scripts\python.exe"
	$UeEntry = Join-Path $PluginRoot "Python\ue.py"

	if (Test-Path $VenvPython) {
		$Python = $VenvPython
	} else {
		$Python = "python"
	}

	if ($InputLines.Count -gt 0) {
		($InputLines -join [Environment]::NewLine) | & $Python $UeEntry @Arguments
	} else {
		& $Python $UeEntry @Arguments
	}
	exit $LASTEXITCODE
}
