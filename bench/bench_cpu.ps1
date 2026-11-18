param(
    [Parameter(Mandatory = $true)][string]$Aesctr,
    [Parameter(Mandatory = $true)][string]$InputFile,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{32}$')][string]$Key,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{32}$')][string]$Counter,
    [ValidateRange(16, 2147483647)][int]$BufferBytes = 1048576,
    [ValidateRange(1, 100)][int]$Runs = 3
)

$exe = (Resolve-Path -LiteralPath $Aesctr).Path
$source = (Resolve-Path -LiteralPath $InputFile).Path
$fileBytes = (Get-Item -LiteralPath $source).Length
$tempOutput = Join-Path ([IO.Path]::GetTempPath()) ("aesctr-bench-{0}.bin" -f [guid]::NewGuid())

try {
    foreach ($mode in @('serial', 'omp')) {
        $samples = @()
        for ($run = 0; $run -lt $Runs; $run++) {
            $timer = [Diagnostics.Stopwatch]::StartNew()
            & $exe $source $tempOutput --key $Key --ctr $Counter --buf $BufferBytes --mode $mode
            if ($LASTEXITCODE -ne 0) { throw "aesctr failed in $mode mode (exit $LASTEXITCODE)" }
            $timer.Stop()
            $samples += $timer.Elapsed.TotalSeconds
        }
        $median = ($samples | Sort-Object)[$Runs -shr 1]
        [pscustomobject]@{
            Mode = $mode
            FileBytes = $fileBytes
            BufferBytes = $BufferBytes
            Runs = $Runs
            MedianSeconds = [math]::Round($median, 6)
            ThroughputMBps = if ($median -gt 0) { [math]::Round($fileBytes / $median / 1e6, 2) } else { 0 }
        }
    }
}
finally {
    Remove-Item -LiteralPath $tempOutput -Force -ErrorAction SilentlyContinue
}
