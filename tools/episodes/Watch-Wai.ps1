#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Client,
    [ValidateRange(1,4)][int]$Count = 4,
    [ValidatePattern('^[1-4](,[1-4])*$')][string]$Slots,
    [Parameter(Mandatory)][string]$Credentials,
    [Parameter(Mandatory)][string]$ObserverAccounts,
    [string]$LoginFile = "$PSScriptRoot/wai-watch-login.xml",
    [string]$Url = 'http://127.0.0.1:7878/',
    [int]$StartupSeconds = 12,
    [int]$CharacterSelectSeconds = 12,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$selectedSlots = if ($Slots) { @($Slots.Split(',') | ForEach-Object { [int]$_ } | Select-Object -Unique) } else { @(1..$Count) }
$observerAccountSlots = $ObserverAccounts.Split(',')
if ($observerAccountSlots.Count -ne 4 -or $ObserverAccounts -match '[\r\n]') { throw 'Supply four private observer-account slots, separated by commas.' }
foreach ($slot in $selectedSlots) {
    if ([string]::IsNullOrWhiteSpace($observerAccountSlots[$slot-1])) { throw "Observer account slot $slot is not configured." }
}
if (-not (Test-Path -LiteralPath $Client -PathType Leaf)) { throw 'Watch client executable does not exist.' }
$endpoint = [Uri]$Url
if (-not $endpoint.IsLoopback -or $endpoint.Scheme -ne 'http') { throw 'Watch requires loopback HTTP SOAP.' }
if ($ValidateOnly) { return }
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WaiWatchWindows {
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public DATA data; }
    [StructLayout(LayoutKind.Explicit)] public struct DATA {
        [FieldOffset(0)] public KEYBDINPUT keyboard;
        [FieldOffset(0)] public MOUSEINPUT mouse;
    }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
        public ushort vk, scan; public uint flags, time; public UIntPtr extra;
    }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
        public int x, y; public uint data, flags, time; public UIntPtr extra;
    }
    [DllImport("user32.dll")] static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int cmd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x,y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
    public static void Key(ushort vk, ushort scan, uint flags) {
        INPUT input = new INPUT(); input.type=1;
        input.data.keyboard.vk=vk; input.data.keyboard.scan=scan; input.data.keyboard.flags=flags;
        if(SendInput(1,new INPUT[]{input},Marshal.SizeOf<INPUT>())!=1) throw new Exception("Input injection failed.");
    }
    public static void Click() {
        INPUT down=new INPUT(); down.type=0; down.data.mouse.flags=2;
        INPUT up=new INPUT(); up.type=0; up.data.mouse.flags=4;
        if(SendInput(2,new INPUT[]{down,up},Marshal.SizeOf<INPUT>())!=2) throw new Exception("Mouse injection failed.");
    }
}
'@
function Focus-Client($Window) {
    [void][WaiWatchWindows]::ShowWindow($Window, 9)
    [void][WaiWatchWindows]::SetForegroundWindow($Window)
    Start-Sleep -Milliseconds 300
    if ([WaiWatchWindows]::GetForegroundWindow() -ne $Window) {
        throw 'Cannot focus observer client; stopped before typing credentials.'
    }
}
function Press-Key([ushort]$Key) {
    [WaiWatchWindows]::Key($Key,0,0)
    [WaiWatchWindows]::Key($Key,0,2)
}
function Type-Private([string]$Text, $Window) {
    foreach ($character in $Text.ToCharArray()) {
        if ([WaiWatchWindows]::GetForegroundWindow() -ne $Window) { throw 'Client lost focus; credential entry stopped.' }
        [WaiWatchWindows]::Key(0,[ushort]$character,4)
        [WaiWatchWindows]::Key(0,[ushort]$character,6)
    }
}
function Clear-Field {
    [WaiWatchWindows]::Key(0x11,0,0)
    Press-Key 0x41
    [WaiWatchWindows]::Key(0x11,0,2)
    Press-Key 0x08
}
function Get-ObserverLogin {
    $form = [Windows.Forms.Form]::new()
    $form.Text = 'WAI observer login setup'
    $form.Width = 450; $form.Height = 180; $form.StartPosition = 'CenterScreen'; $form.TopMost = $true
    $label = [Windows.Forms.Label]::new()
    $label.Text = 'Shared password for the four GM observer accounts:'
    $label.SetBounds(12,15,410,25)
    $password = [Windows.Forms.TextBox]::new()
    $password.UseSystemPasswordChar = $true; $password.SetBounds(12,45,405,25)
    $ok = [Windows.Forms.Button]::new()
    $ok.Text = 'Save encrypted'; $ok.SetBounds(165,85,130,28); $ok.DialogResult = 'OK'
    $cancel = [Windows.Forms.Button]::new()
    $cancel.Text = 'Cancel'; $cancel.SetBounds(310,85,105,28); $cancel.DialogResult = 'Cancel'
    $form.Controls.AddRange(@($label,$password,$ok,$cancel)); $form.AcceptButton=$ok; $form.CancelButton=$cancel
    try {
        if ($form.ShowDialog() -ne 'OK' -or -not $password.Text) { throw 'Observer login cancelled.' }
        return [PSCredential]::new('WAI_OBSERVER',(ConvertTo-SecureString $password.Text -AsPlainText -Force))
    } finally { $password.Clear(); $form.Dispose() }
}
function Read-LoginPoint($Window, [int]$Key) {
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ([WaiWatchWindows]::GetForegroundWindow() -eq $Window -and
            ([WaiWatchWindows]::GetAsyncKeyState($Key) -band 0x8000)) {
            $point = [WaiWatchWindows+POINT]::new()
            $rect = [WaiWatchWindows+RECT]::new()
            [void][WaiWatchWindows]::GetCursorPos([ref]$point)
            [void][WaiWatchWindows]::GetWindowRect($Window,[ref]$rect)
            if ($point.x -le $rect.left -or $point.x -ge $rect.right -or
                $point.y -le $rect.top -or $point.y -ge $rect.bottom) { throw 'Login point outside client.' }
            Start-Sleep -Milliseconds 300
            return @{x=$point.x-$rect.left;y=$point.y-$rect.top}
        }
        Start-Sleep -Milliseconds 50
    }
    throw 'Login calibration cancelled by timeout; no password was typed.'
}
function Click-LoginField($Window, $Point) {
    Focus-Client $Window
    $rect = [WaiWatchWindows+RECT]::new()
    [void][WaiWatchWindows]::GetWindowRect($Window,[ref]$rect)
    if ($Point.x -lt 0 -or $Point.x -ge 1024 -or $Point.y -lt 0 -or $Point.y -ge 768) { throw 'Invalid login calibration.' }
    [void][WaiWatchWindows]::SetCursorPos(($rect.left+$Point.x),($rect.top+$Point.y))
    [WaiWatchWindows]::Click()
    Clear-Field
}
function Soap([string]$Command) {
    if ($Command -notmatch '^waiobserver (status|join) [1-4]$') { throw 'Unexpected watch SOAP command.' }
    $escaped = [System.Security.SecurityElement]::Escape($Command)
    $body = '<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/"><SOAP-ENV:Body><ns1:executeCommand xmlns:ns1="urn:AC"><command>' + $escaped + '</command></ns1:executeCommand></SOAP-ENV:Body></SOAP-ENV:Envelope>'
    $message = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Post,$endpoint)
    $message.Content = [Net.Http.StringContent]::new($body,[Text.Encoding]::UTF8,'text/xml')
    try {
        $response = $http.Send($message)
        try {
            if (-not $response.IsSuccessStatusCode) { throw 'Observer SOAP request rejected.' }
            [xml]$xml = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            $result = $xml.SelectSingleNode('//*[local-name()="result"]')
            if (-not $result) { throw 'Observer SOAP result missing.' }
            return $result.InnerText.Trim()
        } finally { $response.Dispose() }
    } finally { $message.Dispose() }
}
$bridge = Get-Content -LiteralPath $Credentials -Raw | ConvertFrom-Json
$handler = [Net.Http.HttpClientHandler]::new()
$handler.AllowAutoRedirect = $false
$handler.UseProxy = $false
$handler.Credentials = [Net.NetworkCredential]::new($bridge.Username,$bridge.Password)
$http = [Net.Http.HttpClient]::new($handler)
$http.Timeout = [TimeSpan]::FromSeconds(10)
$bridge = $null
$login = if (Test-Path -LiteralPath $LoginFile) { Import-Clixml -LiteralPath $LoginFile } else {
    $value = Get-ObserverLogin
    $value | Export-Clixml -LiteralPath $LoginFile
    $value
}
if ($login -isnot [PSCredential]) { throw 'Invalid encrypted observer login file.' }
$calibrationFile = "$LoginFile.fields.json"
$fields = if (Test-Path -LiteralPath $calibrationFile) { Get-Content -LiteralPath $calibrationFile -Raw | ConvertFrom-Json } else { $null }
$area = [Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$width = [int][Math]::Floor($area.Width/2)
$height = [int][Math]::Floor($area.Height/2)
$mutex = [Threading.Mutex]::new($false,'Local\WaiObserverClientLauncher')
if (-not $mutex.WaitOne(0)) { throw 'Another observer launcher is active.' }
try {
    foreach ($slot in $selectedSlots) {
        try {
        $state = Soap "waiobserver status $slot"
        if ($state -eq 'WAI_OBSERVER_OFFLINE') {
            $taskProcess = Start-Process -FilePath $Client -ArgumentList '-windowed' -WorkingDirectory (Split-Path -Parent $Client) -PassThru
            $deadline = [DateTime]::UtcNow.AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                $taskProcess.Refresh()
                if ($taskProcess.HasExited) { throw "Observer client $slot exited." }
            } until ($taskProcess.MainWindowHandle -ne 0 -or [DateTime]::UtcNow -gt $deadline)
            $window = $taskProcess.MainWindowHandle
            if ($window -eq 0) { throw "Observer client $slot did not open a window." }
            [void][WaiWatchWindows]::SetWindowPos($window,[IntPtr]::Zero,$area.Left,$area.Top,1024,768,4)
            Start-Sleep -Seconds $StartupSeconds
            if (-not $fields) {
                [void][Windows.Forms.MessageBox]::Show('One-time login calibration: on the WoW login screen, point at ACCOUNT NAME and press F8, then point at PASSWORD and press F9. No credentials are typed until both are recorded.','WAI Watch setup')
                Focus-Client $window
                $accountPoint = Read-LoginPoint $window 0x77
                $passwordPoint = Read-LoginPoint $window 0x78
                $fields = @{account=$accountPoint;password=$passwordPoint}
                $fields | ConvertTo-Json | Set-Content -LiteralPath $calibrationFile
            }
            Click-LoginField $window $fields.account
            Type-Private $observerAccountSlots[$slot-1] $window
            Click-LoginField $window $fields.password
            Type-Private ($login.GetNetworkCredential().Password) $window
            Press-Key 0x0D
            Start-Sleep -Seconds $CharacterSelectSeconds
            Focus-Client $window
            Press-Key 0x0D
            $deadline = [DateTime]::UtcNow.AddSeconds(90)
            do {
                Start-Sleep -Seconds 2
                $state = Soap "waiobserver status $slot"
            } until ($state -ne 'WAI_OBSERVER_OFFLINE' -or [DateTime]::UtcNow -gt $deadline)
            if ($state -eq 'WAI_OBSERVER_OFFLINE') { throw "Observer $slot login timed out. Check login/realm/character screen." }
            $column = ($slot-1)%2
            $row = [int][Math]::Floor(($slot-1)/2)
            [void][WaiWatchWindows]::SetWindowPos($window,[IntPtr]::Zero,($area.Left+$column*$width),($area.Top+$row*$height),$width,$height,4)
        }
        $joined = Soap "waiobserver join $slot"
        if ($joined -ne 'WAI_OBSERVER_JOINING') { throw "Observer $slot could not join its party; check server receipts." }
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            Start-Sleep -Seconds 1
            $state = Soap "waiobserver status $slot"
        } until ($state -eq 'WAI_OBSERVER_READY' -or [DateTime]::UtcNow -gt $deadline)
        if ($state -ne 'WAI_OBSERVER_READY') { throw "Observer $slot did not enter the tank's instance." }
        Write-Output "WAI WATCH: observer $slot is invisible in its party's instance."
        } catch {
            # A failed observer must never prevent the other parties from being watched.
            Write-Warning "WAI WATCH: observer $slot failed; check its client and server receipts, then retry watch."
        }
    }
} finally {
    $http.Dispose()
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
