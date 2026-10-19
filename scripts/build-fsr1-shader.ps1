param([string]$Fxc)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$Fxc){$Fxc=Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\fxc.exe'|Sort-Object FullName -Descending|Select-Object -First 1 -ExpandProperty FullName}
if(!$Fxc -or !(Test-Path -LiteralPath $Fxc)){throw 'Pass the Windows SDK fxc.exe path using -Fxc'}
& $Fxc /nologo /O3 /T cs_5_1 /E main /Fh (Join-Path $root 'src/peripheral_fsr1_bytecode.hpp') /Vn peripheral_fsr1_bytecode (Join-Path $root 'src/peripheral_fsr1.hlsl')
if($LASTEXITCODE){throw 'FSR1 shader compilation failed'}
