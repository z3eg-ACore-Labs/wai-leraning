# Explicit local runtime settings; no server/game launch or credential discovery.
function Resolve-WaiPython {
    param([string]$Python, [Parameter(Mandatory)][string]$Root)
    if (-not $Python) {
        $Python = Join-Path $Root $(if ($IsWindows) { '.venv/Scripts/python.exe' } else { '.venv/bin/python' })
    }
    if ([IO.File]::Exists($Python)) { return [IO.Path]::GetFullPath($Python) }
    $command = Get-Command -Name $Python -ErrorAction SilentlyContinue
    if ($command -and $command.CommandType -eq 'Application') { return $command.Source }
    throw 'Learning Python is unavailable. Create a normal virtual environment or pass -Python explicitly.'
}

function Read-WaiSoapCredential {
    param([string]$Path)
    $taskUser = $env:WAI_SOAP_USER
    $taskPassword = $env:WAI_SOAP_PASSWORD
    if ([bool]$taskUser -ne [bool]$taskPassword) { throw 'Set both WAI_SOAP_USER and WAI_SOAP_PASSWORD, or clear both.' }
    if ($Path) {
        if ($taskUser) { throw 'Use either a complete SOAP environment pair or -Credentials, not both.' }
        try {
            $value = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
            $taskUser = $value.Username
            $taskPassword = $value.Password
        } catch { throw 'Cannot read the configured private SOAP credential JSON.' }
    }
    if ($taskUser -isnot [string] -or [string]::IsNullOrWhiteSpace($taskUser) -or
        $taskUser -match '[:\r\n]' -or $taskPassword -isnot [string] -or -not $taskPassword) {
        throw 'Configure a complete private SOAP environment pair or JSON with Username and Password.'
    }
    $secure = ConvertTo-SecureString -String $taskPassword -AsPlainText -Force
    return [Management.Automation.PSCredential]::new($taskUser, $secure)
}
Export-ModuleMember -Function Resolve-WaiPython, Read-WaiSoapCredential
