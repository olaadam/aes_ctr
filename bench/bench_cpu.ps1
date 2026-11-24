param(
    [Parameter(Mandatory = $true)][string]$Aesctr,
    [Parameter(Mandatory = $true)][string[]]$InputFile,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{32}$')][string]$Key,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{32}$')][string]$Counter,
    [ValidateRange(16, 2147483647)][int[]]$BufferBytes = @(4096, 65536, 262144, 1048576, 4194304),
    [ValidateRange(1, 1024)][int[]]$ThreadCounts = @(1, 2, 4, 8),
    [ValidateRange(1, 100)][int]$Runs = 5,
    [string]$OutputCsv
)

$exe = (Resolve-Path -LiteralPath $Aesctr).Path
$tempOutput = Join-Path ([IO.Path]::GetTempPath()) ("aesctr-bench-{0}.bin" -f [guid]::NewGuid())
$oldThreadCount = $env:OMP_NUM_THREADS

try {
    $results = @()
    foreach ($file in $InputFile) {
        $source = (Resolve-Path -LiteralPath $file).Path
        $fileBytes = (Get-Item -LiteralPath $source).Length
        foreach ($mode in @('serial', 'omp')) {
            $threadsToRun = if ($mode -eq 'omp') { $ThreadCounts } else { @(1) }
            foreach ($threads in $threadsToRun) {
                foreach ($buffer in $BufferBytes) {
                    if (($buffer -band 15) -ne 0) { throw "Buffer size $buffer must be divisible by 16" }
                    $env:OMP_NUM_THREADS = [string]$threads
                    $samples = @()
                    for ($run = 0; $run -lt $Runs; $run++) {
                        $timer = [Diagnostics.Stopwatch]::StartNew()
                        & $exe $source $tempOutput --key $Key --ctr $Counter --buf $buffer --mode $mode
                        if ($LASTEXITCODE -ne 0) { throw "aesctr failed in $mode mode (exit $LASTEXITCODE)" }
                        $timer.Stop()
                        $samples += $timer.Elapsed.TotalSeconds
                    }
                    $sorted = @($samples | Sort-Object)
                    if (($Runs % 2) -eq 1) { $median = $sorted[[int]($Runs / 2)] }
                    else {
                        $upper = [int]($Runs / 2)
                        $median = ($sorted[$upper - 1] + $sorted[$upper]) / 2
                    }
                    $results += [pscustomobject]@{
                        Mode = $mode
                        Threads = $threads
                        InputFile = $source
                        FileBytes = $fileBytes
                        BufferBytes = $buffer
                        Runs = $Runs
                        MedianSeconds = [math]::Round($median, 6)
                        ThroughputGBps = if ($median -gt 0) { [math]::Round($fileBytes / $median / 1e9, 4) } else { 0 }
                    }
                }
            }
        }
    }
    $results | Format-Table -AutoSize
    if ($OutputCsv) { $results | Export-Csv -LiteralPath $OutputCsv -NoTypeInformation -Encoding utf8 }
}
finally {
    $env:OMP_NUM_THREADS = $oldThreadCount
    Remove-Item -LiteralPath $tempOutput -Force -ErrorAction SilentlyContinue
}
