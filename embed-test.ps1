# F1 embed smoke test for the launcher demo player.
#
# Creates a plain WinForms window and launches the patched oDFe as a CHILD
# of it (via "+set in_embedParent <hwnd>"). If the embed patch works, the
# Quake3 view renders INSIDE this window instead of opening its own.
#
# Run from PowerShell on the Windows host (the WSL2 host machine):
#   powershell -ExecutionPolicy Bypass -File embed-test.ps1 `
#       -Q3Path "C:\Games\Quake3" `
#       -Exe   "\\wsl$\Ubuntu\home\lukas\projects\oDFe\build\release-mingw64-x86_64\oDFe.x64.exe" `
#       -Demo  "demo/somedemo.dm_68"
#
# -Q3Path  : your Quake3/defrag install (the folder that contains baseq3\).
# -Exe     : path to the patched oDFe.x64.exe (copy it next to Q3Path first
#            if \\wsl$ is slow). Also copy oDFe_opengl_x86_64.dll beside it.
# -Demo    : optional. A demo path relative to baseq3 (.dm_68). Omit to just
#            land in the main menu - that alone proves the window embeds.
#
# SUCCESS  = the game renders inside this gray window, no separate game window.
# FAILURE  = a separate game window opens, or it errors / renders nothing.

param(
    [Parameter(Mandatory = $true)] [string]$Q3Path,
    [Parameter(Mandatory = $true)] [string]$Exe,
    [string]$Demo = "",
    # Render-area (client) size. In embed mode the engine renders to THIS
    # size and ignores r_mode/r_customwidth in the config, so pick the
    # aspect ratio you actually use. Default ~21:9 to match a 21:9 desktop.
    [int]$Width  = 1680,
    [int]$Height = 720
)

# MINIMAL isolation test: ONLY embed + auto-play a demo. No control channel,
# no buttons, no timer, no sub-panel - the engine renders straight into the
# form. If this is smooth but control-test.ps1 stutters, the culprit is the
# control-channel / timer work on the UI thread, not the engine.

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Exe))    { Write-Error "Exe not found: $Exe"; exit 1 }
if (-not (Test-Path $Q3Path)) { Write-Error "Q3Path not found: $Q3Path"; exit 1 }

# Auto-pick the first demo so we land straight in playback (no mouse-less menu).
if ($Demo -eq "") {
    $first = Get-ChildItem -Path (Join-Path $Q3Path "defrag\demos") -Filter *.dm_68 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($first) { $Demo = $first.Name }
}
Write-Host "Demo: $Demo"

$form               = New-Object System.Windows.Forms.Form
$form.Text          = "oDFe embed test ($Width x $Height client - engine renders to this size)"
# ClientSize = the actual render area handed to the engine (excludes the
# title bar / borders), so the aspect ratio is exactly what we ask for.
$form.ClientSize    = New-Object System.Drawing.Size($Width, $Height)
$form.StartPosition = "CenterScreen"
$form.BackColor     = [System.Drawing.Color]::DimGray

# We must have the native window handle created before launching the engine.
$form.Add_Shown({
    $hwnd = $form.Handle.ToInt64()
    Write-Host "Host window HWND = $hwnd"

    $args = @(
        "+set", "in_embedParent", "$hwnd",
        # keep it from trying to change display mode / go fullscreen
        "+set", "r_fullscreen", "0",
        "+set", "fs_basepath", "`"$Q3Path`"",
        "+set", "fs_game", "defrag"
    )
    if ($Demo -ne "") {
        $args += @("+demo", $Demo)
    }

    Write-Host "Launching: $Exe $($args -join ' ')"
    # Working dir = the engine's own folder (so it finds its renderer DLL);
    # fs_basepath points the filesystem at the Quake3 install.
    $exeDir = Split-Path -Parent $Exe
    $global:proc = Start-Process -FilePath $Exe -ArgumentList $args -WorkingDirectory $exeDir -PassThru
})

# When the host window closes, kill the engine too.
$form.Add_FormClosed({
    if ($global:proc -and -not $global:proc.HasExited) {
        try { $global:proc.Kill() } catch {}
    }
})

[System.Windows.Forms.Application]::Run($form)
