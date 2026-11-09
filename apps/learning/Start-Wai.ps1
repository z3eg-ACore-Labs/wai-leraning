# Starts only the external local controller after the user invokes this script.
# It never builds or starts worldserver and never grants an in-game control lease.
#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$Model,
    [string]$Route,
    [Alias('VoiceCredentialPath')][string]$Credentials,
    [string]$Python,
    [string]$Log = "$PSScriptRoot/data/live-decisions.jsonl",
    [string]$Url = 'http://127.0.0.1:7878/'
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WaiRuntime.psm1') -Force
$taskPython = Resolve-WaiPython -Python $Python -Root $PSScriptRoot
if (-not (Test-Path -LiteralPath $Model -PathType Leaf)) { throw 'Trained model file not found.' }
if ($Route -and -not (Test-Path -LiteralPath $Route -PathType Leaf)) { throw 'Route recording not found.' }
$taskOldUser = $env:WAI_SOAP_USER
$taskOldPassword = $env:WAI_SOAP_PASSWORD
try {
    $taskCredential = Read-WaiSoapCredential -Path $Credentials
    $env:WAI_SOAP_USER = $taskCredential.UserName
    $env:WAI_SOAP_PASSWORD = $taskCredential.GetNetworkCredential().Password
    $taskCredential = $null
    $taskArguments = @((Join-Path $PSScriptRoot 'live.py'), '--model', $Model, '--url', $Url,
        '--log', $Log)
    if ($Route) { $taskArguments += @('--route', $Route) }
    & $taskPython @taskArguments
    if ($LASTEXITCODE -ne 0) { throw "WAI exited with code $LASTEXITCODE." }
} finally {
    $env:WAI_SOAP_USER = $taskOldUser
    $env:WAI_SOAP_PASSWORD = $taskOldPassword
}
