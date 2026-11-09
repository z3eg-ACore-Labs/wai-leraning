#requires -Version 7.0
# Explicit launch only; never starts a server/game or grants character control.
param([int]$OwnerPid = 0, [string]$Url = 'http://127.0.0.1:7878/',
      [Alias('VoiceCredentialPath')][string]$Credentials, [string]$Python,
      [string[]]$Recordings = @())
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WaiRuntime.psm1') -Force
$taskPython = Resolve-WaiPython -Python $Python -Root $PSScriptRoot
$taskUserBefore = $env:WAI_SOAP_USER
$taskPasswordBefore = $env:WAI_SOAP_PASSWORD
try {
    $taskCredential = Read-WaiSoapCredential -Path $Credentials
    $env:WAI_SOAP_USER = $taskCredential.UserName
    $env:WAI_SOAP_PASSWORD = $taskCredential.GetNetworkCredential().Password
    $taskCredential = $null
    $taskArguments = @('-u', (Join-Path $PSScriptRoot 'navigation_controller.py'), '--url', $Url)
    if ($OwnerPid -gt 0) { $taskArguments += @('--owner-pid', [string]$OwnerPid) }
    foreach ($taskRecording in $Recordings) { $taskArguments += @('--recordings', $taskRecording) }
    & $taskPython @taskArguments
    if ($LASTEXITCODE -ne 0) { throw "Navigation helper exited with code $LASTEXITCODE." }
} finally {
    $env:WAI_SOAP_USER = $taskUserBefore
    $env:WAI_SOAP_PASSWORD = $taskPasswordBefore
}
