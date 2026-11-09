#requires -Version 7.0
# Pure settings/credential fixtures only. No Python/controller/client/server/network is launched.
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'WaiRuntime.psm1') -Force
$oldUser = $env:WAI_SOAP_USER
$oldPassword = $env:WAI_SOAP_PASSWORD
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('wai-runtime-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixtureRoot)
$fixtureFile = Join-Path $fixtureRoot 'synthetic.json'
function Require-Rejection([scriptblock]$Action) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw 'Expected invalid runtime configuration to reject.' }
}
try {
    $env:WAI_SOAP_USER = 'Synthetic'; $env:WAI_SOAP_PASSWORD = 'fixture-only'
    $credential = Read-WaiSoapCredential
    if ($credential.UserName -ne 'Synthetic' -or $credential.GetNetworkCredential().Password -ne 'fixture-only') {
        throw 'Complete environment pair was not preserved.'
    }
    $env:WAI_SOAP_PASSWORD = ''
    Require-Rejection { Read-WaiSoapCredential }
    $env:WAI_SOAP_USER = ''
    Require-Rejection { Read-WaiSoapCredential }
    [IO.File]::WriteAllText($fixtureFile, '{"Username":"Synthetic","Password":"fixture-only"}')
    $credential = Read-WaiSoapCredential -Path $fixtureFile
    if ($credential.UserName -ne 'Synthetic') { throw 'Explicit JSON did not resolve.' }
    $env:WAI_SOAP_USER = 'Synthetic'; $env:WAI_SOAP_PASSWORD = 'fixture-only'
    Require-Rejection { Read-WaiSoapCredential -Path $fixtureFile }
    $env:WAI_SOAP_USER = ''; $env:WAI_SOAP_PASSWORD = ''
    [IO.File]::WriteAllText($fixtureFile, '{"Username":"bad:account","Password":"fixture-only"}')
    Require-Rejection { Read-WaiSoapCredential -Path $fixtureFile }
    [IO.File]::WriteAllText($fixtureFile, '{"Username":"Synthetic","Password":true}')
    Require-Rejection { Read-WaiSoapCredential -Path $fixtureFile }
    $hostExecutable = Join-Path $PSHOME $(if ($IsWindows) { 'pwsh.exe' } else { 'pwsh' })
    if ((Resolve-WaiPython -Python $hostExecutable -Root $fixtureRoot) -ne [IO.Path]::GetFullPath($hostExecutable)) {
        throw 'Explicit executable path did not resolve.'
    }
    Require-Rejection { Resolve-WaiPython -Python 'missing-wai-fixture-executable' -Root $fixtureRoot }
    foreach ($name in @('Start-Wai.ps1', 'Start-Navigation.ps1')) {
        $tokens = $null; $errors = $null
        [void][Management.Automation.Language.Parser]::ParseFile(
            (Join-Path (Split-Path $PSScriptRoot -Parent) $name), [ref]$tokens, [ref]$errors)
        if ($errors.Count) { throw "Launcher parse failed: $name" }
    }
    [Console]::WriteLine('PASS: explicit Python/credential settings, missing/partial/conflicting values, JSON validation and launcher syntax.')
} finally {
    $env:WAI_SOAP_USER = $oldUser; $env:WAI_SOAP_PASSWORD = $oldPassword
    if ([IO.File]::Exists($fixtureFile)) { [IO.File]::Delete($fixtureFile) }
    [IO.Directory]::Delete($fixtureRoot)
}
