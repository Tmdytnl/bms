param(
    [string]$Uv4Path = 'D:\Keil_v5\UV4\UV4.exe'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$project = Join-Path $repoRoot 'firmware\Project\Keil\BMS_V1.uvprojx'
$userOptions = [IO.Path]::ChangeExtension($project, '.uvoptx')
$buildDir = Join-Path $PSScriptRoot 'Build\Phase8'
$summaryLog = Join-Path $buildDir 'phase8_split_simulator.log'

$suites = @(
    @{ Name='phase4'; Image='phase4_regression_tests.axf';
       Markers=@('PHASE4_REGRESSION_COMPLETED=1','PHASE4_REGRESSION_FAILURES=0') },
    @{ Name='phase6'; Image='phase6_regression_tests.axf';
       Markers=@('PHASE6_REGRESSION_COMPLETED=1','PHASE6_REGRESSION_FAILURES=0') },
    @{ Name='phase7'; Image='phase7_regression_tests.axf';
       Markers=@('PHASE7_REGRESSION_COMPLETED=1','PHASE7_REGRESSION_FAILURES=0','P7_SIM_COMM_FAILURES=0') },
    @{ Name='phase8_data'; Image='phase8_data_tests.axf';
       Markers=@('PHASE8_DATA_TEST_COMPLETED=1','PHASE8_DATA_TEST_FAILURES=0') },
    @{ Name='phase8_sample'; Image='phase8_sample_tests.axf';
       Markers=@('PHASE8_SAMPLE_TEST_COMPLETED=1','PHASE8_SAMPLE_TEST_FAILURES=0','P8_SAMPLE_PROVENANCE_GUARD_COMPLETED=1') },
    @{ Name='phase8_afe'; Image='phase8_afe_tests.axf';
       Markers=@('PHASE8_AFE_TEST_COMPLETED=1','PHASE8_AFE_TEST_FAILURES=0') }
)

$expressions = @{
    phase4 = @('g_phase4_test_completed','g_phase4_test_failures')
    phase6 = @('g_phase6_test_completed','g_phase6_test_failures')
    phase7 = @('g_phase7_test_completed','g_phase7_test_failures','g_p7_sim_comm_failures')
    phase8_data = @('g_phase8_test_completed','g_phase8_test_failures')
    phase8_sample = @('g_phase8_test_completed','g_phase8_test_failures','g_phase8_sample_provenance_guard_completed')
    phase8_afe = @('g_phase8_test_completed','g_phase8_test_failures')
}

$labels = @{
    phase4 = @('PHASE4_REGRESSION_COMPLETED','PHASE4_REGRESSION_FAILURES')
    phase6 = @('PHASE6_REGRESSION_COMPLETED','PHASE6_REGRESSION_FAILURES')
    phase7 = @('PHASE7_REGRESSION_COMPLETED','PHASE7_REGRESSION_FAILURES','P7_SIM_COMM_FAILURES')
    phase8_data = @('PHASE8_DATA_TEST_COMPLETED','PHASE8_DATA_TEST_FAILURES')
    phase8_sample = @('PHASE8_SAMPLE_TEST_COMPLETED','PHASE8_SAMPLE_TEST_FAILURES','P8_SAMPLE_PROVENANCE_GUARD_COMPLETED')
    phase8_afe = @('PHASE8_AFE_TEST_COMPLETED','PHASE8_AFE_TEST_FAILURES')
}

$summary = [Collections.Generic.List[string]]::new()
$summary.Add('BMS V1 split ARMCC5 Simulator regression')
$summary.Add('HARDWARE_CLAIM=NONE')

foreach ($suite in $suites) {
    $iniName = 'split_' + $suite.Name + '.ini'
    $iniPath = Join-Path $buildDir $iniName
    $logName = 'split_' + $suite.Name + '.log'
    $logPath = Join-Path $buildDir $logName
    $relativeIni = '..\..\Tests\Build\Phase8\' + $iniName
    $relativeImage = '..\..\Tests\Build\Phase8\' + $suite.Image
    $relativeLog = '..\..\Tests\Build\Phase8\' + $logName
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('LOG > ' + $relativeLog)
    $lines.Add('LOAD ' + $relativeImage)
    $lines.Add('RESET')
    $lines.Add('G')
    for ($i = 0; $i -lt $expressions[$suite.Name].Count; ++$i) {
        $label = $labels[$suite.Name][$i]
        $expression = $expressions[$suite.Name][$i]
        $lines.Add('printf("' + $label + '=%u\n", ' + $expression + ')')
    }
    $lines.Add('LOG OFF')
    $lines.Add('EXIT')
    [IO.File]::WriteAllLines($iniPath, $lines, [Text.Encoding]::ASCII)
    if (Test-Path -LiteralPath $logPath) {
        Remove-Item -LiteralPath $logPath -Force
    }

    $optionBytes = [IO.File]::ReadAllBytes($userOptions)
    $optionTime = (Get-Item -LiteralPath $userOptions).LastWriteTimeUtc
    try {
        $encoding = [Text.Encoding]::GetEncoding(28591)
        $text = $encoding.GetString($optionBytes)
        if ([regex]::Matches($text, '<sIfile>[^<]*</sIfile>').Count -ne 1) {
            throw 'Keil options must contain exactly one Simulator init'
        }
        $text = [regex]::Replace($text, '<sIfile>[^<]*</sIfile>',
            '<sIfile>' + $relativeIni + '</sIfile>')
        [IO.File]::WriteAllBytes($userOptions, $encoding.GetBytes($text))
        $arguments = '-d "' + $project + '" -t BMS_V1 -j0'
        $process = Start-Process -FilePath $Uv4Path `
            -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if (-not $process.WaitForExit(20000)) {
            Stop-Process -Id $process.Id -Force
            throw "Keil Simulator timeout: $($suite.Name)"
        }
        if ($process.ExitCode -ne 0) {
            throw "Keil Simulator failed: $($suite.Name)"
        }
    }
    finally {
        [IO.File]::WriteAllBytes($userOptions, $optionBytes)
        [IO.File]::SetLastWriteTimeUtc($userOptions, $optionTime)
        if (Test-Path -LiteralPath $iniPath) {
            Remove-Item -LiteralPath $iniPath -Force
        }
    }
    $text = Get-Content -LiteralPath $logPath -Raw
    foreach ($marker in $suite.Markers) {
        $exactLine = '(?m)^' + [regex]::Escape($marker) + '\r?$'
        if ($text -notmatch $exactLine) {
            throw "Missing $marker in $($suite.Name)"
        }
        $summary.Add($marker)
    }
}

$summary.Add('PHASE8_SPLIT_SIMULATOR_REGRESSION_PASS')
$summary | Set-Content -LiteralPath $summaryLog -Encoding UTF8
Write-Output 'PHASE8 SPLIT SIMULATOR REGRESSION PASS'
