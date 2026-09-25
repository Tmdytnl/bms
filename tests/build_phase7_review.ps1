param(
    [string]$ToolchainDir = 'D:\Keil_v5\ARM\Version5.06\bin',
    [string]$Uv4Path = 'D:\Keil_v5\UV4\UV4.exe',
    [string]$PythonPath = 'python',
    [switch]$SkipSimulator,
    [switch]$SkipProductionBuild
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $PSScriptRoot 'Build\Phase7Review'
$armcc = Join-Path $ToolchainDir 'armcc.exe'
$armasm = Join-Path $ToolchainDir 'armasm.exe'
$armlink = Join-Path $ToolchainDir 'armlink.exe'
$fromelf = Join-Path $ToolchainDir 'fromelf.exe'
$project = Join-Path $repoRoot 'APP\keil\BMS_V1.uvprojx'
$productionLog = Join-Path $repoRoot 'APP\keil\Build\BMS_V1_Codex_Phase7_build.log'
$productionMap = Join-Path $repoRoot 'APP\keil\Listings\BMS_V1.map'
$buildLog = Join-Path $buildDir 'phase7_review_build.log'
$simLog = Join-Path $buildDir 'phase7_review_simulator.log'
$verifyLog = Join-Path $buildDir 'verify_phase7_review.log'
$phase4Axf = Join-Path $buildDir 'phase4_review_tests.axf'
$phase4Map = Join-Path $buildDir 'phase4_review_tests.map'
$phase6Axf = Join-Path $buildDir 'phase6_review_tests.axf'
$phase6Map = Join-Path $buildDir 'phase6_review_tests.map'
$phase7Axf = Join-Path $buildDir 'phase7_review_tests.axf'
$phase7Map = Join-Path $buildDir 'phase7_review_tests.map'
$userOptions = [System.IO.Path]::ChangeExtension($project, '.uvoptx')

foreach ($tool in @($armcc, $armasm, $armlink, $fromelf)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required ARMCC5 tool is missing: $tool"
    }
}
if ((-not $SkipSimulator -or -not $SkipProductionBuild) -and
    -not (Test-Path -LiteralPath $Uv4Path -PathType Leaf)) {
    throw "Required Keil executable is missing: $Uv4Path"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
foreach ($staleEvidence in @($buildLog, $simLog, $verifyLog)) {
    if (Test-Path -LiteralPath $staleEvidence) {
        Remove-Item -LiteralPath $staleEvidence -Force
    }
}

$logLines = [System.Collections.Generic.List[string]]::new()
$logLines.Add('BMS V1 Codex Phase 7 review test build')
$logLines.Add("Repository: $repoRoot")
$logLines.Add("ARMCC5: $armcc")
if (Test-Path -LiteralPath $Uv4Path -PathType Leaf) {
    $uv4Version = (Get-Item -LiteralPath $Uv4Path).VersionInfo.FileVersion
    $logLines.Add("uVision executable: $Uv4Path")
    $logLines.Add("uVision FileVersion: $uv4Version")
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $script:logLines.Add("RUN: $Label")
    $output = & $FilePath @Arguments 2>&1
    if ($output) {
        foreach ($line in $output) {
            $script:logLines.Add([string]$line)
        }
    }
    if ($LASTEXITCODE -ne 0) {
        $script:logLines | Set-Content -LiteralPath $buildLog -Encoding UTF8
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Invoke-KeilPreservingUserOptions {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $optionsBytes = [System.IO.File]::ReadAllBytes($userOptions)
    $optionsLastWriteUtc =
        (Get-Item -LiteralPath $userOptions).LastWriteTimeUtc
    try {
        # Start-Process 会把 argument array 合成 command line；显式加 quote，确保
        # 含空格 checkout path 对 uVision 仍是单个 argument。
        $processArguments = foreach ($argument in $Arguments) {
            if ($argument.Contains('"')) {
                throw "Keil argument contains an unsupported quote: $argument"
            }
            if ($argument -match '\s') {
                '"' + $argument + '"'
            }
            else {
                $argument
            }
        }
        $argumentLine = $processArguments -join ' '
        $process = Start-Process -FilePath $Uv4Path `
            -ArgumentList $argumentLine -WindowStyle Hidden -Wait -PassThru
        return $process.ExitCode
    }
    finally {
        [System.IO.File]::WriteAllBytes($userOptions, $optionsBytes)
        [System.IO.File]::SetLastWriteTimeUtc($userOptions,
                                              $optionsLastWriteUtc)
    }
}

foreach ($tool in @($armcc, $armasm, $armlink, $fromelf)) {
    Invoke-Checked -FilePath $tool -Arguments @('--vsn') `
        -Label ("record tool version: " + [System.IO.Path]::GetFileName($tool))
}

$includeDirs = @(
    'firmware\App',
    'APP\fml', 'APP\os',
    'firmware\Driver',
    'tests',
    'APP\RTD\ST',
    'APP\RTD\ST\Start',
    'APP\RTD\ST\Libarary',
    'APP\os\FreeRTOS\include',
    'APP\os\FreeRTOS\portable'
)
$commonArgs = @(
    '--cpu', 'Cortex-M3',
    '--c99',
    '--apcs=interwork',
    '--split_sections',
    '--signed_chars',
    '-O0',
    '-g',
    '-DSTM32F10X_MD',
    '-DUSE_STDPERIPH_DRIVER'
)
foreach ($includeDir in $includeDirs) {
    $commonArgs += '-I' + (Join-Path $repoRoot $includeDir)
}

$startupSource = Join-Path $repoRoot 'APP\RTD\ST\Start\startup_stm32f10x_md.s'
$startupObject = Join-Path $buildDir 'startup_stm32f10x_md.o'
Invoke-Checked -FilePath $armasm `
    -Arguments @('--cpu', 'Cortex-M3', '--apcs=interwork', '-g',
                 '-I', (Split-Path -Parent $startupSource),
                 $startupSource, '-o', $startupObject) `
    -Label 'assemble startup_stm32f10x_md.s'

function Build-ReviewImage {
    param(
        [Parameter(Mandatory = $true)][string]$Suite,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Sources,
        [Parameter(Mandatory = $true)][string]$Scatter,
        [Parameter(Mandatory = $true)][string]$MapPath,
        [Parameter(Mandatory = $true)][string]$AxfPath
    )

    $objects = [System.Collections.Generic.List[string]]::new()
    $objects.Add($startupObject)
    foreach ($entry in $Sources.GetEnumerator()) {
        $source = Join-Path $repoRoot $entry.Value
        $object = Join-Path $buildDir ($Suite + '_' + $entry.Key + '.o')
        Invoke-Checked -FilePath $armcc `
            -Arguments ($commonArgs + @('-c', $source, '-o', $object)) `
            -Label ("compile $Suite " + $entry.Value)
        $objects.Add($object)
    }

    $linkArgs = @('--cpu', 'Cortex-M3', '--scatter', $Scatter, '--remove',
                  '--map', '--list', $MapPath, '--xref', '--symbols',
                  '--info', 'sizes,totals,unused,veneers')
    $linkArgs += $objects.ToArray()
    $linkArgs += @('-o', $AxfPath)
    Invoke-Checked -FilePath $armlink -Arguments $linkArgs `
        -Label "link $Suite review test image"

    $sizeOutput = & $fromelf --info=sizes,totals $AxfPath 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "fromelf $Suite size report failed with exit code $LASTEXITCODE"
    }
    $script:logLines.Add("SIZE REPORT: $Suite")
    foreach ($line in $sizeOutput) {
        $script:logLines.Add([string]$line)
    }
    $script:logLines.Add("$Suite TEST IMAGE BUILD: PASS")
}

$phase4Sources = [ordered]@{
    'bq76940' = 'firmware\Driver\bq76940.c'
    'bq76940_measurement' = 'firmware\Driver\bq76940_measurement.c'
    'crc8_bq76940' = 'firmware\Driver\crc8_bq76940.c'
    'test_mapping' = 'tests\test_phase4_mapping.c'
    'test_measurement' = 'tests\test_phase4_measurement.c'
    'test_main' = 'tests\test_phase4_main.c'
}
Build-ReviewImage -Suite 'phase4' -Sources $phase4Sources `
    -Scatter (Join-Path $PSScriptRoot 'phase4_tests.sct') `
    -MapPath $phase4Map -AxfPath $phase4Axf

$phase6Sources = [ordered]@{
    'app_rtos' = 'firmware\App\app_rtos.c'
    'app_rtos_hooks' = 'firmware\App\app_rtos_hooks.c'
    'tasks' = 'APP\os\FreeRTOS\source\tasks.c'
    'queue' = 'APP\os\FreeRTOS\source\queue.c'
    'list' = 'APP\os\FreeRTOS\source\list.c'
    'event_groups' = 'APP\os\FreeRTOS\source\event_groups.c'
    'timers' = 'APP\os\FreeRTOS\source\timers.c'
    'heap_4' = 'APP\os\FreeRTOS\portable\heap_4.c'
    'port' = 'APP\os\FreeRTOS\portable\port.c'
    'task_stub' = 'tests\test_phase6_task_stub.c'
    'test_objects' = 'tests\test_phase6_objects.c'
    'test_tasks' = 'tests\test_phase6_tasks.c'
    'test_main' = 'tests\test_phase6_main.c'
}
Build-ReviewImage -Suite 'phase6' -Sources $phase6Sources `
    -Scatter (Join-Path $PSScriptRoot 'phase6_tests.sct') `
    -MapPath $phase6Map -AxfPath $phase6Axf

$phase7Sources = [ordered]@{
    'bms_protect' = 'firmware\App\bms_protect.c'
    'bms_fault' = 'firmware\App\bms_fault.c'
    'bq76940_control' = 'firmware\Driver\bq76940_control.c'
    'test_stub' = 'tests\test_phase7_stub_i2c.c'
    'test_phase5_trip' = 'tests\test_phase5_trip.c'
    'test_phase5_ocdscd' = 'tests\test_phase5_ocdscd.c'
    'test_phase5_fet' = 'tests\test_phase5_fet.c'
    'test_phase5_cellbal' = 'tests\test_phase5_cellbal.c'
    'test_logic' = 'tests\test_phase7_logic.c'
    'test_main' = 'tests\test_phase7_main.c'
}
Build-ReviewImage -Suite 'phase7' -Sources $phase7Sources `
    -Scatter (Join-Path $PSScriptRoot 'phase7_tests.sct') `
    -MapPath $phase7Map -AxfPath $phase7Axf

$logLines | Set-Content -LiteralPath $buildLog -Encoding UTF8

if (-not $SkipProductionBuild) {
    if (Test-Path -LiteralPath $productionLog) {
        Remove-Item -LiteralPath $productionLog -Force
    }
    if (Test-Path -LiteralPath $productionMap) {
        Remove-Item -LiteralPath $productionMap -Force
    }
    $productionStartedUtc = [DateTime]::UtcNow
    $exitCode = Invoke-KeilPreservingUserOptions `
        -Arguments @('-cr', $project, '-t', 'BMS_V1', '-j0',
                     '-o', $productionLog)
    if ($exitCode -ne 0) {
        throw "Keil production rebuild failed with exit code $exitCode"
    }
    if (-not (Test-Path -LiteralPath $productionLog -PathType Leaf)) {
        throw 'Keil production rebuild did not create a new build log'
    }
    if ((Get-Item -LiteralPath $productionLog).LastWriteTimeUtc -lt
        $productionStartedUtc) {
        throw 'Keil production build log predates the current rebuild'
    }
    if (-not (Test-Path -LiteralPath $productionMap -PathType Leaf)) {
        throw 'Keil production rebuild did not create a new linker map'
    }
    if ((Get-Item -LiteralPath $productionMap).LastWriteTimeUtc -lt
        $productionStartedUtc) {
        throw 'Keil production linker map predates the current rebuild'
    }
    $productionText = Get-Content -LiteralPath $productionLog -Raw
    if ($productionText -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        throw 'Keil production build log does not contain 0 errors / 0 warnings'
    }
}

if (-not $SkipSimulator) {
    if (Test-Path -LiteralPath $simLog) {
        Remove-Item -LiteralPath $simLog -Force
    }
    $simulatorStartedUtc = [DateTime]::UtcNow
    $exitCode = Invoke-KeilPreservingUserOptions `
        -Arguments @('-d', $project, '-t', 'BMS_V1', '-j0')
    if ($exitCode -ne 0) {
        throw "Keil Simulator failed with exit code $exitCode"
    }
    if (-not (Test-Path -LiteralPath $simLog -PathType Leaf)) {
        throw 'Keil Simulator did not create the review log'
    }
    if ((Get-Item -LiteralPath $simLog).LastWriteTimeUtc -lt
        $simulatorStartedUtc) {
        throw 'Keil Simulator log predates the current simulator run'
    }
    $simText = Get-Content -LiteralPath $simLog -Raw
    foreach ($required in @(
        'PHASE4_REVIEW_TEST_COMPLETED=1',
        'PHASE4_REVIEW_TEST_FAILURES=0',
        'P4_MAPPING_FAILURES=0',
        'P4_MEASUREMENT_FAILURES=0',
        'P4_WRITE_COMMIT_FAILURES=0',
        'PHASE6_REVIEW_TEST_COMPLETED=1',
        'PHASE6_REVIEW_TEST_FAILURES=0',
        'P6_OBJECTS_FAILURES=0',
        'P6_TASKS_FAILURES=0',
        'P6_PROBE=4',
        'PHASE7_REVIEW_TEST_COMPLETED=1',
        'PHASE7_REVIEW_TEST_FAILURES=0',
        'P5_TRIP_FAILURES=0',
        'P5_OCDSCD_FAILURES=0',
        'P5_FET_FAILURES=0',
        'P5_CELLBAL_FAILURES=0',
        'P7_PROTECT_FAILURES=0',
        'P7_CC_FAILURES=0',
        'P7_RETRY_FAILURES=0',
        'P7_XREADY_FAILURES=0',
        'P7_BOUNDARY_FAILURES=0',
        'P7_PROBE=8'
    )) {
        if ($simText -notmatch [regex]::Escape($required)) {
            throw "Simulator evidence is missing: $required"
        }
    }
}

if (-not $SkipSimulator -and -not $SkipProductionBuild) {
    $verifier = Join-Path $PSScriptRoot 'verify_phase7_review.py'
    & $PythonPath $verifier
    if ($LASTEXITCODE -ne 0) {
        throw "Phase 7 review verifier failed with exit code $LASTEXITCODE"
    }
}

Write-Output 'Phase 7 review build and requested validation steps passed.'
