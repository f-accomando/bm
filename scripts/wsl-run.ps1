<#
.SYNOPSIS
  Runs a command in WSL, inside this repository, from Windows. One form for every build and test.

.EXAMPLE
  scripts/wsl-run.ps1 "make test-audio"
  scripts/wsl-run.ps1 "make -j`$(nproc) build/kernel.img" -Tail 15
  scripts/wsl-run.ps1 -Script C:\path\to\scratchpad\t.sh test-riff
  scripts/wsl-run.ps1 "git status --short" -Tail 0

  -Tail N   keep the last N lines of output (default 40); 0 keeps everything.
  -Dir D    run in the subfolder D of the repository.
  -Script F run a script file (any Windows path) with bash, the rest of the line as its arguments.
The exit code is the command's own, not tail's.
#>
param(
  [Parameter(Position = 0)][string]$Cmd = '',
  [string]$Script = '',
  [int]$Tail = 40,
  [string]$Dir = '',
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest
)

$root = Split-Path -Parent $PSScriptRoot
$wroot = (wsl -e wslpath -a $root).Trim()
if (-not $wroot) { Write-Error 'wslpath failed: is WSL installed?'; exit 2 }
if ($Dir) { $wroot = "$wroot/$($Dir -replace '\\', '/')" }

if ($Script) {
  $ws = (wsl -e wslpath -a $Script).Trim()
  # with -Script the first bare word is bound to $Cmd: it is the script's first argument
  $args2 = @($Cmd) + $Rest | Where-Object { $_ }
  $Cmd = ("bash '{0}' {1}" -f $ws, ($args2 -join ' ')).Trim()
}
if (-not $Cmd) { Write-Error 'nothing to run'; exit 2 }

$pipe = if ($Tail -gt 0) { " 2>&1 | tail -n $Tail" } else { ' 2>&1' }
# PIPESTATUS keeps the command's exit code when tail is in the pipe
$line = "cd '$wroot' && { $Cmd; }$pipe; exit `${PIPESTATUS[0]}"
wsl -e bash -lc $line
exit $LASTEXITCODE
