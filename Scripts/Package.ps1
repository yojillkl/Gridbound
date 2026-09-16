param(
    [Parameter(Mandatory=$true)][string]$EnginePath,
    [string]$OutputPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'Releases\v0.1.0'),
    [ValidateSet('Development','Shipping')][string]$Configuration = 'Development'
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$ProjectFile = Join-Path $ProjectRoot 'Gridbound.uproject'
$AutomationTool = Join-Path $EnginePath 'Engine\Build\BatchFiles\RunUAT.bat'
if (!(Test-Path -LiteralPath $AutomationTool)) { throw "Unreal Automation Tool not found: $AutomationTool" }
& $AutomationTool BuildCookRun "-project=$ProjectFile" -noP4 -platform=Win64 "-clientconfig=$Configuration" -build -cook -stage -pak -iostore -compressed -prereqs -nodebuginfo -archive "-archivedirectory=$OutputPath" -unattended -utf8output -DDC=GridboundLocal
if ($LASTEXITCODE -ne 0) { throw "Packaging failed with exit code $LASTEXITCODE" }
