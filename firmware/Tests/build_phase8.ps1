param(
    [string]$ToolchainDir = 'D:\Keil_v5\ARM\Version5.06\bin',
    [string]$Uv4Path = 'D:\Keil_v5\UV4\UV4.exe',
    [string]$PythonPath = 'python',
    [switch]$SkipSimulator,
    [switch]$SkipProductionBuild
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDir = Join-Path $PSScriptRoot 'Build\Phase8'
$armcc = Join-Path $ToolchainDir 'armcc.exe'
$armasm = Join-Path $ToolchainDir 'armasm.exe'
$armlink = Join-Path $ToolchainDir 'armlink.exe'
$fromelf = Join-Path $ToolchainDir 'fromelf.exe'
$project = Join-Path $repoRoot 'firmware\Project\Keil\BMS_V1.uvprojx'
$userOptions = [System.IO.Path]::ChangeExtension($project, '.uvoptx')
$productionLog = Join-Path $repoRoot `
    'firmware\Project\Keil\Build\BMS_V1_Phase8_build.log'
$productionMap = Join-Path $repoRoot `
    'firmware\Project\Keil\Listings\BMS_V1.map'
$productionStackReport = Join-Path $repoRoot `
    'firmware\Project\Keil\Objects\BMS_V1.htm'
$productionMapEvidence = Join-Path $buildDir `
    'BMS_V1_Phase8_production.map'
$productionStackEvidence = Join-Path $buildDir `
    'BMS_V1_Phase8_production.callgraph.txt'
$productionMapPreserve = $productionMap + '.phase8-preserve.tmp'
$buildLog = Join-Path $buildDir 'phase8_build.log'
$simLog = Join-Path $buildDir 'phase8_simulator.log'
$verifyLog = Join-Path $buildDir 'verify_phase8.log'
$runStartedUtc = [DateTime]::UtcNow
$notExecutedExitCode = 3

$imageNames = @(
    'phase4_regression_tests',
    'phase6_regression_tests',
    'phase7_regression_tests',
    'phase8_data_tests',
    'phase8_sample_tests',
    'phase8_afe_tests'
)

foreach ($tool in @($armcc, $armasm, $armlink, $fromelf)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required ARMCC5 tool is missing: $tool"
    }
}
if ((-not $SkipSimulator -or -not $SkipProductionBuild) -and
    -not (Test-Path -LiteralPath $Uv4Path -PathType Leaf)) {
    throw "Required Keil executable is missing: $Uv4Path"
}
if (-not (Test-Path -LiteralPath $userOptions -PathType Leaf)) {
    throw "Required Keil user options are missing: $userOptions"
}
$userOptionsOriginalHash =
    (Get-FileHash -LiteralPath $userOptions `
     -Algorithm SHA256).Hash.ToLowerInvariant()

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
$resolvedBuildDir = (Resolve-Path -LiteralPath $buildDir).Path
if (-not $resolvedBuildDir.StartsWith($repoRoot,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use a build directory outside the repository"
}

$staleEvidence = [System.Collections.Generic.List[string]]::new()
$staleEvidence.Add($buildLog)
$staleEvidence.Add($simLog)
$staleEvidence.Add($verifyLog)
$staleEvidence.Add($productionMapEvidence)
$staleEvidence.Add($productionStackEvidence)
$staleEvidence.Add($productionLog)
foreach ($imageName in $imageNames) {
    $staleEvidence.Add((Join-Path $buildDir ($imageName + '.axf')))
    $staleEvidence.Add((Join-Path $buildDir ($imageName + '.map')))
}
foreach ($path in $staleEvidence) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

$logLines = [System.Collections.Generic.List[string]]::new()
$logLines.Add('BMS V1 Phase 8 reproducible hard-gate build')
$logLines.Add('Repository-relative input paths; absolute tool paths only')
$logLines.Add('RUN_STARTED_UTC=' + $runStartedUtc.ToString('o'))
$logLines.Add('ARMCC5_TOOL=' + $armcc)
$logLines.Add('UVISION_TOOL=' + $Uv4Path)
$logLines.Add('UVOPTX_ORIGINAL_SHA256 ' + $userOptionsOriginalHash +
              ' firmware/Project/Keil/BMS_V1.uvoptx')
if (Test-Path -LiteralPath $Uv4Path -PathType Leaf) {
    $logLines.Add('UVISION_FILE_VERSION=' +
                  (Get-Item -LiteralPath $Uv4Path).VersionInfo.FileVersion)
}

function Save-BuildLog {
    $script:logLines | Set-Content -LiteralPath $buildLog -Encoding UTF8
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $script:logLines.Add('RUN: ' + $Label)
    $output = & $FilePath @Arguments 2>&1
    if ($output) {
        foreach ($line in $output) {
            $script:logLines.Add([string]$line)
        }
    }
    if ($LASTEXITCODE -ne 0) {
        Save-BuildLog
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Invoke-KeilPreservingUserOptions {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$SimulatorInitRelativePath
    )

    $optionsBytes = [System.IO.File]::ReadAllBytes($userOptions)
    $optionsHash =
        (Get-FileHash -LiteralPath $userOptions `
         -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($optionsHash -ne $script:userOptionsOriginalHash) {
        throw 'Keil user options changed after the Phase 8 baseline hash'
    }
    $optionsLastWriteUtc =
        (Get-Item -LiteralPath $userOptions).LastWriteTimeUtc
    $patchApplied = $false
    try {
        if ($SimulatorInitRelativePath) {
            # ISO-8859-1 is used as a byte-preserving one-byte mapping. The
            # XML span and replacement are ASCII; every byte outside the
            # unique sIfile element is therefore copied bit-for-bit.
            $byteEncoding = [System.Text.Encoding]::GetEncoding(28591)
            $optionsText = $byteEncoding.GetString($optionsBytes)
            $matches = [regex]::Matches(
                $optionsText, '<sIfile>[^<]*</sIfile>')
            if ($matches.Count -ne 1) {
                throw 'Keil user options must contain exactly one sIfile'
            }
            $replacement = '<sIfile>' +
                $SimulatorInitRelativePath + '</sIfile>'
            $optionsText = [regex]::Replace(
                $optionsText, '<sIfile>[^<]*</sIfile>', $replacement)
            $patchedBytes = $byteEncoding.GetBytes($optionsText)
            [System.IO.File]::WriteAllBytes($userOptions, $patchedBytes)
            $patchedHash =
                (Get-FileHash -LiteralPath $userOptions `
                 -Algorithm SHA256).Hash.ToLowerInvariant()
            $script:logLines.Add(
                'UVOPTX_PATCH_TARGET_SIFILE ' +
                $SimulatorInitRelativePath)
            $script:logLines.Add(
                'UVOPTX_PATCH_MATCH_COUNT ' + $matches.Count)
            $script:logLines.Add(
                'UVOPTX_PATCHED_SHA256 ' + $patchedHash)
            $patchApplied = $true
        }

        $quotedArguments = foreach ($argument in $Arguments) {
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
        $argumentLine = $quotedArguments -join ' '
        $process = Start-Process -FilePath $Uv4Path `
            -ArgumentList $argumentLine -WindowStyle Hidden -Wait -PassThru
        return $process.ExitCode
    }
    finally {
        [System.IO.File]::WriteAllBytes($userOptions, $optionsBytes)
        [System.IO.File]::SetLastWriteTimeUtc(
            $userOptions, $optionsLastWriteUtc)
        $restoredHash =
            (Get-FileHash -LiteralPath $userOptions `
             -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($patchApplied) {
            $script:logLines.Add(
                'UVOPTX_RESTORED_SHA256 ' + $restoredHash)
            Save-BuildLog
        }
        if ($restoredHash -ne $script:userOptionsOriginalHash) {
            throw 'Keil user options byte-for-byte restoration failed'
        }
    }
}

foreach ($tool in @($armcc, $armasm, $armlink, $fromelf)) {
    Invoke-Checked -FilePath $tool -Arguments @('--vsn') `
        -Label ('record tool version: ' +
                [System.IO.Path]::GetFileName($tool))
}

$includeDirs = @(
    'firmware\App',
    'firmware\Config',
    'firmware\Driver',
    'firmware\Tests',
    'docs\reference\ST\STM32F10x Standard Peripheral Library',
    'docs\reference\ST\STM32F10x Standard Peripheral Library\Start',
    'docs\reference\ST\STM32F10x Standard Peripheral Library\Libarary',
    'docs\FreeRTOS\include',
    'docs\FreeRTOS\portable'
)
$commonArgs = @(
    '--cpu', 'Cortex-M3',
    '--diag_error=warning',
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

$startupSource = Join-Path $repoRoot `
    'docs\reference\ST\STM32F10x Standard Peripheral Library\Start\startup_stm32f10x_md.s'
$startupObject = Join-Path $buildDir 'startup_stm32f10x_md.o'
Invoke-Checked -FilePath $armasm `
    -Arguments @('--cpu', 'Cortex-M3', '--apcs=interwork', '-g',
                 '-I', (Split-Path -Parent $startupSource),
                 $startupSource, '-o', $startupObject) `
    -Label 'assemble startup_stm32f10x_md.s'

$phase4Sources = [ordered]@{
    'bq76940' = 'firmware\Driver\bq76940.c'
    'bq76940_measurement' = 'firmware\Driver\bq76940_measurement.c'
    'crc8_bq76940' = 'firmware\Driver\crc8_bq76940.c'
    'test_mapping' = 'firmware\Tests\test_phase4_mapping.c'
    'test_measurement' = 'firmware\Tests\test_phase4_measurement.c'
    'test_main' = 'firmware\Tests\test_phase4_main.c'
}
$phase6Sources = [ordered]@{
    'app_rtos' = 'firmware\App\app_rtos.c'
    'app_rtos_hooks' = 'firmware\App\app_rtos_hooks.c'
    'tasks' = 'docs\FreeRTOS\source\tasks.c'
    'queue' = 'docs\FreeRTOS\source\queue.c'
    'list' = 'docs\FreeRTOS\source\list.c'
    'event_groups' = 'docs\FreeRTOS\source\event_groups.c'
    'timers' = 'docs\FreeRTOS\source\timers.c'
    'heap_4' = 'docs\FreeRTOS\portable\heap_4.c'
    'port' = 'docs\FreeRTOS\portable\port.c'
    'task_stub' = 'firmware\Tests\test_phase6_task_stub.c'
    'test_objects' = 'firmware\Tests\test_phase6_objects.c'
    'test_tasks' = 'firmware\Tests\test_phase6_tasks.c'
    'test_main' = 'firmware\Tests\test_phase6_main.c'
}
$phase7Sources = [ordered]@{
    'bms_protect' = 'firmware\App\bms_protect.c'
    'bms_fault' = 'firmware\App\bms_fault.c'
    'bq76940_control' = 'firmware\Driver\bq76940_control.c'
    'test_stub' = 'firmware\Tests\test_phase7_stub_i2c.c'
    'test_phase5_trip' = 'firmware\Tests\test_phase5_trip.c'
    'test_phase5_ocdscd' = 'firmware\Tests\test_phase5_ocdscd.c'
    'test_phase5_fet' = 'firmware\Tests\test_phase5_fet.c'
    'test_phase5_cellbal' = 'firmware\Tests\test_phase5_cellbal.c'
    'test_logic' = 'firmware\Tests\test_phase7_logic.c'
    'test_main' = 'firmware\Tests\test_phase7_main.c'
}
$phase8DataSources = [ordered]@{
    'bms_data' = 'firmware\App\bms_data.c'
    'bms_ntc' = 'firmware\App\bms_ntc.c'
    'bms_fault' = 'firmware\App\bms_fault.c'
    'test_data' = 'firmware\Tests\test_phase8_data.c'
    'test_main' = 'firmware\Tests\test_phase8_main.c'
}
$phase8SampleSources = [ordered]@{
    'bms_data' = 'firmware\App\bms_data.c'
    'bms_ntc' = 'firmware\App\bms_ntc.c'
    'bms_fault' = 'firmware\App\bms_fault.c'
    'bms_sample' = 'firmware\App\bms_sample.c'
    'test_stub' = 'firmware\Tests\test_phase8_sample_stub.c'
    'test_sample' = 'firmware\Tests\test_phase8_sample.c'
    'test_main' = 'firmware\Tests\test_phase8_main.c'
}
$phase8AfeSources = [ordered]@{
    'bms_afe_startup' = 'firmware\App\bms_afe_startup.c'
    'bq76940_control' = 'firmware\Driver\bq76940_control.c'
    'test_afe' = 'firmware\Tests\test_phase8_afe_startup.c'
    'test_main' = 'firmware\Tests\test_phase8_main.c'
}

$allDeclaredInputs = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($sourceSet in @($phase4Sources, $phase6Sources, $phase7Sources,
                          $phase8DataSources, $phase8SampleSources,
                          $phase8AfeSources)) {
    foreach ($path in $sourceSet.Values) {
        [void]$allDeclaredInputs.Add(($path -replace '\\', '/'))
    }
}
foreach ($path in @(
    'firmware/Tests/build_phase8.ps1',
    'firmware/Tests/verify_phase8.py',
    'firmware/Tests/phase8_tests.sct',
    'firmware/Tests/phase8_simulator.ini',
    'firmware/Tests/test_phase8_data.h',
    'firmware/Tests/test_phase8_sample.h',
    'firmware/Tests/test_phase8_sample_stub.h',
    'firmware/App/bms_data.h',
    'firmware/App/bms_ntc.h',
    'firmware/App/bms_sample.h',
    'firmware/App/bms_afe_startup.h',
    'firmware/App/bms_protect.h',
    'firmware/Config/bms_config.h',
    'firmware/Config/FreeRTOSConfig.h',
    'firmware/Project/Keil/BMS_V1.uvprojx',
    'firmware/Project/Keil/BMS_V1.uvoptx',
    'firmware/User/main.c',
    'docs/reference/ST/STM32F10x Standard Peripheral Library/Start/startup_stm32f10x_md.s')) {
    [void]$allDeclaredInputs.Add($path)
}

# Manifest every production-project translation unit, not only the sources
# reused by the standalone test images.
[xml]$projectXml = Get-Content -LiteralPath $project -Raw
$projectDirectory = Split-Path -Parent $project
foreach ($fileNode in $projectXml.SelectNodes('//FilePath')) {
    $candidate = [System.IO.Path]::GetFullPath(
        (Join-Path $projectDirectory $fileNode.InnerText))
    $repoPrefix = $repoRoot.TrimEnd('\') + '\'
    if (-not $candidate.StartsWith(
            $repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        Save-BuildLog
        throw "Production source escapes repository: $candidate"
    }
    $relativePath = $candidate.Substring($repoPrefix.Length) `
        -replace '\\', '/'
    [void]$allDeclaredInputs.Add($relativePath)
}

# ARMCC dependency files are deliberately hashed as gate inputs. This makes
# concurrent edits to transitive application, driver, RTOS, CMSIS or test
# headers fail closed when verify_phase8.py recomputes the manifest.
$headerRoots = @(
    'firmware\App',
    'firmware\Config',
    'firmware\Driver',
    'firmware\Tests',
    'docs\FreeRTOS',
    'docs\reference\ST\STM32F10x Standard Peripheral Library'
)
foreach ($headerRoot in $headerRoots) {
    $absoluteHeaderRoot = Join-Path $repoRoot $headerRoot
    foreach ($header in Get-ChildItem -LiteralPath $absoluteHeaderRoot `
             -Filter '*.h' -File -Recurse) {
        $relativeHeader = $header.FullName.Substring(
            $repoRoot.TrimEnd('\').Length + 1) -replace '\\', '/'
        [void]$allDeclaredInputs.Add($relativeHeader)
    }
}
foreach ($relativePath in ($allDeclaredInputs | Sort-Object)) {
    $absolutePath = Join-Path $repoRoot ($relativePath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf)) {
        Save-BuildLog
        throw "Declared Phase 8 input is missing: $relativePath"
    }
    $hash = (Get-FileHash -LiteralPath $absolutePath `
             -Algorithm SHA256).Hash.ToLowerInvariant()
    $logLines.Add("INPUT_SHA256 $hash $relativePath")
}

function Build-TestImage {
    param(
        [Parameter(Mandatory = $true)][string]$Suite,
        [Parameter(Mandatory = $true)]
        [System.Collections.IDictionary]$Sources,
        [string[]]$LanguageArgs = @('--c99'),
        [string[]]$ExtraDefines = @()
    )

    $objects = [System.Collections.Generic.List[string]]::new()
    $objects.Add($startupObject)
    $compileArgs = [System.Collections.Generic.List[string]]::new()
    foreach ($argument in $LanguageArgs) {
        $compileArgs.Add($argument)
    }
    foreach ($argument in $commonArgs) {
        $compileArgs.Add($argument)
    }
    foreach ($define in $ExtraDefines) {
        $compileArgs.Add('-D' + $define)
    }
    $script:logLines.Add(
        "LANGUAGE_MODE $Suite " + ($LanguageArgs -join ' '))

    foreach ($entry in $Sources.GetEnumerator()) {
        $source = Join-Path $repoRoot $entry.Value
        $object = Join-Path $buildDir ($Suite + '_' + $entry.Key + '.o')
        Invoke-Checked -FilePath $armcc `
            -Arguments ($compileArgs.ToArray() +
                        @('-c', $source, '-o', $object)) `
            -Label ("compile $Suite " +
                    ($entry.Value -replace '\\', '/'))
        $objects.Add($object)
    }

    $mapPath = Join-Path $buildDir ($Suite + '.map')
    $axfPath = Join-Path $buildDir ($Suite + '.axf')
    $scatter = Join-Path $PSScriptRoot 'phase8_tests.sct'
    $linkArgs = @('--cpu', 'Cortex-M3', '--scatter', $scatter,
                  '--remove', '--map', '--list', $mapPath,
                  '--xref', '--symbols', '--info',
                  'sizes,totals,unused,veneers')
    $linkArgs += $objects.ToArray()
    $linkArgs += @('-o', $axfPath)
    Invoke-Checked -FilePath $armlink -Arguments $linkArgs `
        -Label "link $Suite test image"

    $sizeOutput = & $fromelf --info=sizes,totals $axfPath 2>&1
    if ($LASTEXITCODE -ne 0) {
        Save-BuildLog
        throw "fromelf $Suite size report failed"
    }
    $script:logLines.Add("SIZE_REPORT_BEGIN $Suite")
    foreach ($line in $sizeOutput) {
        $script:logLines.Add([string]$line)
    }
    $script:logLines.Add("SIZE_REPORT_END $Suite")
    $script:logLines.Add("TEST_IMAGE_BUILD_PASS $Suite")
}

Build-TestImage -Suite 'phase4_regression_tests' -Sources $phase4Sources
Build-TestImage -Suite 'phase6_regression_tests' -Sources $phase6Sources
Build-TestImage -Suite 'phase7_regression_tests' -Sources $phase7Sources
Build-TestImage -Suite 'phase8_data_tests' -Sources $phase8DataSources `
    -LanguageArgs @('--c90') `
    -ExtraDefines @('TEST_PHASE8_DATA_IMAGE')
Build-TestImage -Suite 'phase8_sample_tests' -Sources $phase8SampleSources `
    -LanguageArgs @('--c90') `
    -ExtraDefines @('TEST_PHASE8_SAMPLE_IMAGE')
Build-TestImage -Suite 'phase8_afe_tests' -Sources $phase8AfeSources `
    -LanguageArgs @('--c90') `
    -ExtraDefines @('TEST_PHASE8_AFE_IMAGE')
Save-BuildLog

if (-not $SkipProductionBuild) {
    if (Test-Path -LiteralPath $productionMapPreserve) {
        throw "Unresolved production-map preserve file exists: $productionMapPreserve"
    }
    $mapExisted = Test-Path -LiteralPath $productionMap -PathType Leaf
    if ($mapExisted) {
        Copy-Item -LiteralPath $productionMap `
            -Destination $productionMapPreserve
        $mapOriginalLastWriteUtc =
            (Get-Item -LiteralPath $productionMap).LastWriteTimeUtc
    }
    try {
        if (Test-Path -LiteralPath $productionLog) {
            Remove-Item -LiteralPath $productionLog -Force
        }
        $productionStartedUtc = [DateTime]::UtcNow
        $exitCode = Invoke-KeilPreservingUserOptions `
            -Arguments @('-cr', $project, '-t', 'BMS_V1', '-j0',
                         '-o', $productionLog)
        if ($exitCode -ne 0) {
            throw "Keil production rebuild failed with exit code $exitCode"
        }
        foreach ($path in @($productionLog, $productionMap,
                             $productionStackReport)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Production rebuild did not create: $path"
            }
            if ((Get-Item -LiteralPath $path).LastWriteTimeUtc -lt
                $productionStartedUtc) {
                throw "Production evidence predates this rebuild: $path"
            }
        }
        $productionText = Get-Content -LiteralPath $productionLog -Raw
        if ($productionText -notmatch '0 Error\(s\), 0 Warning\(s\)') {
            throw 'Production rebuild was not 0 errors / 0 warnings'
        }
        Copy-Item -LiteralPath $productionMap `
            -Destination $productionMapEvidence
        Copy-Item -LiteralPath $productionStackReport `
            -Destination $productionStackEvidence
        $productionStackHash =
            (Get-FileHash -LiteralPath $productionStackEvidence `
             -Algorithm SHA256).Hash.ToLowerInvariant()
        $logLines.Add(
            'PRODUCTION_CALLGRAPH_SOURCE ' +
            'firmware/Project/Keil/Objects/BMS_V1.htm')
        $logLines.Add(
            'PRODUCTION_CALLGRAPH_SHA256 ' + $productionStackHash + ' ' +
            'firmware/Tests/Build/Phase8/' +
            'BMS_V1_Phase8_production.callgraph.txt')
        $productionStackText =
            Get-Content -LiteralPath $productionStackEvidence -Raw
        $stackRegexOptions =
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
            [System.Text.RegularExpressions.RegexOptions]::Singleline
        $taskStackBlocks = [regex]::Matches(
            $productionStackText,
            '<P><STRONG><a name="[^"]+"></a>Task_Sample</STRONG>\s*' +
            '\(Thumb,.*?bms_sample\.o\(i\.Task_Sample\)\)' +
            '(.*?)(?=<P><STRONG>|</BODY>)',
            $stackRegexOptions)
        if ($taskStackBlocks.Count -ne 1) {
            throw 'Production callgraph must contain one Task_Sample block'
        }
        $taskStackDepth = [regex]::Matches(
            $taskStackBlocks[0].Groups[1].Value,
            'Max Depth\s*=\s*(\d+)(?:\s*\+\s*Unknown)?',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($taskStackDepth.Count -ne 1) {
            throw 'Production Task_Sample must have one Max Depth record'
        }
        $taskStackUnknown = [regex]::IsMatch(
            $taskStackBlocks[0].Groups[1].Value,
            '\+\s*Unknown',
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        $logLines.Add(
            'PRODUCTION_TASK_SAMPLE_MAX_DEPTH_BYTES ' +
            $taskStackDepth[0].Groups[1].Value)
        $logLines.Add(
            'PRODUCTION_TASK_SAMPLE_UNKNOWN ' +
            $(if ($taskStackUnknown) { '1' } else { '0' }))
    }
    finally {
        if ($mapExisted -and
            (Test-Path -LiteralPath $productionMapPreserve -PathType Leaf)) {
            Copy-Item -LiteralPath $productionMapPreserve `
                -Destination $productionMap -Force
            [System.IO.File]::SetLastWriteTimeUtc(
                $productionMap, $mapOriginalLastWriteUtc)
            Remove-Item -LiteralPath $productionMapPreserve -Force
        }
        elseif (-not $mapExisted -and
                (Test-Path -LiteralPath $productionMap -PathType Leaf)) {
            Remove-Item -LiteralPath $productionMap -Force
        }
    }
}

if (-not $SkipSimulator) {
    $simulatorStartedUtc = [DateTime]::UtcNow
    $exitCode = Invoke-KeilPreservingUserOptions `
        -Arguments @('-d', $project, '-t', 'BMS_V1', '-j0') `
        -SimulatorInitRelativePath '..\..\Tests\phase8_simulator.ini'
    if ($exitCode -ne 0) {
        throw "Keil Simulator failed with exit code $exitCode"
    }
    if (-not (Test-Path -LiteralPath $simLog -PathType Leaf)) {
        throw 'Keil Simulator did not create the Phase 8 log'
    }
    if ((Get-Item -LiteralPath $simLog).LastWriteTimeUtc -lt
        $simulatorStartedUtc) {
        throw 'Phase 8 Simulator log predates the current run'
    }
}

if ($SkipSimulator -or $SkipProductionBuild) {
    $skippedStages = [System.Collections.Generic.List[string]]::new()
    if ($SkipSimulator) {
        $skippedStages.Add('Simulator')
    }
    if ($SkipProductionBuild) {
        $skippedStages.Add('ProductionBuild')
    }
    $notExecuted =
        'RESULT: PHASE8 HARD GATE NOT_EXECUTED skipped=' +
        ($skippedStages -join ',')
    $logLines.Add($notExecuted)
    Save-BuildLog
    Write-Output $notExecuted
    exit $notExecutedExitCode
}

$verifier = Join-Path $PSScriptRoot 'verify_phase8.py'
& $PythonPath $verifier --started-utc $runStartedUtc.ToString('o')
$verifyExitCode = $LASTEXITCODE
if ($verifyExitCode -eq 1) {
    throw 'Phase 8 verifier reported FAIL'
}
if ($verifyExitCode -eq 2) {
    Write-Output 'Phase 8 tests/evidence PASS; HARD GATE BLOCKED.'
    exit 2
}
if ($verifyExitCode -ne 0) {
    throw "Phase 8 verifier returned unexpected code $verifyExitCode"
}

Write-Output 'PHASE 8 HARD GATE PASS'
