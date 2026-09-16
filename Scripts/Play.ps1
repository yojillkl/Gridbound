param([string]$EnginePath = 'D:\epic\UE_5.8')
$ProjectFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'Gridbound.uproject'
$Editor = Join-Path $EnginePath 'Engine\Binaries\Win64\UnrealEditor.exe'
& $Editor $ProjectFile /Game/Maps/GridArena -game -windowed -ResX=1280 -ResY=720 -DDC=GridboundLocal
