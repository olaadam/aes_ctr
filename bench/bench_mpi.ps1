param(
    [Parameter(Mandatory = $true)][string]$Mpiexec,
    [Parameter(Mandatory = $true)][string]$AesctrMpi,
    [Parameter(Mandatory = $true)][string]$InputFile,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{32}$')][string]$Key,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-fA-F]{32}$')][string]$Counter,
    [int[]]$Ranks = @(1, 2, 4, 8),
    [ValidateRange(16, 2147483647)][int[]]$BufferBytes = @(65536, 1048576, 4194304),
    [ValidateSet('serial', 'omp')][string]$Mode = 'serial',
    [ValidateRange(1, 100)][int]$Runs = 3,
    [string]$OutputCsv
)

$launcher = (Resolve-Path -LiteralPath $Mpiexec).Path
$exe = (Resolve-Path -LiteralPath $AesctrMpi).Path
$source = (Resolve-Path -LiteralPath $InputFile).Path
$fileBytes = (Get-Item -LiteralPath $source).Length
$tempOutput = Join-Path ([IO.Path]::GetTempPath()) ("aesctr-mpi-bench-{0}.bin" -f [guid]::NewGuid())
$results = @()

function Get-Median([double[]]$Values) {
    $ordered = @($Values | Sort-Object)
    $middle = [int][math]::Floor($ordered.Count / 2)
    if (($ordered.Count % 2) -eq 1) { return $ordered[$middle] }
    return ($ordered[$middle - 1] + $ordered[$middle]) / 2
}

try {
    foreach ($rankCount in $Ranks) {
        if ($rankCount -lt 1) { throw "Rank count must be positive" }
        foreach ($buffer in $BufferBytes) {
            if (($buffer -band 15) -ne 0) { throw "Buffer size $buffer must be divisible by 16" }
            $samples = @()
            for ($run = 0; $run -lt $Runs; $run++) {
                $lines = @(& $launcher -n $rankCount $exe $source $tempOutput `
                    --key $Key --ctr $Counter --buf $buffer --mode $Mode 2>&1)
                if ($LASTEXITCODE -ne 0) { throw "MPI run failed ($LASTEXITCODE): $($lines -join [Environment]::NewLine)" }
                $text = $lines -join [Environment]::NewLine
                $distMatch = [regex]::Match($text, 'Config distribution \(MPI_Bcast\): ([0-9.]+) s')
                $pipeMatch = [regex]::Match($text, 'Pipeline max-rank time: ([0-9.]+) s \(([0-9.]+) GB/s\)')
                $stageMatch = [regex]::Match($text, 'Max-rank stages: read ([0-9.]+) s, crypto ([0-9.]+) s, write ([0-9.]+) s')
                if (!$distMatch.Success -or !$pipeMatch.Success -or !$stageMatch.Success) {
                    throw "Could not parse benchmark output: $text"
                }
                $samples += [pscustomobject]@{
                    DistributionSeconds = [double]$distMatch.Groups[1].Value
                    PipelineSeconds = [double]$pipeMatch.Groups[1].Value
                    ThroughputGBps = [double]$pipeMatch.Groups[2].Value
                    ReadSeconds = [double]$stageMatch.Groups[1].Value
                    CryptoSeconds = [double]$stageMatch.Groups[2].Value
                    WriteSeconds = [double]$stageMatch.Groups[3].Value
                }
            }
            $results += [pscustomobject]@{
                Ranks = $rankCount
                Mode = $Mode
                FileBytes = $fileBytes
                BufferBytes = $buffer
                Runs = $Runs
                MedianPipelineSeconds = (Get-Median $samples.PipelineSeconds)
                MedianThroughputGBps = [math]::Round($fileBytes / (Get-Median $samples.PipelineSeconds) / 1e9, 4)
                MedianConfigDistributionSeconds = (Get-Median $samples.DistributionSeconds)
                MedianReadSeconds = (Get-Median $samples.ReadSeconds)
                MedianCryptoSeconds = (Get-Median $samples.CryptoSeconds)
                MedianWriteSeconds = (Get-Median $samples.WriteSeconds)
            }
        }
    }
    $results | Format-Table -AutoSize
    if ($OutputCsv) { $results | Export-Csv -LiteralPath $OutputCsv -NoTypeInformation -Encoding utf8 }
}
finally {
    Remove-Item -LiteralPath $tempOutput -Force -ErrorAction SilentlyContinue
}
