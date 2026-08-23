param(
    [string]$ToolchainDir = 'D:\Keil_v5\ARM\Version5.06\bin',
    [string]$Uv4Path = 'D:\Keil_v5\UV4\UV4.exe',
    [switch]$SkipSimulator
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDir = Join-Path $PSScriptRoot 'Build\Phase9'
$armcc = Join-Path $ToolchainDir 'armcc.exe'
$armasm = Join-Path $ToolchainDir 'armasm.exe'
$armlink = Join-Path $ToolchainDir 'armlink.exe'
$fromelf = Join-Path $ToolchainDir 'fromelf.exe'
$project = Join-Path $repoRoot 'firmware\Project\Keil\BMS_V1.uvprojx'
$userOptions = [IO.Path]::ChangeExtension($project, '.uvoptx')
$buildLog = Join-Path $buildDir 'phase9_build.log'
$simLog = Join-Path $buildDir 'phase9_simulator.log'
$axf = Join-Path $buildDir 'phase9_tests.axf'
$map = Join-Path $buildDir 'phase9_tests.map'

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
foreach ($path in @($buildLog, $simLog, $axf, $map)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

$log = [Collections.Generic.List[string]]::new()
$log.Add('BMS V1 Phase 9 simulation-development build')
$log.Add('HARDWARE_CLAIM=NONE')

function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments, [string]$Label)
    $script:log.Add('RUN: ' + $Label)
    $output = & $FilePath @Arguments 2>&1
    foreach ($line in $output) { $script:log.Add([string]$line) }
    if ($LASTEXITCODE -ne 0) {
        $script:log | Set-Content -LiteralPath $buildLog -Encoding UTF8
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

$includeDirs = @(
    'firmware\App', 'firmware\Config', 'firmware\Driver',
    'firmware\Tests',
    'docs\reference\ST\STM32F10x Standard Peripheral Library',
    'docs\reference\ST\STM32F10x Standard Peripheral Library\Start',
    'docs\reference\ST\STM32F10x Standard Peripheral Library\Libarary',
    'docs\FreeRTOS\include', 'docs\FreeRTOS\portable'
)
$common = @('--cpu', 'Cortex-M3', '--c99', '--diag_error=warning',
            '--apcs=interwork', '--split_sections', '--signed_chars',
            '-O0', '-g', '-DSTM32F10X_MD', '-DUSE_STDPERIPH_DRIVER',
            '-DTEST_PHASE9_IMAGE')
foreach ($dir in $includeDirs) {
    $common += '-I' + (Join-Path $repoRoot $dir)
}

$startup = Join-Path $repoRoot `
    'docs\reference\ST\STM32F10x Standard Peripheral Library\Start\startup_stm32f10x_md.s'
$startupObject = Join-Path $buildDir 'startup_stm32f10x_md.o'
Invoke-Checked $armasm @('--cpu', 'Cortex-M3', '--apcs=interwork', '-g',
    '-I', (Split-Path -Parent $startup), $startup, '-o', $startupObject) `
    'assemble startup'

$sources = [ordered]@{
    bms_fault = 'firmware\App\bms_fault.c'
    bms_ntc = 'firmware\App\bms_ntc.c'
    bms_policy = 'firmware\Config\bms_policy.c'
    bq_control = 'firmware\Driver\bq76940_control.c'
    bms_state = 'firmware\App\bms_state.c'
    bms_health = 'firmware\App\bms_health.c'
    bms_hw_recovery = 'firmware\App\bms_hw_recovery.c'
    bms_recovery = 'firmware\App\bms_recovery.c'
    bms_fet_manager = 'firmware\App\bms_fet_manager.c'
    test_stub = 'firmware\Tests\test_phase9_stub.c'
    test_phase9 = 'firmware\Tests\test_phase9.c'
    test_main = 'firmware\Tests\test_phase9_main.c'
}
$objects = [Collections.Generic.List[string]]::new()
$objects.Add($startupObject)
foreach ($entry in $sources.GetEnumerator()) {
    $source = Join-Path $repoRoot $entry.Value
    $object = Join-Path $buildDir ($entry.Key + '.o')
    Invoke-Checked $armcc ($common + @('-c', $source, '-o', $object)) `
        ('compile ' + ($entry.Value -replace '\\', '/'))
    $objects.Add($object)
}

$scatter = Join-Path $PSScriptRoot 'phase8_tests.sct'
$linkArgs = @('--cpu', 'Cortex-M3', '--scatter', $scatter,
              '--remove', '--map', '--list', $map, '--xref', '--symbols',
              '--info', 'sizes,totals,unused,veneers') +
            $objects.ToArray() + @('-o', $axf)
Invoke-Checked $armlink $linkArgs 'link Phase 9 test image'
$sizes = & $fromelf --info=sizes,totals $axf 2>&1
foreach ($line in $sizes) { $log.Add([string]$line) }
$log.Add('PHASE9_TEST_IMAGE_BUILD_PASS')
$log | Set-Content -LiteralPath $buildLog -Encoding UTF8

if (-not $SkipSimulator) {
    $optionBytes = [IO.File]::ReadAllBytes($userOptions)
    $optionTime = (Get-Item -LiteralPath $userOptions).LastWriteTimeUtc
    try {
        $encoding = [Text.Encoding]::GetEncoding(28591)
        $text = $encoding.GetString($optionBytes)
        if ([regex]::Matches($text, '<sIfile>[^<]*</sIfile>').Count -ne 1) {
            throw 'Keil options must have one sIfile'
        }
        $text = [regex]::Replace($text, '<sIfile>[^<]*</sIfile>',
            '<sIfile>..\..\Tests\phase9_simulator.ini</sIfile>')
        [IO.File]::WriteAllBytes($userOptions, $encoding.GetBytes($text))
        $arguments = '-d "' + $project + '" -t BMS_V1 -j0'
        $process = Start-Process -FilePath $Uv4Path `
            -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) {
            throw "Keil Simulator failed with exit code $($process.ExitCode)"
        }
    }
    finally {
        [IO.File]::WriteAllBytes($userOptions, $optionBytes)
        [IO.File]::SetLastWriteTimeUtc($userOptions, $optionTime)
    }
    if (-not (Test-Path -LiteralPath $simLog -PathType Leaf)) {
        throw 'Phase 9 Simulator log missing'
    }
    $simText = Get-Content -LiteralPath $simLog -Raw
    foreach ($required in @('PHASE9_TEST_COMPLETED=1',
                             'PHASE9_TEST_FAILURES=0',
                             'P9_SCENARIOS_COMPLETED=24',
                             'P9_RACES_COMPLETED=3')) {
        if ($simText -notmatch [regex]::Escape($required)) {
            throw "Phase 9 Simulator failed evidence check: $required"
        }
    }
    & python (Join-Path $PSScriptRoot 'verify_phase9.py')
    if ($LASTEXITCODE -ne 0) {
        throw 'Phase 9 simulation-development verifier reported FAIL'
    }
    Write-Output 'PHASE9 SIMULATOR TESTS PASS'
}
else {
    Write-Output 'PHASE9 TEST IMAGE BUILD PASS; SIMULATOR NOT EXECUTED'
}
