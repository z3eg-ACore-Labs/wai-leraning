#requires -Version 7.0
# Offline regression: execute the actual launch loop with online-observer SOAP doubles.
# No process is launched, no window API is called, and no network request is made.
$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    "$PSScriptRoot/Watch-Wai.ps1", [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Watcher does not parse.' }
$loop = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.ForEachStatementAst] -and
    $node.Variable.VariablePath.UserPath -eq 'slot' -and
    $node.Extent.Text.Contains('waiobserver join')
}, $true)
if (-not $loop) { throw 'Watcher slot loop not found.' }
$script:joined = [Collections.Generic.List[int]]::new()
$script:warnings = [Collections.Generic.List[string]]::new()
$script:receipts = [Collections.Generic.List[string]]::new()
function Soap([string]$command) {
    $slot = [int]($command.Split(' ')[-1])
    if ($command.StartsWith('waiobserver join ')) {
        if ($slot -eq 1) { throw 'Synthetic failed observer' }
        $script:joined.Add($slot)
        return 'WAI_OBSERVER_JOINING'
    }
    if ($script:joined.Contains($slot)) { return 'WAI_OBSERVER_READY' }
    return 'WAI_OBSERVER_ONLINE'
}
function Start-Sleep { param($Seconds, $Milliseconds) }
function Write-Warning([string]$message) { $script:warnings.Add($message) }
function Write-Output([string]$message) { $script:receipts.Add($message) }
$selectedSlots = @(1, 3, 4)
& ([scriptblock]::Create($loop.Extent.Text))
if (($script:joined -join ',') -ne '3,4') { throw 'Failed slot prevented subsequent observers or changed slot identity.' }
if ($script:warnings.Count -ne 1 -or $script:receipts.Count -ne 2) { throw 'Incorrect success/failure receipts.' }
[Console]::WriteLine('PASS: one observer failure leaves other selected slots running; quadrant identities preserved.')

# ValidateOnly exits before native window APIs, credential reads, processes and SOAP.
& "$PSScriptRoot/Watch-Wai.ps1" -Client $PSCommandPath -Credentials 'synthetic-unused.json' `
    -ObserverAccounts 'Observerone,,Observerthree,Observerfour' -Slots '1,3,4' -ValidateOnly
$rejected = $false
try {
    & "$PSScriptRoot/Watch-Wai.ps1" -Client $PSCommandPath -Credentials 'synthetic-unused.json' `
        -ObserverAccounts ',Observertwo,Observerthree,Observerfour' -Slots '1' -ValidateOnly
} catch { $rejected = $true }
if (-not $rejected) { throw 'Missing selected observer account did not reject before launch.' }
[Console]::WriteLine('PASS: configured private account slots validate; missing selected slot fails before launch.')
