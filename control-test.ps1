# F2/F3 control-channel test for the launcher demo player.
#
# Embeds the patched oDFe inside a host PANEL (engine on top) with a transport
# BAR docked underneath: Pause / Play / speed buttons + a draggable SCRUBBER
# with a LIVE playhead that follows the demo as it plays.
#
# Threading model (this is the important bit):
#   An embedded child window shares its input queue with the host UI thread, so
#   any BLOCKING socket read on the UI thread stutters the engine. So the status
#   reads happen on a BACKGROUND RUNSPACE that owns the single control socket and
#   blocks there; it parks the latest playhead position in a synchronized table.
#   The UI thread only (a) WRITES commands (non-blocking, never stuttered) on the
#   same stream and (b) runs a cheap 200 ms timer that copies the parked position
#   into the slider. No socket reads ever touch the UI thread.
#   (The real launcher does exactly this with a Rust background thread.)
#
# Usage (Windows host):
#   powershell -ExecutionPolicy Bypass -File control-test.ps1 `
#       -Q3Path "E:\GamesLibraries\_Random-Games\QIIIA\QIIIAdfNEW" -Exe ".\oDFe.x64.exe"
#
# A demo auto-plays. The scrubber follows playback; drag it to seek; use the
# buttons for pause/speed.

param(
    [Parameter(Mandatory = $true)] [string]$Q3Path,
    [Parameter(Mandatory = $true)] [string]$Exe,
    [string]$Demo = "",
    [int]$Width  = 1680,
    [int]$Height = 720,
    [int]$Port   = 28960,
    [int]$Vsync  = 1,
    [int]$MaxSeconds = 180     # initial scrubber range; replaced by the real length
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Exe))    { Write-Error "Exe not found: $Exe"; exit 1 }
if (-not (Test-Path $Q3Path)) { Write-Error "Q3Path not found: $Q3Path"; exit 1 }

if ($Demo -eq "") {
    $first = Get-ChildItem -Path (Join-Path $Q3Path "defrag\demos") -Filter *.dm_68 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($first) { $Demo = $first.Name }
}
Write-Host "Demo: $Demo"

# --- Render aspect ratio from the player's config (port of oDFe CL_GetModeInfo) ---
# The engine renders to its host window's client rect and ignores the config's
# r_mode/r_customwidth/etc, so WE must reproduce what resolution the engine would
# have used and keep that ASPECT - otherwise the HUD (which is anchored to the
# view height) and the FOV come out wrong. We fit the engine into a centered
# region of that aspect and letterbox the rest with black bars.
$CL_VID_MODES = @(
    @(320,240),@(400,300),@(512,384),@(640,480),@(800,600),@(960,720),
    @(1024,768),@(1152,864),@(1280,1024),@(1600,1200),@(2048,1536),@(856,480),
    @(1280,960),@(1280,720),@(1280,800),@(1366,768),@(1440,900),@(1600,900),
    @(1680,1050),@(1920,1080),@(1920,1200),@(2560,1080),@(3440,1440),@(3840,2160),@(4096,2160)
)

function Parse-VideoCvars([string[]]$paths) {
    $cv = @{ r_mode = -2; r_customwidth = 1600; r_customheight = 1024;
             r_custompixelaspect = 1.0; r_fullscreen = $false; r_modefullscreen = "" }
    foreach ($p in $paths) {
        if (-not (Test-Path -LiteralPath $p)) { continue }
        foreach ($line in (Get-Content -LiteralPath $p)) {
            $parts = $line.Trim() -split '\s+', 3
            if ($parts.Count -lt 3) { continue }
            if ($parts[0].ToLower() -notin @('set','seta','sets','setu')) { continue }
            $name = $parts[1].ToLower()
            $val  = $parts[2].Trim().Trim('"').Trim()
            $iv = 0; $fv = 0.0
            switch ($name) {
                'r_mode'              { if ([int]::TryParse($val, [ref]$iv))   { $cv.r_mode = $iv } }
                'r_customwidth'       { if ([int]::TryParse($val, [ref]$iv))   { $cv.r_customwidth = $iv } }
                'r_customheight'      { if ([int]::TryParse($val, [ref]$iv))   { $cv.r_customheight = $iv } }
                'r_custompixelaspect' { if ([double]::TryParse($val, [ref]$fv) -and $fv -gt 0) { $cv.r_custompixelaspect = $fv } }
                'r_fullscreen'        { $cv.r_fullscreen = ($val -ne '0' -and $val -ne '') }
                'r_modefullscreen'    { $cv.r_modefullscreen = $val }
            }
        }
    }
    $cv
}

function Resolve-RenderAspect($cv, [int]$dw, [int]$dh) {
    $mode = $cv.r_mode
    if ($cv.r_fullscreen -and $cv.r_modefullscreen -ne '') {
        $m = 0; if ([int]::TryParse($cv.r_modefullscreen.Trim(), [ref]$m)) { $mode = $m }
    }
    if ($mode -lt -2 -or $mode -ge $CL_VID_MODES.Count) { return $null }
    if ($mode -eq -2 -and ($dw -eq 0 -or $dh -eq 0)) { $mode = 3 }
    if     ($mode -eq -2) { $w = $dw; $h = $dh; $pa = $cv.r_custompixelaspect }
    elseif ($mode -eq -1) { $w = $cv.r_customwidth; $h = $cv.r_customheight; $pa = $cv.r_custompixelaspect }
    else                  { $w = $CL_VID_MODES[$mode][0]; $h = $CL_VID_MODES[$mode][1]; $pa = 1.0 }
    if ($w -le 0 -or $h -le 0) { return $null }
    return ($w / ($h * $pa))
}

# config exec order: baseq3 then the mod, q3config then autoexec (later wins)
$cfgPaths = @(
    (Join-Path $Q3Path 'baseq3\q3config.cfg'), (Join-Path $Q3Path 'baseq3\autoexec.cfg'),
    (Join-Path $Q3Path 'defrag\q3config.cfg'), (Join-Path $Q3Path 'defrag\autoexec.cfg')
)
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$cv = Parse-VideoCvars $cfgPaths
$global:aspect = Resolve-RenderAspect $cv $screen.Width $screen.Height
if ($null -eq $global:aspect -or $global:aspect -le 0) { $global:aspect = $screen.Width / $screen.Height }
Write-Host ("Render aspect: {0:N3} (r_mode={1}, desktop={2}x{3})" -f $global:aspect, $cv.r_mode, $screen.Width, $screen.Height)

# Shared state between the UI thread and the background reader runspace.
$sync = [hashtable]::Synchronized(@{
    pos       = 0       # demo playhead (ms from demo start), de-biased by engine
    total     = 0       # demo length (ms), 0 until measured
    connected = $false
    stop      = $false
    writer    = $null   # StreamWriter on the single control socket (UI writes)
})

$barH = 70
$form            = New-Object System.Windows.Forms.Form
$form.Text       = "oDFe demo player test - live playhead, drag to seek"
$form.ClientSize = New-Object System.Drawing.Size($Width, ($Height + $barH + 30))  # +30 for the top name bar
$form.StartPosition = "CenterScreen"

$host_panel = New-Object System.Windows.Forms.Panel
$host_panel.Dock = "Fill"
$host_panel.BackColor = [System.Drawing.Color]::Black
$form.Controls.Add($host_panel)

# The engine embeds into THIS stage (not host_panel directly). We size the stage
# to the config's aspect ratio, centered inside host_panel; the black host_panel
# shows through as letterbox/pillarbox bars. So the engine always renders at the
# right aspect and the HUD/FOV stay correct no matter the window shape.
$stage = New-Object System.Windows.Forms.Panel
$stage.BackColor = [System.Drawing.Color]::Black
$host_panel.Controls.Add($stage)

function Fit-Stage {
    $availW = $host_panel.ClientSize.Width
    $availH = $host_panel.ClientSize.Height
    if ($availW -le 0 -or $availH -le 0) { return }
    $w = $availW
    $h = [int][math]::Round($w / $global:aspect)
    if ($h -gt $availH) { $h = $availH; $w = [int][math]::Round($h * $global:aspect) }
    $x = [int](($availW - $w) / 2)
    $y = [int](($availH - $h) / 2)
    $stage.Bounds = New-Object System.Drawing.Rectangle($x, $y, $w, $h)
}

$bar = New-Object System.Windows.Forms.Panel
$bar.Dock = "Bottom"
$bar.Height = $barH
$bar.BackColor = [System.Drawing.Color]::FromArgb(24, 24, 27)
$form.Controls.Add($bar)

# Top bar with the demo name (the engine is a native child window, so the name
# has to live in a bar around it, not overlaid on the video - the real launcher
# does the same with its own top bar).
$topbar = New-Object System.Windows.Forms.Panel
$topbar.Dock = "Top"
$topbar.Height = 30
$topbar.BackColor = [System.Drawing.Color]::FromArgb(18, 18, 20)
$form.Controls.Add($topbar)

# Parse "map[physics]MM.SS.mmm(player.country).dm_68" into something readable.
function Format-DemoName([string]$name) {
    $n = $name -replace '\.dm_68$', ''
    $m = [regex]::Match($n, '^(?<map>.+?)\[(?<phys>[^\]]+)\](?<mm>\d{2})\.(?<ss>\d{2})\.(?<ms>\d{3})\((?<player>[^.]+)\.(?<country>[^)]+)\)')
    if ($m.Success) {
        return ("{0}    {1}    {2}:{3}.{4}    {5} ({6})" -f `
            $m.Groups['map'].Value, $m.Groups['phys'].Value, `
            $m.Groups['mm'].Value, $m.Groups['ss'].Value, $m.Groups['ms'].Value, `
            $m.Groups['player'].Value, $m.Groups['country'].Value)
    }
    return $n
}

$nameLbl = New-Object System.Windows.Forms.Label
$nameLbl.Dock = "Fill"
$nameLbl.Text = if ($Demo -ne "") { Format-DemoName $Demo } else { "(no demo)" }
$nameLbl.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$nameLbl.ForeColor = [System.Drawing.Color]::FromArgb(225, 225, 230)
$nameLbl.TextAlign = "MiddleCenter"
$topbar.Controls.Add($nameLbl)

$btnFont = New-Object System.Drawing.Font("Segoe UI", 10, [System.Drawing.FontStyle]::Bold)
$lblFont = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)

# Write a command on the single shared control socket. Writes are quick and
# non-blocking, so they are safe on the UI thread (only reads stutter).
function Send-Cmd([string]$cmd) {
    try {
        $w = $sync.writer
        if ($w) { $w.WriteLine($cmd) }
    } catch { }
}

$global:btnX = 10
function Add-Button([string]$text, [int]$w, $back, [scriptblock]$onClick) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $text
    $b.Location = New-Object System.Drawing.Point($global:btnX, 14)
    $b.Size = New-Object System.Drawing.Size($w, 42)
    $b.Font = $btnFont
    $b.ForeColor = [System.Drawing.Color]::White
    $b.BackColor = $back
    $b.FlatStyle = "Flat"
    $b.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(80, 80, 86)
    $b.TextAlign = "MiddleCenter"
    $b.AutoEllipsis = $false
    $b.UseCompatibleTextRendering = $false
    $b.Add_Click($onClick)
    $bar.Controls.Add($b)
    $global:btnX += $w + 4
}

$cPlay  = [System.Drawing.Color]::FromArgb(34, 110, 60)
$cSpeed = [System.Drawing.Color]::FromArgb(45, 50, 70)

# Pause holds the demo clock (demopause 1) without touching timescale, so the
# cgame doesn't draw "Connection Interrupted". Play / speed buttons clear the
# pause and set the playback rate via timescale.
Add-Button "Pause" 68 $cPlay  { Send-Cmd "demopause 1" }
Add-Button "Play"  60 $cPlay  { Send-Cmd "demopause 0; timescale 1" }
Add-Button "0.1x"  56 $cSpeed { Send-Cmd "demopause 0; timescale 0.1" }
Add-Button "0.25x" 64 $cSpeed { Send-Cmd "demopause 0; timescale 0.25" }
Add-Button "0.5x"  60 $cSpeed { Send-Cmd "demopause 0; timescale 0.5" }
Add-Button "1x"    48 $cSpeed { Send-Cmd "demopause 0; timescale 1" }
Add-Button "2x"    48 $cSpeed { Send-Cmd "demopause 0; timescale 2" }
Add-Button "4x"    48 $cSpeed { Send-Cmd "demopause 0; timescale 4" }
Add-Button "8x"    48 $cSpeed { Send-Cmd "demopause 0; timescale 8" }

# Time label (current playhead / length).
$lbl = New-Object System.Windows.Forms.Label
$lbl.Text = "0:00"
$lbl.Font = $lblFont
$lbl.ForeColor = [System.Drawing.Color]::White
$lbl.AutoSize = $false
$lbl.TextAlign = "MiddleCenter"
$lbl.Size = New-Object System.Drawing.Size(86, 42)
$lbl.Location = New-Object System.Drawing.Point(($Width - 98), 14)
$bar.Controls.Add($lbl)

# Scrubber. Range is in seconds; updated to the real length once measured.
$scrub = New-Object System.Windows.Forms.TrackBar
$scrub.Minimum = 0
$scrub.Maximum = $MaxSeconds
$scrub.TickFrequency = 10
$scrub.SmallChange = 1
$scrub.LargeChange = 5
$scrubX = $global:btnX + 12
$scrub.Location = New-Object System.Drawing.Point($scrubX, 16)
$scrub.Size = New-Object System.Drawing.Size(($Width - $scrubX - 110), 40)
$bar.Controls.Add($scrub)

$fmt = { param($s) "{0}:{1:D2}" -f [math]::Floor($s / 60), ($s % 60) }

# Dragging state: while the user holds the scrubber, the live timer must not
# fight them by writing the playhead back.
$global:dragging = $false
$global:suppress = $false        # set while WE move the slider, so events are ignored
$global:seekHoldUntil = 0        # tick time until which the live timer leaves the slider alone
$global:seekTarget = 0           # running ms target for arrow-key seeking
$global:lastArrowTick = -10000   # tick of the last arrow seek (to tell tap from hold)
$global:measured = $false        # have we learned the demo's real length yet?
$global:measureTick = -10000     # tick of the last measurement request
$global:lastW = -1               # last engine-area size (to detect window resize)
$global:lastH = -1
$global:resizeTick = 0           # tick of the last size change (debounce)
$global:resizePending = $false   # a resize is waiting to settle before we re-create

# Click anywhere on the bar to jump there (not just step), then drag.
$scrub.Add_MouseDown({
    param($s, $e)
    $range = $scrub.Maximum - $scrub.Minimum
    if ($range -gt 0 -and $scrub.Width -gt 0) {
        $val = [int][math]::Round($scrub.Minimum + $range * ($e.X / [double]$scrub.Width))
        if ($val -lt $scrub.Minimum) { $val = $scrub.Minimum }
        if ($val -gt $scrub.Maximum) { $val = $scrub.Maximum }
        $global:suppress = $true; $scrub.Value = $val; $global:suppress = $false
    }
    $global:dragging = $true
})
$scrub.Add_Scroll({ if (-not $global:suppress) { $lbl.Text = (& $fmt $scrub.Value) + " / " + (& $fmt $scrub.Maximum) } })
$doSeek = {
    Send-Cmd ("seekdemo " + ($scrub.Value * 1000))
    $lbl.Text = (& $fmt $scrub.Value) + " / " + (& $fmt $scrub.Maximum)
    $global:dragging = $false
    # hold the live updater off the slider for a moment, so stale status lines
    # (the engine reports the OLD position until it applies the seek) don't yank
    # the playhead back before snapping to where we clicked.
    $global:seekHoldUntil = [Environment]::TickCount + 700
}
$scrub.Add_MouseUp($doSeek)
$scrub.Add_KeyUp($doSeek)

# Arrow keys: Right/Left seek the demo. A single tap jumps +/-5 s; holding the
# key (auto-repeat) scrubs smoothly in that direction. We accumulate a local
# target so repeats progress (the live position is held off briefly after each
# seek, so it can't be read back between rapid repeats).
$arrowSeek = {
    param($s, $e)
    $dir = 0
    if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Right) { $dir = 1 }
    elseif ($e.KeyCode -eq [System.Windows.Forms.Keys]::Left) { $dir = -1 }
    if ($dir -eq 0) { return }
    $e.Handled = $true
    $now = [Environment]::TickCount
    $gap = $now - $global:lastArrowTick
    if ($gap -gt 350) {
        # fresh tap: 5 s from the current live position
        $global:seekTarget = $sync.pos + $dir * 5000
    } else {
        # held (auto-repeat): smooth continuous scrub, throttled
        if ($gap -lt 90) { return }
        $global:seekTarget = $global:seekTarget + $dir * 2000
    }
    $global:lastArrowTick = $now
    if ($global:seekTarget -lt 0) { $global:seekTarget = 0 }
    if ($sync.total -gt 0 -and $global:seekTarget -gt $sync.total) { $global:seekTarget = $sync.total }
    Send-Cmd ("seekdemo " + [int]$global:seekTarget)
    # reflect on the slider and hold the live updater off for a moment
    $sec = [int][math]::Round($global:seekTarget / 1000.0)
    if ($sec -lt $scrub.Minimum) { $sec = $scrub.Minimum }
    if ($sec -gt $scrub.Maximum) { $sec = $scrub.Maximum }
    $global:suppress = $true; $scrub.Value = $sec; $global:suppress = $false
    $lbl.Text = (& $fmt $sec) + " / " + (& $fmt $scrub.Maximum)
    $global:seekHoldUntil = $now + 300
}
$form.KeyPreview = $true
$form.Add_KeyDown($arrowSeek)

# Background reader: owns the single control socket, blocks on reads off the UI
# thread, parses status lines, and parks the latest position in $sync.
$readerScript = {
    param($sync, $port)
    while (-not $sync.stop) {
        try {
            $tcp = New-Object System.Net.Sockets.TcpClient
            $tcp.Connect("127.0.0.1", $port)             # retries below until the engine's listener is up
            $stream = $tcp.GetStream()
            $writer = New-Object System.IO.StreamWriter($stream)
            $writer.AutoFlush = $true
            $sync.writer = $writer                       # hand the write side to the UI thread
            $sync.connected = $true
            $reader = New-Object System.IO.StreamReader($stream)
            while (-not $sync.stop) {
                $line = $reader.ReadLine()               # BLOCKS here, on this background thread
                if ($null -eq $line) { break }
                # "status time <ms> start <ms> total <ms> demo .. paused .. atend .."
                $p = $line -split '\s+'
                $t = $null; $st = $null; $tot = $null
                for ($i = 0; $i -lt $p.Length - 1; $i++) {
                    switch ($p[$i]) {
                        'time'  { $t   = [int]$p[$i + 1] }
                        'start' { $st  = [int]$p[$i + 1] }
                        'total' { $tot = [int]$p[$i + 1] }
                    }
                }
                if ($null -ne $t -and $null -ne $st) { $sync.pos = $t - $st }
                if ($null -ne $tot -and $null -ne $st) { $sync.total = $tot - $st }
            }
        } catch { }
        $sync.connected = $false
        $sync.writer = $null
        Start-Sleep -Milliseconds 500                    # engine not up yet / dropped: retry
    }
}

$rs = [runspacefactory]::CreateRunspace()
$rs.ApartmentState = "MTA"
$rs.ThreadOptions  = "ReuseThread"
$rs.Open()
$rps = [powershell]::Create()
$rps.Runspace = $rs
$rps.AddScript($readerScript).AddArgument($sync).AddArgument($Port) | Out-Null
$global:readerHandle = $rps.BeginInvoke()
$global:reader_ps = $rps
$global:reader_rs = $rs

# Cheap UI timer: copy the parked playhead into the slider. No socket work here.
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200
$timer.Add_Tick({
    # Window resize -> re-create the engine at the new size (debounced). A
    # vid_restart destroys+recreates the embedded child window, which re-reads
    # the parent's (now larger/smaller) client rect, so the demo re-renders at
    # the new resolution. Debounced so a drag-resize only restarts once it settles.
    $cw = $host_panel.ClientSize.Width
    $ch = $host_panel.ClientSize.Height
    if ($global:lastW -lt 0) {
        $global:lastW = $cw; $global:lastH = $ch
    } elseif ($cw -ne $global:lastW -or $ch -ne $global:lastH) {
        $global:lastW = $cw; $global:lastH = $ch
        Fit-Stage                  # reposition the aspect-correct stage + bars now
        $global:resizeTick = [Environment]::TickCount
        $global:resizePending = $true
    } elseif ($global:resizePending -and ([Environment]::TickCount - $global:resizeTick) -gt 350) {
        $global:resizePending = $false
        # re-create the engine to fill the (resized) stage at its new resolution
        if ($sync.connected -and $stage.Width -gt 0 -and $stage.Height -gt 0) { Send-Cmd "vid_restart" }
    }

    # One-time length measurement: the engine only knows the demo's length once
    # it has read to the end, so right after connecting we seek far past the end
    # (engine clamps + reports the real total) and then jump back to the start.
    # The real launcher does this at load behind a loading screen.
    if (-not $global:measured) {
        if ($sync.total -gt 0) {
            $scrub.Maximum = [math]::Max(1, [int][math]::Round($sync.total / 1000.0))
            Send-Cmd "seekdemo 0"
            $global:measured = $true
            $global:seekHoldUntil = [Environment]::TickCount + 500
        } elseif ($sync.connected -and ([Environment]::TickCount - $global:measureTick) -gt 1200) {
            Send-Cmd "seekdemo 86400000"   # 24 h: forces a read to EOF so total is set
            $global:measureTick = [Environment]::TickCount
        }
        return
    }

    if ($global:dragging) { return }
    # just after a seek, leave the slider where the user put it until the engine
    # catches up (avoids the brief bounce back to the stale reported position)
    if ([Environment]::TickCount -lt $global:seekHoldUntil) { return }
    # keep the slider length in sync with the measured total (Round so the demo's
    # last frame lands exactly on the end of the slider).
    if ($sync.total -gt 0) {
        $maxSec = [int][math]::Round($sync.total / 1000.0)
        if ($maxSec -gt 0 -and $scrub.Maximum -ne $maxSec) { $scrub.Maximum = $maxSec }
    }
    $sec = [int][math]::Round($sync.pos / 1000.0)
    if ($sec -lt $scrub.Minimum) { $sec = $scrub.Minimum }
    if ($sec -gt $scrub.Maximum) { $sec = $scrub.Maximum }
    $global:suppress = $true
    $scrub.Value = $sec
    $global:suppress = $false
    $lbl.Text = (& $fmt $sec) + " / " + (& $fmt $scrub.Maximum)
})
$timer.Start()

$form.Add_Shown({
    Fit-Stage    # size the aspect-correct stage before the engine embeds into it
    $hwnd = $stage.Handle.ToInt64()
    $args = @(
        "+set", "in_embedParent", "$hwnd",
        "+set", "in_controlPort", "$Port",
        "+set", "r_fullscreen", "0",
        "+set", "r_swapInterval", "$Vsync",
        "+set", "con_notifytime", "0",   # don't draw console notify lines over the demo
        "+set", "fs_basepath", "`"$Q3Path`"",
        "+set", "fs_game", "defrag"
    )
    if ($Demo -ne "") { $args += @("+demo", $Demo) }
    $exeDir = Split-Path -Parent $Exe
    Write-Host "Launching: $Exe $($args -join ' ')"
    $global:proc = Start-Process -FilePath $Exe -ArgumentList $args -WorkingDirectory $exeDir -PassThru
})

$form.Add_FormClosed({
    $sync.stop = $true
    $timer.Stop()
    try { if ($sync.writer) { $sync.writer.Close() } } catch {}
    try { if ($global:reader_ps) { $global:reader_ps.Dispose() } } catch {}
    try { if ($global:reader_rs) { $global:reader_rs.Dispose() } } catch {}
    if ($global:proc -and -not $global:proc.HasExited) { try { $global:proc.Kill() } catch {} }
})

[System.Windows.Forms.Application]::Run($form)
