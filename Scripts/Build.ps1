param([string]$EnginePath = 'D:\epic\UE_5.8')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$ProjectFile = Join-Path $ProjectRoot 'Gridbound.uproject'
$BuildLogDir = Join-Path $ProjectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $BuildLogDir -Force | Out-Null
$Dotnet = Join-Path $EnginePath 'Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe'
$BuildTool = Join-Path $EnginePath 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
& $Dotnet $BuildTool GridboundEditor Win64 Development "-Project=$ProjectFile" "-Log=$BuildLogDir\Build.log" -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw 'GridboundEditor compilation failed; see Saved/Logs/Build.log' }
$Editor = Join-Path $EnginePath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
& $Editor $ProjectFile -run=pythonscript "-script=$PSScriptRoot\create_art.py" -unattended -nullrhi -nosplash -NoSound -DDC=GridboundLocal
if ($LASTEXITCODE -ne 0) { throw 'Art material creation failed; see Saved/Logs' }
& $Editor $ProjectFile -run=pythonscript "-script=$PSScriptRoot\create_map.py" -unattended -nullrhi -nosplash -NoSound -DDC=GridboundLocal
if ($LASTEXITCODE -ne 0) { throw 'Map creation failed; see Saved/Logs' }
