# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
<#
.SYNOPSIS
NoDrive Windows install and smoke test (Windows lane). CI only.

.DESCRIPTION
Runs in .github/workflows/windows.yml (job "install-test") on a fresh GitHub
windows-2022 runner. Never run it on a developer PC: it installs the MSI,
writes HKLM and HKCU, sets Image File Execution Options for web browsers and
restarts Explorer.

  powershell -File tools/screenshots/windows.ps1 -Msi <NoDrive-*.msi> -Out <dir> [-Root <repo>]

What it does, in order:
  1. MSI tables: product name, vendor, links, shortcuts and every string in the
     MSI (also in the embedded installer languages) carry no Nextcloud name,
     link or colour. Screenshot of the installer's first page (en, de), then
     the installer is cancelled before it changes anything.
  2. Silent install (msiexec /i /qn /l*v). Checks: install folder, Start-menu
     and desktop shortcuts, "Apps & features" entry, registry keys
     (HKLM/HKCU Software\Nourax\NoDrive), Explorer integration (context-menu
     handler and overlay names, the new CLSIDs; none of Nextcloud's),
     the nc:// handler, the file version resources of every .exe/.dll, the
     app icon, nodrivecmd --version/--help.
  3. Starts the app with a fresh config in four variants: en-light, de-light,
     ar-light (Arabic, right to left) and en-dark (Windows dark mode). Per
     variant two runs, like the Linux test:
        screens: 01 wizard, 02 tray menu, 03 proxy settings dialog
        login:   04 after clicking "Log in". A fake browser (Image File
                 Execution Options "Debugger" for the browsers) records the
                 URL the app hands to Windows; no browser is started.
     Per run: the default config folder %APPDATA%\NoDrive, the Explorer
     context-menu texts the app serves on its named pipe (GET_STRINGS), and
     every text the window exposes to UI Automation (no "Nextcloud").
  4. Network: the Windows DNS client log (Microsoft-Windows-DNS-Client/
     Operational) for the whole test, the app's TCP connections and the URLs
     in the app logs. Fails when a Nextcloud host is looked up or contacted.
  5. Silent uninstall (msiexec /x /qn) and checks that files, shortcuts,
     registry keys and Explorer integration are gone.
  6. Administrative image (msiexec /a) for the packaged-files sweep
     (tools/check-branding.py --artifact, run by the workflow).

Output: <Out>\screens\<variant>\NN-<screen>.png, <Out>\screens\installer\,
<Out>\logs\, <Out>\net\, <Out>\results.tsv (check, PASS|FAIL|INFO, evidence),
<Out>\summary.md and <Out>\admin-image\ (for the artifact sweep).
Exit code 1 when any check fails.

Written for Windows PowerShell 5.1 (shell: powershell in the workflow).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Msi,
    [Parameter(Mandatory = $true)][string] $Out,
    [string] $Root = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Msi = (Resolve-Path -LiteralPath $Msi).Path
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path
$Root = (Resolve-Path -LiteralPath $Root).Path
foreach ($d in 'screens', 'logs', 'net') { New-Item -ItemType Directory -Force -Path (Join-Path $Out $d) | Out-Null }
$Work = Join-Path $env:RUNNER_TEMP 'nodrive-smoke'
if (-not $env:RUNNER_TEMP) { $Work = Join-Path $env:TEMP 'nodrive-smoke' }
New-Item -ItemType Directory -Force -Path $Work | Out-Null

$Utf8 = New-Object System.Text.UTF8Encoding($false)
$ResultsFile = Join-Path $Out 'results.tsv'
[IO.File]::WriteAllText($ResultsFile, '', $Utf8)

$Ids = Get-Content -LiteralPath (Join-Path $Root 'tools\branding\ids.json') -Raw | ConvertFrom-Json
$AppName = $Ids.product.name                     # NoDrive
$Vendor = $Ids.product.vendor                    # Nourax
$Server = $Ids.product.server_url                # https://drive.nourax.org
$ExeName = $Ids.cmake.APPLICATION_EXECUTABLE     # nodrive
$Scheme = $Ids.cmake.APPLICATION_URI_HANDLER_SCHEME
$Primary = $Ids.colors.primary                   # #2563EB
$Win = $Ids.windows
$InstallDir = Join-Path $env:ProgramFiles $AppName
$Exe = Join-Path $InstallDir "$ExeName.exe"
$ForbiddenText = '(?i)nextcloud|next cloud|0082c9|00679e'
$ForbiddenHost = '(?i)(^|\.)nextcloud(\.|$)'
# Nextcloud's own Windows ids (NEXTCLOUD.cmake / CMakeLists.txt at v35.0.0): none may be registered.
$UpstreamGuids = @('BC6988AB-ACE2-4B81-84DC-DC34F9B24401', 'E0342B74-7593-4C70-9D61-22F294AAFE05',
    'E1094E94-BE93-4EA2-9639-8475C68F3886', 'E243AD85-F71B-496B-B17E-B8091CBE93D2',
    'E3D6DB20-1D83-4829-B5C9-941B31C0C35A', 'E4977F33-F93A-4A0A-9D3C-83DEA0EE8483',
    'E314A650-DCA4-416E-974E-18EA37C213EA', '1E62D59A-6EA4-476C-B707-4A32E88ED822',
    '6FF9B5B6-389F-444A-9FDD-A286C36EA079')

function Log([string] $msg) { Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $msg) }

function Result([string] $check, [string] $status, [string] $evidence) {
    $evidence = ($evidence -replace '[\r\n\t]+', ' ').Trim()
    [IO.File]::AppendAllText($ResultsFile, "$check`t$status`t$evidence`n", $Utf8)
    Write-Host "RESULT $check $status $evidence"
}

function Step([string] $check, [scriptblock] $body) {
    try { & $body } catch { Result $check 'FAIL' ("exception: " + $_.Exception.Message + " at line " + $_.InvocationInfo.ScriptLineNumber) }
}

function Save-Text([string] $rel, $lines) {
    $p = Join-Path $Out $rel
    [IO.File]::WriteAllText($p, (($lines | ForEach-Object { "$_" }) -join "`r`n") + "`r`n", $Utf8)
}

# ---------------------------------------------------------------------------
# Native helpers (screen capture, windows, mouse, pixels, tray icon position)
# ---------------------------------------------------------------------------
Add-Type -AssemblyName System.Drawing, System.Windows.Forms, UIAutomationClient, UIAutomationTypes
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public class NdWindow {
    public IntPtr Handle; public uint Pid; public string Title; public string Class; public bool Visible;
    public int X, Y, W, H;      // frame (DWM extended frame bounds), screen coordinates
    public int CX, CY, CW, CH;  // client area, screen coordinates
    public override string ToString() {
        return String.Format("0x{0:X} pid {1} visible {2} '{3}' [{4}] frame {5},{6} {7}x{8} client {9},{10} {11}x{12}",
            Handle.ToInt64(), Pid, Visible, Title, Class, X, Y, W, H, CX, CY, CW, CH);
    }
}

public static class NdWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct NOTIFYICONIDENTIFIER { public uint cbSize; public IntPtr hWnd; public uint uID; public Guid guidItem; }
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int size);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("shell32.dll")] static extern int Shell_NotifyIconGetRect(ref NOTIFYICONIDENTIFIER id, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] static extern int GetSystemMetrics(int i);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, UIntPtr w, string l, uint flags, uint timeout, out UIntPtr result);

    public static NdWindow Describe(IntPtr h) {
        NdWindow w = new NdWindow();
        w.Handle = h;
        uint pid; GetWindowThreadProcessId(h, out pid); w.Pid = pid;
        w.Visible = IsWindowVisible(h);
        StringBuilder sb = new StringBuilder(512); GetWindowText(h, sb, sb.Capacity); w.Title = sb.ToString();
        StringBuilder cb = new StringBuilder(256); GetClassName(h, cb, cb.Capacity); w.Class = cb.ToString();
        RECT r;
        if (DwmGetWindowAttribute(h, 9, out r, Marshal.SizeOf(typeof(RECT))) != 0) GetWindowRect(h, out r);
        w.X = r.Left; w.Y = r.Top; w.W = r.Right - r.Left; w.H = r.Bottom - r.Top;
        RECT c; GetClientRect(h, out c);
        POINT p = new POINT(); ClientToScreen(h, ref p);
        w.CX = p.X; w.CY = p.Y; w.CW = c.Right; w.CH = c.Bottom;
        return w;
    }

    public static List<NdWindow> TopWindows(bool includeHidden) {
        List<NdWindow> list = new List<NdWindow>();
        EnumWindows(delegate (IntPtr h, IntPtr l) {
            if (includeHidden || IsWindowVisible(h)) list.Add(Describe(h));
            return true;
        }, IntPtr.Zero);
        return list;
    }

    public static List<NdWindow> MessageWindows(uint pid) {
        List<NdWindow> list = new List<NdWindow>();
        IntPtr h = IntPtr.Zero;
        while (true) {
            h = FindWindowEx(new IntPtr(-3), h, null, null);   // HWND_MESSAGE: message-only windows
            if (h == IntPtr.Zero) break;
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid) list.Add(Describe(h));
        }
        return list;
    }

    // Position of the process's notification-area icon: x, y, w, h, uID (null when not found).
    public static int[] TrayIconRect(uint pid) {
        List<NdWindow> candidates = MessageWindows(pid);
        foreach (NdWindow w in TopWindows(true)) if (w.Pid == pid) candidates.Add(w);
        foreach (NdWindow w in candidates) {
            for (uint id = 0; id < 4; id++) {
                NOTIFYICONIDENTIFIER nid = new NOTIFYICONIDENTIFIER();
                nid.cbSize = (uint)Marshal.SizeOf(typeof(NOTIFYICONIDENTIFIER));
                nid.hWnd = w.Handle; nid.uID = id; nid.guidItem = Guid.Empty;
                RECT r;
                if (Shell_NotifyIconGetRect(ref nid, out r) == 0 && r.Right > r.Left)
                    return new int[] { r.Left, r.Top, r.Right - r.Left, r.Bottom - r.Top, (int)id };
            }
        }
        return null;
    }

    public static void Foreground(IntPtr h) {
        keybd_event(0x12, 0, 0, UIntPtr.Zero);   // Alt: lets this process move the foreground
        keybd_event(0x12, 0, 2, UIntPtr.Zero);
        ShowWindow(h, 5);
        BringWindowToTop(h);
        SetForegroundWindow(h);
    }

    public static void Key(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); keybd_event(vk, 0, 2, UIntPtr.Zero); }

    public static void Move(int x, int y) { SetCursorPos(x, y); }

    public static void Click(int x, int y, bool right) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(300);
        mouse_event(right ? 0x0008u : 0x0002u, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(80);
        mouse_event(right ? 0x0010u : 0x0004u, 0, 0, 0, UIntPtr.Zero);
    }

    public static void BroadcastSettingChange(string area) {
        UIntPtr res;
        SendMessageTimeout(new IntPtr(0xffff), 0x001A, UIntPtr.Zero, area, 2, 5000, out res);
    }

    public static int[] ScreenBounds() {
        return new int[] { GetSystemMetrics(76), GetSystemMetrics(77), GetSystemMetrics(78), GetSystemMetrics(79) };
    }

    public static void Capture(string path) {
        int[] b = ScreenBounds();
        using (Bitmap bmp = new Bitmap(b[2], b[3], PixelFormat.Format24bppRgb))
        using (Graphics g = Graphics.FromImage(bmp)) {
            g.CopyFromScreen(b[0], b[1], 0, 0, new Size(b[2], b[3]), CopyPixelOperation.SourceCopy);
            bmp.Save(path, ImageFormat.Png);
        }
    }

    // ---- pixels (BGR byte order in the buffer) ----
    static byte[] Load(string path, out int w, out int h, out int stride) {
        using (Bitmap src = new Bitmap(path)) {
            w = src.Width; h = src.Height;
            BitmapData d = src.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            stride = d.Stride;
            byte[] buf = new byte[stride * h];
            Marshal.Copy(d.Scan0, buf, 0, buf.Length);
            src.UnlockBits(d);
            return buf;
        }
    }

    public static int[] Pixel(string path, int x, int y) {
        int w, h, s; byte[] b = Load(path, out w, out h, out s);
        x = Math.Max(0, Math.Min(w - 1, x)); y = Math.Max(0, Math.Min(h - 1, y));
        int i = y * s + x * 3;
        return new int[] { b[i + 2], b[i + 1], b[i] };
    }

    // Largest 4-connected area in colour (r,g,b) +-tol inside the rectangle:
    // cx, cy, area, bx, by, bw, bh in screen-image coordinates, or null.
    public static int[] FindColour(string path, int rx, int ry, int rw, int rh, int r, int g, int bl, int tol) {
        int w, h, s; byte[] b = Load(path, out w, out h, out s);
        int x0 = Math.Max(0, rx), y0 = Math.Max(0, ry), x1 = Math.Min(w, rx + rw), y1 = Math.Min(h, ry + rh);
        int cw = x1 - x0, ch = y1 - y0;
        if (cw <= 0 || ch <= 0) return null;
        bool[] m = new bool[cw * ch];
        for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
            int i = (y + y0) * s + (x + x0) * 3;
            m[y * cw + x] = Math.Abs(b[i + 2] - r) <= tol && Math.Abs(b[i + 1] - g) <= tol && Math.Abs(b[i] - bl) <= tol;
        }
        bool[] seen = new bool[cw * ch];
        int[] best = null; int bestN = 0;
        Queue<int> q = new Queue<int>();
        for (int start = 0; start < m.Length; start++) {
            if (!m[start] || seen[start]) continue;
            int n = 0, minx = int.MaxValue, miny = int.MaxValue, maxx = -1, maxy = -1;
            seen[start] = true; q.Enqueue(start);
            while (q.Count > 0) {
                int p = q.Dequeue(); n++;
                int px = p % cw, py = p / cw;
                minx = Math.Min(minx, px); maxx = Math.Max(maxx, px); miny = Math.Min(miny, py); maxy = Math.Max(maxy, py);
                if (px > 0 && m[p - 1] && !seen[p - 1]) { seen[p - 1] = true; q.Enqueue(p - 1); }
                if (px < cw - 1 && m[p + 1] && !seen[p + 1]) { seen[p + 1] = true; q.Enqueue(p + 1); }
                if (py > 0 && m[p - cw] && !seen[p - cw]) { seen[p - cw] = true; q.Enqueue(p - cw); }
                if (py < ch - 1 && m[p + cw] && !seen[p + cw]) { seen[p + cw] = true; q.Enqueue(p + cw); }
            }
            if (n > bestN) {
                bestN = n;
                best = new int[] { x0 + (minx + maxx) / 2, y0 + (miny + maxy) / 2, n, x0 + minx, y0 + miny, maxx - minx + 1, maxy - miny + 1 };
            }
        }
        return best;
    }

    // Bounding box of the pixels that differ (sum of |dRGB| > thresh) from the
    // colour at the rectangle's top-left corner: cx, cy, count, bx, by, bw, bh, or null.
    public static int[] Content(string path, int rx, int ry, int rw, int rh, int thresh) {
        int w, h, s; byte[] b = Load(path, out w, out h, out s);
        int x0 = Math.Max(0, rx), y0 = Math.Max(0, ry), x1 = Math.Min(w, rx + rw), y1 = Math.Min(h, ry + rh);
        if (x1 <= x0 || y1 <= y0) return null;
        int bi = y0 * s + x0 * 3;
        int br = b[bi + 2], bg = b[bi + 1], bb = b[bi];
        int n = 0, minx = int.MaxValue, miny = int.MaxValue, maxx = -1, maxy = -1;
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
            int i = y * s + x * 3;
            if (Math.Abs(b[i + 2] - br) + Math.Abs(b[i + 1] - bg) + Math.Abs(b[i] - bb) > thresh) {
                n++; minx = Math.Min(minx, x); maxx = Math.Max(maxx, x); miny = Math.Min(miny, y); maxy = Math.Max(maxy, y);
            }
        }
        if (n == 0) return null;
        return new int[] { (minx + maxx) / 2, (miny + maxy) / 2, n, minx, miny, maxx - minx + 1, maxy - miny + 1 };
    }

    // Compares two images (same size) on the pixels that are opaque in the second one.
    // Returns the number of opaque pixels and how many differ by more than tol in a channel.
    public static int[] CompareOpaque(Bitmap a, Bitmap b, int tol) {
        if (a.Width != b.Width || a.Height != b.Height) return new int[] { -1, -1 };
        int n = 0, bad = 0;
        for (int y = 0; y < b.Height; y++) for (int x = 0; x < b.Width; x++) {
            Color cb = b.GetPixel(x, y);
            if (cb.A < 250) continue;
            Color ca = a.GetPixel(x, y);
            n++;
            if (Math.Abs(ca.R - cb.R) > tol || Math.Abs(ca.G - cb.G) > tol || Math.Abs(ca.B - cb.B) > tol || ca.A < 200) bad++;
        }
        return new int[] { n, bad };
    }
}
'@
[NdWin]::SetProcessDPIAware() | Out-Null

function Hex2Rgb([string] $hex) { $h = $hex.TrimStart('#'); return @([Convert]::ToInt32($h.Substring(0, 2), 16), [Convert]::ToInt32($h.Substring(2, 2), 16), [Convert]::ToInt32($h.Substring(4, 2), 16)) }
$PrimaryRgb = Hex2Rgb $Primary

function Shot([string] $variant, [string] $name) {
    $dir = Join-Path $Out "screens\$variant"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $p = Join-Path $dir "$name.png"
    [NdWin]::Capture($p)
    Log "screenshot $variant\$name.png"
    return $p
}

function App-Windows([int] $procId) {
    return @([NdWin]::TopWindows($false) | Where-Object { $_.Pid -eq $procId })
}

function Wait-Wizard([int] $procId, [int] $seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        $w = App-Windows $procId | Where-Object { $_.W -ge 300 -and $_.H -ge 200 } | Sort-Object { $_.W * $_.H } -Descending | Select-Object -First 1
        if ($w) { return $w }
        Start-Sleep -Milliseconds 500
    }
    return $null
}

# Every text the app's windows expose to UI Automation (window titles, labels,
# buttons, menu items). Returns objects; also writes them to logs\uia-<tag>.txt.
function Uia-Dump([int] $procId, [string] $tag) {
    $items = New-Object System.Collections.Generic.List[object]
    try {
        $root = [System.Windows.Automation.AutomationElement]::RootElement
        $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $procId)
        $tops = $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)
        foreach ($top in $tops) {
            $all = $top.FindAll([System.Windows.Automation.TreeScope]::Subtree, [System.Windows.Automation.Condition]::TrueCondition)
            foreach ($e in $all) {
                try {
                    $c = $e.Current
                    $r = $c.BoundingRectangle
                    $rect = if ($r.IsEmpty -or [double]::IsInfinity($r.X)) { $null } else { @([int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height) }
                    $type = ([string]$c.ControlType.ProgrammaticName).Replace('ControlType.', '')
                    $item = New-Object psobject -Property @{ Name = [string]$c.Name; Type = $type; Class = [string]$c.ClassName; Rect = $rect }
                    $items.Add($item)
                } catch { }
            }
        }
    } catch { $items.Add([pscustomobject]@{ Name = "UIA error: $($_.Exception.Message)"; Type = 'Error'; Class = ''; Rect = $null }) }
    Save-Text "logs\uia-$tag.txt" ($items | ForEach-Object { "{0}`t{1}`t{2}`t{3}" -f $_.Type, $_.Name, $_.Class, ($(if ($_.Rect) { $_.Rect -join ',' } else { '' })) })
    return $items
}

function Check-Texts([string] $check, $items) {
    $names = @($items | Where-Object { $_.Name } | ForEach-Object { $_.Name })
    $bad = @($names | Where-Object { $_ -match $ForbiddenText })
    if ($bad.Count -gt 0) { Result $check 'FAIL' ("Nextcloud in UI text: " + (($bad | Select-Object -Unique) -join ' | ')) }
    else {
        $sample = ($names | Where-Object { $_.Trim() } | Select-Object -Unique | Select-Object -First 12) -join ' | '
        Result $check 'PASS' ("$($names.Count) UI Automation texts, none names Nextcloud: $sample")
    }
}

function Reg-Default([string] $path) {
    try { return (Get-Item -LiteralPath $path -ErrorAction Stop).GetValue('') } catch { return $null }
}

function Reg-Value([string] $path, [string] $name) {
    try { return (Get-Item -LiteralPath $path -ErrorAction Stop).GetValue($name) } catch { return $null }
}

function Redact([string] $path) {
    if (Test-Path -LiteralPath $path) {
        $t = [IO.File]::ReadAllText($path)
        $t = [regex]::Replace($t, '[A-Za-z0-9]{64,}', '<redacted>')
        [IO.File]::WriteAllText($path, $t, $Utf8)
    }
}

function Stop-NoDrive([System.Diagnostics.Process] $proc) {
    if ($proc -and -not $proc.HasExited -and (Test-Path -LiteralPath $Exe)) {
        try {
            $q = Start-Process -FilePath $Exe -ArgumentList '--quit' -PassThru
            [void]$q.WaitForExit(20000)
        } catch { }
        if (-not $proc.WaitForExit(20000)) { try { $proc.Kill() } catch { } }
    }
    Get-Process -Name $ExeName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

function Msi-Exec([string[]] $arguments, [string] $what) {
    $p = Start-Process -FilePath msiexec.exe -ArgumentList $arguments -Wait -PassThru
    Log "msiexec $what -> exit $($p.ExitCode)"
    return $p.ExitCode
}

# ---- Windows Installer COM (late bound) ----
function Com-Call($obj, [string] $name, [object[]] $arguments) { return $obj.GetType().InvokeMember($name, [Reflection.BindingFlags]::InvokeMethod, $null, $obj, $arguments) }
function Com-Get($obj, [string] $name, [object[]] $arguments) { return $obj.GetType().InvokeMember($name, [Reflection.BindingFlags]::GetProperty, $null, $obj, $arguments) }

function Msi-Rows($db, [string] $sql) {
    $rows = New-Object System.Collections.Generic.List[object]
    $view = Com-Call $db 'OpenView' @($sql)
    [void](Com-Call $view 'Execute' $null)
    while ($true) {
        $rec = Com-Call $view 'Fetch' $null
        if ($null -eq $rec) { break }
        $n = Com-Get $rec 'FieldCount' $null
        $vals = @()
        for ($i = 1; $i -le $n; $i++) { try { $vals += [string](Com-Get $rec 'StringData' @($i)) } catch { $vals += '<stream>' } }
        $rows.Add($vals)
    }
    [void](Com-Call $view 'Close' $null)
    return , $rows
}

function Msi-Scan($db, [string] $label) {
    $hits = New-Object System.Collections.Generic.List[string]
    $count = 0
    foreach ($t in (Msi-Rows $db 'SELECT `Name` FROM `_Tables`')) {
        $table = $t[0]
        if ($table -in @('Binary', 'Icon', 'MsiDigitalSignature', 'MsiPatchHeaders', 'MsiFileHash')) { continue }
        foreach ($row in (Msi-Rows $db "SELECT * FROM ``$table``")) {
            $count++
            $line = $row -join ' | '
            if ($line -match $ForbiddenText) { $hits.Add("[$label] $table`: $line") }
        }
    }
    return @{ Rows = $count; Hits = $hits }
}

# ===========================================================================
Log "NoDrive Windows smoke test: $Msi"
$screen = [NdWin]::ScreenBounds()
try { Set-DisplayResolution -Width 1280 -Height 800 -Force -ErrorAction Stop; Start-Sleep -Seconds 3; $screen = [NdWin]::ScreenBounds() } catch { Log "display resolution unchanged: $($_.Exception.Message)" }
Result 'runner-screen' 'INFO' ("screen {0}x{1}; {2}; user {3}; session {4}" -f $screen[2], $screen[3], (Get-CimInstance Win32_OperatingSystem).Caption, $env:USERNAME, (Get-Process -Id $PID).SessionId)

# DNS client log for the whole test.
& wevtutil.exe sl Microsoft-Windows-DNS-Client/Operational /e:true /ms:104857600 | Out-Null
& wevtutil.exe cl Microsoft-Windows-DNS-Client/Operational | Out-Null
$TestStart = Get-Date
$NetPids = New-Object System.Collections.Generic.List[int]

# ---------------------------------------------------------------------------
# 1. The MSI itself
# ---------------------------------------------------------------------------
Step 'msi-properties' {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $db = Com-Call $installer 'OpenDatabase' @($Msi, 0)
    $props = @{}
    foreach ($r in (Msi-Rows $db 'SELECT `Property`, `Value` FROM `Property`')) { $props[$r[0]] = $r[1] }
    $shortcuts = @()
    foreach ($r in (Msi-Rows $db 'SELECT `Name`, `Directory_` FROM `Shortcut`')) { $shortcuts += "$($r[0]) in $($r[1])" }
    $info = "ProductName=$($props['ProductName']); Manufacturer=$($props['Manufacturer']); ProductVersion=$($props['ProductVersion']); UpgradeCode=$($props['UpgradeCode']); ARPHELPLINK=$($props['ARPHELPLINK']); ARPURLINFOABOUT=$($props['ARPURLINFOABOUT']); shortcuts: $($shortcuts -join ', ')"
    $ok = ($props['ProductName'] -eq $AppName) -and ($props['Manufacturer'] -eq $Vendor) -and
        ($props['UpgradeCode'] -eq "{$($Win.WIN_MSI_UPGRADE_CODE)}") -and ($props['ARPHELPLINK'] -like "$Server*") -and
        (@($shortcuts | Where-Object { $_ -notlike "$AppName in *" }).Count -eq 0)
    Result 'msi-properties' $(if ($ok) { 'PASS' } else { 'FAIL' }) $info
    $scan = Msi-Scan $db 'en-us'
    $allHits = New-Object System.Collections.Generic.List[string]
    $allHits.AddRange($scan.Hits)
    $langs = @()
    foreach ($s in (Msi-Rows $db 'SELECT `Name` FROM `_Storages`')) {
        $lang = $s[0]
        $tdb = Com-Call $installer 'OpenDatabase' @($Msi, 0)
        try {
            [void](Com-Call $tdb 'ApplyTransform' @(":$lang", 0))
            $ts = Msi-Scan $tdb $lang
            $allHits.AddRange($ts.Hits)
            $langs += $lang
        } catch { Log "transform $lang`: $($_.Exception.Message)" }
    }
    Save-Text 'logs\msi-strings-hits.txt' $allHits
    if ($allHits.Count -eq 0) { Result 'msi-strings' 'PASS' ("$($scan.Rows) MSI table rows (en-us) and the embedded languages $($langs -join ', '): no Nextcloud name, link or colour") }
    else { Result 'msi-strings' 'FAIL' ("$($allHits.Count) MSI rows name Nextcloud (logs\msi-strings-hits.txt): " + (($allHits | Select-Object -First 3) -join ' || ')) }
}

# Installer UI (first page), English and German, then cancel before anything is changed.
foreach ($ui in @(@{ Tag = 'en'; Args = @() }, @{ Tag = 'de'; Args = @('TRANSFORMS=:1031') })) {
    Step "installer-ui-$($ui.Tag)" {
        $argsUi = @('/i', "`"$Msi`"") + $ui.Args + @('/l*v', "`"$(Join-Path $Out "logs\installer-ui-$($ui.Tag).log")`"")
        $p = Start-Process -FilePath msiexec.exe -ArgumentList $argsUi -PassThru
        $w = $null
        $deadline = (Get-Date).AddSeconds(90)
        while ((Get-Date) -lt $deadline -and -not $w) {
            $w = [NdWin]::TopWindows($false) | Where-Object { $_.Title -like "*$AppName*" -and $_.W -ge 300 } | Select-Object -First 1
            if (-not $w) { Start-Sleep -Milliseconds 500 }
        }
        if ($w) {
            [NdWin]::Foreground($w.Handle); [NdWin]::Move(1, 1); Start-Sleep -Seconds 3
            Shot 'installer' "01-welcome-$($ui.Tag)" | Out-Null
            $texts = Uia-Dump ([int]$w.Pid) "installer-$($ui.Tag)"
            $bad = @($texts | Where-Object { $_.Name -match $ForbiddenText })
            Result "installer-ui-$($ui.Tag)" $(if ($bad.Count -eq 0) { 'PASS' } else { 'FAIL' }) ("window '$($w.Title)'; texts: " + (($texts | Where-Object { $_.Name } | ForEach-Object { $_.Name } | Select-Object -Unique -First 8) -join ' | '))
        } else {
            Result "installer-ui-$($ui.Tag)" 'FAIL' 'no installer window within 90 s'
        }
        Get-Process msiexec -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Stop-Process -Force -ErrorAction SilentlyContinue
        try { $p.Kill() } catch { }
        Start-Sleep -Seconds 3
    }
}

# ---------------------------------------------------------------------------
# 2. Silent install
# ---------------------------------------------------------------------------
$before = @{
    NextcloudHKLM = (Test-Path 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Nextcloud GmbH') -or (Test-Path 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Nextcloud GmbH')
}
# 1618 = another installation is still finishing (the cancelled installer UI above).
for ($try = 1; $try -le 5; $try++) {
    $code = Msi-Exec @('/i', "`"$Msi`"", '/qn', '/norestart', '/l*v', "`"$(Join-Path $Out 'logs\install.log')`"") 'install'
    if ($code -ne 1618) { break }
    Start-Sleep -Seconds 15
}
if ($code -eq 0 -or $code -eq 3010) { Result 'install' 'PASS' "msiexec /i /qn exit $code (logs\install.log)" }
else { Result 'install' 'FAIL' "msiexec /i /qn exit $code (logs\install.log)"; }

Step 'install-folder' {
    $files = @(Get-ChildItem -LiteralPath $InstallDir -Recurse -File -ErrorAction Stop)
    Save-Text 'logs\install-tree.txt' ($files | ForEach-Object { $_.FullName.Substring($InstallDir.Length + 1) + "`t" + $_.Length })
    $nc = @(Get-ChildItem -LiteralPath $env:ProgramFiles, ${env:ProgramFiles(x86)} -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '(?i)nextcloud' })
    $badNames = @($files | Where-Object { $_.Name -match '(?i)nextcloud' })
    $ok = (Test-Path -LiteralPath $Exe) -and (Test-Path -LiteralPath (Join-Path $InstallDir "$($ExeName)cmd.exe")) -and $nc.Count -eq 0 -and $badNames.Count -eq 0
    Result 'install-folder' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("$InstallDir, $($files.Count) files, $ExeName.exe and $($ExeName)cmd.exe present; Nextcloud folders in Program Files: $($nc.Count); file names with Nextcloud: $($badNames.Count) $(($badNames | Select-Object -First 3 | ForEach-Object Name) -join ', ')")
    $lic = Join-Path $InstallDir 'licenses'
    $licFiles = @(Get-ChildItem -LiteralPath $lic -File -ErrorAction SilentlyContinue | ForEach-Object Name)
    $okLic = ($licFiles -contains 'COPYING.txt') -and ($licFiles -contains 'THIRD-PARTY-NOTICES.txt')
    Result 'licences' $(if ($okLic) { 'PASS' } else { 'FAIL' }) ("$lic`: $($licFiles -join ', ')")
}

Step 'registry-keys' {
    $k32 = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\' + "$Vendor\$AppName"
    $k64 = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\' + "$Vendor\$AppName"
    $kcu = 'Registry::HKEY_CURRENT_USER\Software\' + "$Vendor\$AppName"
    $dir = Reg-Default $k32
    $pc = Reg-Value $k32 'InstallerProductCode'
    $skip = Reg-Value $k64 'skipUpdateCheck'
    $sm = Reg-Value $kcu 'installedStartMenuShortcut'
    $ncKeys = @('Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Nextcloud GmbH', 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Nextcloud GmbH',
        'Registry::HKEY_CURRENT_USER\Software\Nextcloud GmbH') | Where-Object { Test-Path -LiteralPath $_ }
    $ok = ($dir -like "$InstallDir*") -and $pc -and ($null -ne $skip) -and ($sm -eq 1) -and (@($ncKeys).Count -eq 0)
    Result 'registry-keys' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("HKLM\SOFTWARE\WOW6432Node\$Vendor\$AppName = '$dir', InstallerProductCode $pc; HKLM\SOFTWARE\$Vendor\$AppName skipUpdateCheck=$skip; HKCU\Software\$Vendor\$AppName installedStartMenuShortcut=$sm; Nextcloud GmbH keys: $(@($ncKeys).Count) (before install: $($before.NextcloudHKLM))")
}

Step 'apps-and-features' {
    $entries = @(Get-ChildItem 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall' -ErrorAction SilentlyContinue |
        ForEach-Object { [pscustomobject]@{ Key = $_.PSChildName; Name = $_.GetValue('DisplayName'); Publisher = $_.GetValue('Publisher'); Version = $_.GetValue('DisplayVersion'); Help = $_.GetValue('HelpLink'); About = $_.GetValue('URLInfoAbout'); Icon = $_.GetValue('DisplayIcon') } })
    $ours = @($entries | Where-Object { $_.Name -eq $AppName })
    $nc = @($entries | Where-Object { "$($_.Name) $($_.Publisher)" -match '(?i)nextcloud' })
    $e = $ours | Select-Object -First 1
    $ok = $ours.Count -eq 1 -and $e.Publisher -eq $Vendor -and $e.Help -like "$Server*" -and $nc.Count -eq 0
    Result 'apps-and-features' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("'$($e.Name)' publisher '$($e.Publisher)' version $($e.Version) help $($e.Help) about $($e.About) ($($e.Key)); Nextcloud entries: $($nc.Count)")
}

Step 'start-menu' {
    $programs = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'
    $lnks = @(Get-ChildItem -LiteralPath $programs, (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs') -Recurse -Filter '*.lnk' -ErrorAction SilentlyContinue)
    $ours = @($lnks | Where-Object { $_.BaseName -eq $AppName })
    $nc = @($lnks | Where-Object { $_.BaseName -match '(?i)nextcloud' })
    $shell = New-Object -ComObject WScript.Shell
    $target = if ($ours.Count) { $shell.CreateShortcut($ours[0].FullName).TargetPath } else { '' }
    $desk = Join-Path $env:PUBLIC "Desktop\$AppName.lnk"
    $deskTarget = if (Test-Path -LiteralPath $desk) { $shell.CreateShortcut($desk).TargetPath } else { '' }
    $ok = $ours.Count -eq 1 -and $target -eq $Exe -and $deskTarget -eq $Exe -and $nc.Count -eq 0
    Result 'start-menu' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("Start menu '$($ours[0].FullName)' -> $target; desktop '$desk' -> $deskTarget; Nextcloud shortcuts: $($nc.Count)")
}

Step 'explorer-integration' {
    $guid = $Win.WIN_SHELLEXT_CONTEXT_MENU_GUID
    $cm = 'Registry::HKEY_CLASSES_ROOT\AllFileSystemObjects\shellex\ContextMenuHandlers'
    $handlers = @(Get-ChildItem -LiteralPath $cm -ErrorAction SilentlyContinue | ForEach-Object { [pscustomobject]@{ Name = $_.PSChildName; Value = $_.GetValue('') } })
    $ourCm = @($handlers | Where-Object { $_.Name -eq "$($AppName)ContextMenuHandler" -and $_.Value -eq $guid })
    $clsid = "Registry::HKEY_CLASSES_ROOT\CLSID\$guid"
    $cmDesc = Reg-Default $clsid
    $cmDll = Reg-Default "$clsid\InprocServer32"
    $ov = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers'
    $overlays = @(Get-ChildItem -LiteralPath $ov -ErrorAction SilentlyContinue | ForEach-Object { [pscustomobject]@{ Name = $_.PSChildName.Trim(); Value = $_.GetValue('') } })
    $want = [ordered]@{ "$($AppName)Error" = $Win.WIN_SHELLEXT_OVERLAY_GUID_ERROR; "$($AppName)OK" = $Win.WIN_SHELLEXT_OVERLAY_GUID_OK; "$($AppName)OKShared" = $Win.WIN_SHELLEXT_OVERLAY_GUID_OK_SHARED; "$($AppName)Sync" = $Win.WIN_SHELLEXT_OVERLAY_GUID_SYNC; "$($AppName)Warning" = $Win.WIN_SHELLEXT_OVERLAY_GUID_WARNING }
    $missing = @()
    foreach ($k in $want.Keys) {
        if (@($overlays | Where-Object { $_.Name -eq $k -and $_.Value -eq $want[$k] }).Count -eq 0) { $missing += $k }
    }
    $ncNames = @(($handlers + $overlays) | Where-Object { $_.Name -match '(?i)nextcloud' } | ForEach-Object Name)
    $upstream = @($UpstreamGuids | Where-Object { (Test-Path -LiteralPath "Registry::HKEY_CLASSES_ROOT\CLSID\{$_}") -or (Test-Path -LiteralPath "Registry::HKEY_CLASSES_ROOT\AppID\{$_}") })
    Save-Text 'logs\explorer-integration.txt' (@("context menu handlers:") + ($handlers | ForEach-Object { "  $($_.Name) = $($_.Value)" }) + @("overlay identifiers:") + ($overlays | ForEach-Object { "  $($_.Name) = $($_.Value)" }))
    $ok = $ourCm.Count -eq 1 -and $cmDesc -eq "$AppName context menu handler" -and $cmDll -like "$InstallDir*" -and $missing.Count -eq 0 -and $ncNames.Count -eq 0 -and $upstream.Count -eq 0
    Result 'explorer-integration' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("context menu handler '$($AppName)ContextMenuHandler' -> $guid '$cmDesc' ($cmDll); overlays $(($want.Keys) -join ', ') registered, missing: $($missing -join ',' ); names with Nextcloud: $($ncNames.Count); Nextcloud CLSIDs/AppIDs present: $($upstream.Count)")
    $nc = Reg-Default "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\$Scheme"
    $cmd = Reg-Default "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\$Scheme\shell\open\command"
    Result 'url-handler' $(if ($nc -eq "URL:$AppName Protocol" -and $cmd -like "*$InstallDir\$ExeName.exe*") { 'PASS' } else { 'FAIL' }) ("$($Scheme):// = '$nc', command $cmd (scheme kept for the server's 'Open locally' links)")
}

Step 'version-resources' {
    $bins = @(Get-ChildItem -LiteralPath $InstallDir -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' })
    $rows = @()
    $bad = @()
    foreach ($b in $bins) {
        $v = $b.VersionInfo
        $text = "$($v.CompanyName) | $($v.ProductName) | $($v.FileDescription) | $($v.LegalCopyright) | $($v.Comments)"
        $rows += "$($b.Name)`t$text`t$($v.ProductVersion)"
        if ($text -match $ForbiddenText) { $bad += "$($b.Name): $text" }
    }
    Save-Text 'logs\version-resources.txt' $rows
    $main = (Get-Item -LiteralPath $Exe).VersionInfo
    $ok = $bad.Count -eq 0 -and $main.ProductName -eq $AppName -and $main.CompanyName -eq $Vendor
    Result 'version-resources' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("$ExeName.exe: product '$($main.ProductName)', company '$($main.CompanyName)', version $($main.ProductVersion); $($bins.Count) .exe/.dll checked, with Nextcloud: $($bad.Count) $($bad -join ' || ')")
}

Step 'app-icon' {
    $ico = [System.Drawing.Icon]::ExtractAssociatedIcon($Exe)
    $bmp = $ico.ToBitmap()
    $p = Join-Path $Out 'screens\app-icon-32.png'
    $bmp.Save($p, [System.Drawing.Imaging.ImageFormat]::Png)
    $ref = New-Object System.Drawing.Bitmap (Join-Path $Root "theme\colored\32-$($Ids.cmake.APPLICATION_ICON_NAME)-icon.png")
    $cmp = [NdWin]::CompareOpaque($bmp, $ref, 12)
    $ok = $cmp[0] -gt 100 -and $cmp[1] -le [math]::Ceiling($cmp[0] * 0.02)
    Result 'app-icon' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("icon of $ExeName.exe (32 px, screens\app-icon-32.png) vs theme\colored\32-NoDrive-icon.png from tools/make-icons.py: $($cmp[0]) opaque pixels, $($cmp[1]) differ")
}

Step 'cmd-version' {
    $ErrorActionPreference = 'Continue'   # stderr of a native command must not stop the step (PowerShell 5.1)
    $cmdExe = Join-Path $InstallDir "$($ExeName)cmd.exe"
    $ver = (& $cmdExe --version 2>&1 | Out-String)
    $help = (& $cmdExe --help 2>&1 | Out-String)
    Save-Text 'logs\cmd-version.txt' $ver
    Save-Text 'logs\cmd-help.txt' $help
    $ok = $ver -match $AppName -and $ver -notmatch $ForbiddenText -and $help -notmatch $ForbiddenText
    $first = ($ver -split "`n" | Where-Object { $_.Trim() } | Select-Object -First 1)
    Result 'cmd-version' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("$($ExeName)cmd --version: $first; --help names no Nextcloud: $($help -notmatch $ForbiddenText)")
}

# ---------------------------------------------------------------------------
# 3. App runs
# ---------------------------------------------------------------------------
# Fake browser: Windows starts this script instead of a browser (Image File
# Execution Options "Debugger"), with the browser's command line appended.
$FakeDir = 'C:\nodrive-fake-browser'
New-Item -ItemType Directory -Force -Path $FakeDir | Out-Null
$OpenedLog = Join-Path $FakeDir 'opened.txt'
$fake = Join-Path $FakeDir 'fakebrowser.ps1'
[IO.File]::WriteAllText($fake, @"
`$line = '{0:HH:mm:ss} {1}' -f (Get-Date), [Environment]::CommandLine
[IO.File]::AppendAllText('$OpenedLog', `$line + "``r``n")
"@, $Utf8)
$ifeo = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options'
foreach ($b in @('msedge.exe', 'chrome.exe', 'firefox.exe', 'iexplore.exe', 'MicrosoftEdge.exe', 'launchwinapp.exe', 'OpenWith.exe')) {
    New-Item -Path "$ifeo\$b" -Force | Out-Null
    Set-ItemProperty -Path "$ifeo\$b" -Name Debugger -Value "`"$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe`" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$fake`""
}
$handler = Reg-Value 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\https\UserChoice' 'ProgId'
Result 'browser-default' 'INFO' "default https handler: '$handler'; fake browser via Image File Execution Options for msedge, chrome, firefox, iexplore, OpenWith"

# Show every notification-area icon on the taskbar (no overflow), so the tray icon can be clicked.
Set-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer' -Name EnableAutoTray -Value 0 -Type DWord
Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 3
if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe; Start-Sleep -Seconds 5 }

# Arabic: Qt mirrors the layout when its own Arabic catalogue is loaded; else --reverse.
$qtAr = @(Get-ChildItem -LiteralPath $InstallDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -in 'qt_ar.qm', 'qtbase_ar.qm' } | ForEach-Object { $_.FullName.Substring($InstallDir.Length + 1) })
$arExtra = if ($qtAr | Where-Object { $_ -like 'i18n\*' }) { @() } else { @('--reverse') }
Result 'arabic-layout' 'INFO' ("Qt Arabic catalogues in the install: $(if ($qtAr) { $qtAr -join ', ' } else { 'none' }); extra arguments for ar: $(if ($arExtra) { $arExtra -join ' ' } else { 'none' })")

$Variants = @(
    @{ Name = 'en-light'; Lang = 'en'; Dark = $false; Extra = @() },
    @{ Name = 'de-light'; Lang = 'de'; Dark = $false; Extra = @() },
    @{ Name = 'ar-light'; Lang = 'ar'; Dark = $false; Extra = $arExtra },
    @{ Name = 'en-dark'; Lang = 'en'; Dark = $true; Extra = @() }
)
$Personalize = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
$ConfDir = Join-Path $env:APPDATA $AppName
$LocalDir = Join-Path $env:LOCALAPPDATA $AppName
$FirstRun = $true
$Status = 0

function Snapshot-Tcp([int] $procId, [string] $tag, [string] $when) {
    $c = @(Get-NetTCPConnection -OwningProcess $procId -ErrorAction SilentlyContinue | ForEach-Object { "$when`t$($_.State)`t$($_.LocalAddress):$($_.LocalPort)`t$($_.RemoteAddress):$($_.RemotePort)" })
    $f = Join-Path $Out "net\tcp-$tag.txt"
    [IO.File]::AppendAllText($f, (($c -join "`r`n") + "`r`n"), $Utf8)
}

function Pipe-Strings([string] $tag) {
    $name = "$ExeName-$env:USERNAME"
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $name, [System.IO.Pipes.PipeDirection]::InOut)
    $lines = New-Object System.Collections.Generic.List[string]
    try {
        $pipe.Connect(10000)
        $bytes = $Utf8.GetBytes("GET_STRINGS`n")
        $pipe.Write($bytes, 0, $bytes.Length); $pipe.Flush()
        $reader = New-Object System.IO.StreamReader($pipe, $Utf8)
        $deadline = (Get-Date).AddSeconds(15)
        while ((Get-Date) -lt $deadline) {
            $task = $reader.ReadLineAsync()
            if (-not $task.Wait(5000)) { break }
            $l = $task.Result
            if ($null -eq $l) { break }
            $lines.Add($l)
            if ($l -eq 'GET_STRINGS:END') { break }
        }
    } finally { $pipe.Dispose() }
    Save-Text "logs\pipe-strings-$tag.txt" $lines
    return $lines
}

foreach ($v in $Variants) {
    foreach ($run in @('screens', 'login')) {
        $tag = "$($v.Name)-$run"
        Step "run-$tag" {
            Stop-NoDrive $null
            Remove-Item -LiteralPath $ConfDir, $LocalDir -Recurse -Force -ErrorAction SilentlyContinue
            if (Test-Path -LiteralPath $OpenedLog) { Remove-Item -LiteralPath $OpenedLog -Force }
            Set-ItemProperty -Path $Personalize -Name AppsUseLightTheme -Value $(if ($v.Dark) { 0 } else { 1 }) -Type DWord
            Set-ItemProperty -Path $Personalize -Name SystemUsesLightTheme -Value $(if ($v.Dark) { 0 } else { 1 }) -Type DWord
            [NdWin]::BroadcastSettingChange('ImmersiveColorSet')
            $sl = Start-Process -FilePath $Exe -ArgumentList @('--set-language', $v.Lang) -PassThru
            if (-not $sl.WaitForExit(60000)) { $sl.Kill(); throw "--set-language $($v.Lang) did not exit" }

            $log = Join-Path $Out "logs\app-$tag.log"
            $argList = @('--logdebug', '--logflush', '--logfile', "`"$log`"") + $v.Extra
            Log "start $tag ($($v.Lang), dark=$($v.Dark) $($v.Extra -join ' '))"
            $proc = Start-Process -FilePath $Exe -ArgumentList $argList -PassThru
            $NetPids.Add($proc.Id)
            try {
                $w = Wait-Wizard $proc.Id 120
                if (-not $w) {
                    Save-Text "logs\windows-$tag.txt" ([NdWin]::TopWindows($true) | Where-Object { $_.Pid -eq $proc.Id })
                    Shot $v.Name "00-no-wizard-$run" | Out-Null
                    Result "launch-$tag" 'FAIL' "no $AppName window within 120 s (process exited: $($proc.HasExited))"
                    return
                }
                [NdWin]::Foreground($w.Handle)
                [NdWin]::Move(1, 1)
                Start-Sleep -Seconds 8
                $w = [NdWin]::Describe($w.Handle)
                Save-Text "logs\windows-$tag.txt" ([NdWin]::TopWindows($true) | Where-Object { $_.Pid -eq $proc.Id })
                Result "launch-$tag" 'PASS' ("window '$($w.Title)' $($w.W)x$($w.H) at $($w.X),$($w.Y)")
                Snapshot-Tcp $proc.Id $tag 'after start'
                $pre = Join-Path $Work "pre-$tag.png"
                [NdWin]::Capture($pre)

                $px = [NdWin]::Pixel($pre, $w.CX + 12, $w.CY + 12)
                $lum = [int](0.299 * $px[0] + 0.587 * $px[1] + 0.114 * $px[2])
                $bgHex = '#{0:X2}{1:X2}{2:X2}' -f $px[0], $px[1], $px[2]
                Result "background-$tag" 'INFO' "wizard background $bgHex (luminance $lum)"
                if ($v.Dark) {
                    Result 'dark-mode' $(if ($lum -lt 90) { 'PASS' } else { 'FAIL' }) "Windows apps set to dark (AppsUseLightTheme=0): wizard background $bgHex, luminance $lum"
                }
                $btn = [NdWin]::FindColour($pre, $w.CX, $w.CY, $w.CW, $w.CH, $PrimaryRgb[0], $PrimaryRgb[1], $PrimaryRgb[2], 6)
                if ($btn -and $btn[2] -ge 300) {
                    Result "colour-$tag" 'PASS' ("button in $Primary`: {0}x{1} px at {2},{3} ({4} px of $Primary)" -f $btn[5], $btn[6], $btn[3], $btn[4], $btn[2])
                } else {
                    Result "colour-$tag" 'FAIL' "no $Primary button in the wizard"
                    $btn = $null
                }
                if ($w.Title -match $ForbiddenText -or $w.Title -notmatch $AppName) { Result "title-$tag" 'FAIL' "window title '$($w.Title)'" }
                $texts = Uia-Dump $proc.Id "$tag-wizard"
                Check-Texts "texts-$tag-wizard" $texts

                if ($script:FirstRun) {
                    $script:FirstRun = $false
                    $cfg = Join-Path $ConfDir "$($Ids.cmake.APPLICATION_CONFIG_NAME).cfg"
                    $listing = @(Get-ChildItem -LiteralPath $ConfDir -Recurse -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
                    Save-Text 'logs\config-folder.txt' $listing
                    $ncDirs = @((Join-Path $env:APPDATA 'Nextcloud'), (Join-Path $env:LOCALAPPDATA 'Nextcloud')) | Where-Object { Test-Path -LiteralPath $_ }
                    $okCfg = (Test-Path -LiteralPath $cfg) -and @($ncDirs).Count -eq 0
                    Result 'config-folder' $(if ($okCfg) { 'PASS' } else { 'FAIL' }) ("$cfg exists: $(Test-Path -LiteralPath $cfg); $($listing.Count) entries in $ConfDir; Nextcloud folders in AppData: $(@($ncDirs).Count)")
                    $pipes = @([IO.Directory]::GetFiles('\\.\pipe\') | ForEach-Object { Split-Path -Leaf $_ } | Where-Object { $_ -match "(?i)^($ExeName|nextcloud)-" })
                    Result 'named-pipe' $(if ($pipes -contains "$ExeName-$env:USERNAME" -and -not ($pipes -match '(?i)^nextcloud')) { 'PASS' } else { 'FAIL' }) ("Explorer integration pipe(s): $($pipes -join ', ')")
                }

                if ($run -eq 'screens') {
                    Shot $v.Name '01-wizard' | Out-Null
                    $strings = @(Pipe-Strings $v.Name)
                    $menuTitle = ($strings | Where-Object { $_ -like 'STRING:CONTEXT_MENU_TITLE:*' } | Select-Object -First 1)
                    $badPipe = @($strings | Where-Object { $_ -match $ForbiddenText })
                    $okPipe = $menuTitle -eq "STRING:CONTEXT_MENU_TITLE:$AppName" -and $badPipe.Count -eq 0
                    Result "context-menu-$($v.Name)" $(if ($okPipe) { 'PASS' } else { 'FAIL' }) ("Explorer context-menu texts from the app (pipe $ExeName-<user>, GET_STRINGS): " + (($strings | Where-Object { $_ -like 'STRING:*' } | ForEach-Object { $_.Substring(7) }) -join ' | '))

                    $tray = [NdWin]::TrayIconRect([uint32]$proc.Id)
                    if ($tray) {
                        [NdWin]::Click($tray[0] + [int]($tray[2] / 2), $tray[1] + [int]($tray[3] / 2), $true)
                        Start-Sleep -Seconds 2
                        Shot $v.Name '02-tray-menu' | Out-Null
                        $menuTexts = Uia-Dump $proc.Id "$tag-tray-menu"
                        Check-Texts "texts-$tag-tray-menu" $menuTexts
                        [NdWin]::Key(0x1B)
                        Start-Sleep -Seconds 1
                    } else {
                        Save-Text "logs\tray-$tag.txt" (@([NdWin]::MessageWindows([uint32]$proc.Id)) + @([NdWin]::TopWindows($true) | Where-Object { $_.Pid -eq $proc.Id }))
                        Result "tray-$tag" 'FAIL' 'notification-area icon not found (logs\tray-*.txt)'
                    }

                    # Proxy settings: the last button of the wizard (footer), else the content of the footer band.
                    [NdWin]::Foreground($w.Handle); [NdWin]::Move(1, 1); Start-Sleep -Milliseconds 800
                    $buttons = @($texts | Where-Object { $_.Type -eq 'Button' -and $_.Rect -and $_.Rect[1] -ge $w.CY -and $_.Rect[1] -lt ($w.CY + $w.CH) -and $_.Rect[2] -gt 0 })
                    $footer = $buttons | Sort-Object { $_.Rect[1] } -Descending | Select-Object -First 1
                    $target = $null
                    if ($footer -and $footer.Rect[1] -gt ($w.CY + $w.CH * 0.6)) {
                        $target = @(($footer.Rect[0] + [int]($footer.Rect[2] / 2)), ($footer.Rect[1] + [int]($footer.Rect[3] / 2)))
                        $how = "UI Automation button '$($footer.Name)'"
                    } else {
                        $c = [NdWin]::Content($pre, $w.CX + 6, $w.CY + $w.CH - 90, $w.CW - 12, 84, 60)
                        if ($c) { $target = @($c[0], $c[1]); $how = 'footer pixels' }
                    }
                    if ($target) {
                        [NdWin]::Click($target[0], $target[1], $false)
                        Start-Sleep -Seconds 3
                        Shot $v.Name '03-proxy-settings' | Out-Null
                        $proxyTexts = Uia-Dump $proc.Id "$tag-proxy"
                        Check-Texts "texts-$tag-proxy" $proxyTexts
                        Result "proxy-$tag" 'INFO' "clicked $how at $($target -join ',')"
                    } else {
                        Result "proxy-$tag" 'FAIL' 'proxy settings button not found'
                    }
                } else {
                    $target = $null
                    if ($btn) { $target = @($btn[0], $btn[1]) }
                    else {
                        $first = @($texts | Where-Object { $_.Type -eq 'Button' -and $_.Rect -and $_.Rect[1] -ge $w.CY -and $_.Rect[2] -gt 0 }) | Sort-Object { $_.Rect[1] } | Select-Object -First 1
                        if ($first) { $target = @(($first.Rect[0] + [int]($first.Rect[2] / 2)), ($first.Rect[1] + [int]($first.Rect[3] / 2))) }
                    }
                    if (-not $target) { Result "browser-$tag" 'FAIL' 'Log in button not found'; return }
                    [NdWin]::Click($target[0], $target[1], $false)
                    [NdWin]::Move(1, 1)
                    $deadline = (Get-Date).AddSeconds(45)
                    while ((Get-Date) -lt $deadline -and -not ((Test-Path -LiteralPath $OpenedLog) -and (Get-Item -LiteralPath $OpenedLog).Length -gt 0)) { Start-Sleep -Milliseconds 500 }
                    Start-Sleep -Seconds 4
                    Snapshot-Tcp $proc.Id $tag 'after Log in'
                    Shot $v.Name '04-sign-in' | Out-Null
                    $signTexts = Uia-Dump $proc.Id "$tag-sign-in"
                    Check-Texts "texts-$tag-sign-in" $signTexts
                    $dest = Join-Path $Out "net\opened-urls-$tag.txt"
                    if (Test-Path -LiteralPath $OpenedLog) {
                        Copy-Item -LiteralPath $OpenedLog -Destination $dest -Force
                        Redact $dest
                        $line = Get-Content -LiteralPath $dest | Select-Object -First 1
                        $m = [regex]::Match($line, 'https?://[^\s"]+')
                        if ($m.Success -and $m.Value.StartsWith("$Server/")) { Result "browser-$tag" 'PASS' "app opened the browser at $($m.Value)" }
                        else { Result "browser-$tag" 'FAIL' "unexpected browser start: $line" }
                    } else {
                        Result "browser-$tag" 'FAIL' "no browser start within 45 s; see screens\$($v.Name)\04-sign-in.png and logs\app-$tag.log"
                    }
                }
            } finally {
                Stop-NoDrive $proc
                Redact $log
            }
        }
    }
}
Stop-NoDrive $null
Set-ItemProperty -Path $Personalize -Name AppsUseLightTheme -Value 1 -Type DWord
Set-ItemProperty -Path $Personalize -Name SystemUsesLightTheme -Value 1 -Type DWord

# ---------------------------------------------------------------------------
# 4. Network proof
# ---------------------------------------------------------------------------
Step 'no-nextcloud-host' {
    Start-Sleep -Seconds 2
    $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'Microsoft-Windows-DNS-Client/Operational'; StartTime = $TestStart } -ErrorAction SilentlyContinue)
    $queries = @()
    foreach ($e in $events) {
        if ($e.Id -notin 3006, 3008, 3020) { continue }
        $x = [xml]$e.ToXml()
        $qn = ($x.Event.EventData.Data | Where-Object { $_.Name -eq 'QueryName' } | Select-Object -First 1).'#text'
        if ($qn) { $queries += [pscustomobject]@{ Name = $qn.TrimEnd('.').ToLower(); Pid = $e.ProcessId; App = $NetPids.Contains([int]$e.ProcessId) } }
    }
    $byName = $queries | Group-Object Name | Sort-Object Count -Descending
    $lines = @("# DNS names looked up during the test ($($events.Count) DNS client events since $TestStart)", "count`tapp`tname")
    $lines += $byName | ForEach-Object { "$($_.Count)`t$(if ($_.Group | Where-Object App) { 'app' } else { '' })`t$($_.Name)" }
    Save-Text 'net\dns.txt' $lines
    $bad = @($byName | Where-Object { $_.Name -match $ForbiddenHost } | ForEach-Object Name)
    $appNames = @($queries | Where-Object App | ForEach-Object Name | Select-Object -Unique)
    $urlBad = @()
    foreach ($f in Get-ChildItem -LiteralPath (Join-Path $Out 'logs') -Filter 'app-*.log' -ErrorAction SilentlyContinue) {
        foreach ($m in [regex]::Matches([IO.File]::ReadAllText($f.FullName), 'https?://([A-Za-z0-9.-]+)')) {
            $h = $m.Groups[1].Value
            if ($h -match $ForbiddenHost -and $m.Value -notmatch '(?i)^https?://(next|own)cloud\.org/ns') { $urlBad += "$($f.Name): $($m.Value)" }
        }
    }
    $tcp = @(Get-ChildItem -LiteralPath (Join-Path $Out 'net') -Filter 'tcp-*.txt' | Get-Content | Where-Object { $_ -match '\t' } | ForEach-Object { ($_ -split "`t")[3] } | Where-Object { $_ -and $_ -notmatch '^(0\.0\.0\.0|::|127\.|::1)' } | Select-Object -Unique)
    $report = @("# Network report (Windows smoke test)", "", "DNS events: $($events.Count); names: $(@($byName).Count)", "Names looked up by the app: $($appNames -join ', ')", "Remote TCP endpoints of the app: $($tcp -join ', ')", "Nextcloud names: $($bad -join ', ')", "Nextcloud URLs in app logs: $($urlBad -join ', ')")
    Save-Text 'net\network-report.md' $report
    if ($bad.Count -eq 0 -and $urlBad.Count -eq 0 -and $events.Count -gt 0) {
        Result 'no-nextcloud-host' 'PASS' ("$(@($byName).Count) DNS names in $($events.Count) events, none Nextcloud; app looked up: $(if ($appNames) { $appNames -join ', ' } else { 'nothing' }); app TCP peers: $(if ($tcp) { $tcp -join ', ' } else { 'none' })")
    } elseif ($events.Count -eq 0) {
        Result 'no-nextcloud-host' 'FAIL' 'the DNS client log is empty: no proof'
    } else {
        Result 'no-nextcloud-host' 'FAIL' ("Nextcloud host(s): $($bad -join ', ') $($urlBad -join ', ')")
    }
}

# ---------------------------------------------------------------------------
# 5. Silent uninstall
# ---------------------------------------------------------------------------
$code = Msi-Exec @('/x', "`"$Msi`"", '/qn', '/norestart', '/l*v', "`"$(Join-Path $Out 'logs\uninstall.log')`"") 'uninstall'
Result 'uninstall' $(if ($code -eq 0 -or $code -eq 3010) { 'PASS' } else { 'FAIL' }) "msiexec /x /qn exit $code (logs\uninstall.log)"
Step 'uninstall-clean' {
    $left = @()
    if (Test-Path -LiteralPath $Exe) { $left += $Exe }
    foreach ($k in @("Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\$Vendor\$AppName", "Registry::HKEY_CLASSES_ROOT\AllFileSystemObjects\shellex\ContextMenuHandlers\$($AppName)ContextMenuHandler",
            "Registry::HKEY_CLASSES_ROOT\CLSID\$($Win.WIN_SHELLEXT_CONTEXT_MENU_GUID)", "Registry::HKEY_CLASSES_ROOT\CLSID\$($Win.WIN_SHELLEXT_OVERLAY_GUID_OK)",
            "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\$Scheme")) {
        if (Test-Path -LiteralPath $k) { $left += $k }
    }
    $ov = @(Get-ChildItem 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers' | Where-Object { $_.PSChildName.Trim() -like "$AppName*" } | ForEach-Object PSChildName)
    $left += $ov
    foreach ($l in @((Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs\$AppName.lnk"), (Join-Path $env:PUBLIC "Desktop\$AppName.lnk"))) { if (Test-Path -LiteralPath $l) { $left += $l } }
    $arp = @(Get-ChildItem 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' | Where-Object { $_.GetValue('DisplayName') -eq $AppName })
    if ($arp.Count) { $left += 'Apps & features entry' }
    $userData = Test-Path -LiteralPath $ConfDir
    Result 'uninstall-clean' $(if ($left.Count -eq 0) { 'PASS' } else { 'FAIL' }) ("left behind: $(if ($left) { $left -join ', ' } else { 'nothing' }) (program files, shortcuts, HKLM keys, Explorer handlers and overlays, $($Scheme):// handler, Apps & features entry checked); user settings folder $ConfDir kept: $userData (normal for an uninstall)")
}

# ---------------------------------------------------------------------------
# 6. Administrative image for the packaged-files sweep
# ---------------------------------------------------------------------------
Step 'admin-image' {
    $img = Join-Path $Out 'admin-image'
    $code = Msi-Exec @('/a', "`"$Msi`"", '/qn', "TARGETDIR=`"$img`"", '/l*v', "`"$(Join-Path $Out 'logs\admin-image.log')`"") 'admin image'
    $exeInImg = Get-ChildItem -LiteralPath $img -Recurse -File -Filter "$ExeName.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($code -eq 0 -and $exeInImg) {
        $dir = $exeInImg.DirectoryName
        [IO.File]::WriteAllText((Join-Path $Out 'admin-image-app-dir.txt'), $dir, $Utf8)
        Result 'admin-image' 'PASS' "msiexec /a exit 0; app folder in the image: $($dir.Substring($Out.Length + 1))"
    } else { Result 'admin-image' 'FAIL' "msiexec /a exit $code; $ExeName.exe found: $([bool]$exeInImg)" }
}

# ---------------------------------------------------------------------------
$rows = @(Get-Content -LiteralPath $ResultsFile | Where-Object { $_ })
$fails = @($rows | Where-Object { ($_ -split "`t")[1] -eq 'FAIL' })
$md = @('## NoDrive Windows install and smoke test', '', '| Check | Result | Evidence |', '|---|---|---|')
$md += $rows | ForEach-Object { $c = $_ -split "`t", 3; "| $($c[0]) | $($c[1]) | $(($c[2] -replace '\|', '\|')) |" }
$md += @('', "Failed checks: $($fails.Count)")
Save-Text 'summary.md' $md
Log "done: $($rows.Count) results, $($fails.Count) failed"
if ($fails.Count -gt 0) { exit 1 }
exit 0
