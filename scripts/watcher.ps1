function watcher {
    param(
        [Parameter(Position=0)]
        [string]$Path,
        [int]$Lines = 50,
        [string]$ProcessName = ""
    )

    if (-not $Path) {
        $Path = Get-Location
    }

    $OriginalDirectory = $Path
    if (-not (Test-Path $Path -PathType Container)) {
        $OriginalDirectory = Split-Path $Path -Parent
    }

    function Get-NewestFile {
        param([string]$Directory)

        $newest = Get-ChildItem -Path $Directory -File |
                  Sort-Object LastWriteTime -Descending |
                  Select-Object -First 1
        if (-not $newest) {
            Write-Error "No files found in directory: $Directory"
            return $null
        }
        return $newest.FullName
    }

    if (Test-Path $Path -PathType Container) {
        $Path = Get-NewestFile -Directory $Path
        if (-not $Path) { return }
    }

    Clear-Host
    Write-Host "Monitoring file: $Path" -ForegroundColor Cyan

    if ($ProcessName) {
        Write-Host "Monitoring process: $ProcessName" -ForegroundColor Cyan
        Write-Host ("-" * 80) -ForegroundColor Gray

        $keepRunning = $true

        while ($keepRunning) {
            $fileJob = Start-Job -ScriptBlock {
                param($FilePath, $TailLines)
                Get-Content -Path $FilePath -Wait -Tail $TailLines
            } -ArgumentList $Path, $Lines

            try {
                $lastCpuTime = 0
                $lastTime = Get-Date
                $firstRun = $true
                $processLost = $false

                while ($true) {
                    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

                    if (-not $proc) {
                        if (-not $processLost) {
                            Write-Host "`n[$ProcessName] Process not found. Waiting 10 seconds for final log messages..." -ForegroundColor Yellow
                            Start-Sleep -Seconds 10
                            $finalOutput = Receive-Job -Job $fileJob
                            if ($finalOutput) {
                                Write-Host "`nFinal log entries:" -ForegroundColor Cyan
                                $finalOutput | ForEach-Object { Write-Host $_ }
                            }
                            $processLost = $true
                            $lostTime = Get-Date
                            Write-Host "`n[$ProcessName] Now waiting 5 minutes for reconnection..." -ForegroundColor Yellow
                        }

                        $elapsedSeconds = ((Get-Date) - $lostTime).TotalSeconds
                        $remainingSeconds = 300 - $elapsedSeconds

                        if ($remainingSeconds -le 0) {
                            Write-Host "[$ProcessName] Process did not return within 5 minutes. Exiting watcher." -ForegroundColor Red
                            $keepRunning = $false
                            break
                        }

                        $remainingMinutes = [math]::Floor($remainingSeconds / 60)
                        $remainingSecondsDisplay = [math]::Floor($remainingSeconds % 60)
                        $procInfo = "[$ProcessName] Waiting for process... Time remaining: ${remainingMinutes}m ${remainingSecondsDisplay}s"
                        $host.UI.RawUI.WindowTitle = $procInfo

                        $padding = ' ' * [Math]::Max(0, 120 - $procInfo.Length)
                        Write-Host "`r$procInfo$padding" -NoNewline -ForegroundColor Yellow

                        Start-Sleep -Seconds 1
                        continue
                    }

                    # Process found
                    if ($processLost) {
                        Write-Host "`n[$ProcessName] Process reconnected! Waiting 5 seconds for new log file creation..." -ForegroundColor Green
                        Start-Sleep -Seconds 5

                        $NewPath = Get-NewestFile -Directory $OriginalDirectory
                        if ($NewPath -and $NewPath -ne $Path) {
                            $Path = $NewPath
                            Write-Host "Now monitoring file: $Path" -ForegroundColor Cyan
                        } else {
                            Write-Host "Continuing with same file: $Path" -ForegroundColor Cyan
                        }

                        Stop-Job -Job $fileJob -ErrorAction SilentlyContinue
                        Remove-Job -Job $fileJob -ErrorAction SilentlyContinue
                        break
                    }

                    $currentTime = Get-Date
                    $currentCpuTime = $proc.CPU

                    if (-not $firstRun) {
                        $timeDiff = ($currentTime - $lastTime).TotalSeconds
                        $cpuDiff = $currentCpuTime - $lastCpuTime
                        $cpuPercent = [math]::Round(($cpuDiff / $timeDiff) * 100 / $env:NUMBER_OF_PROCESSORS, 1)
                    } else {
                        $cpuPercent = 0
                        $firstRun = $false
                    }

                    $memMB = [math]::Round($proc.WorkingSet64 / 1MB, 2)
                    $threads = $proc.Threads.Count

                    $lastCpuTime = $currentCpuTime
                    $lastTime = $currentTime

                    $procInfo = "[$ProcessName] CPU: ${cpuPercent}% | Memory: ${memMB} MB | Threads: $threads"

                    $host.UI.RawUI.WindowTitle = $procInfo

                    $jobOutput = Receive-Job -Job $fileJob
                    if ($jobOutput) {
                        Write-Host "`r$(' ' * $host.UI.RawUI.WindowSize.Width)" -NoNewline
                        Write-Host "`r" -NoNewline
                        $jobOutput | ForEach-Object { Write-Host $_ }
                        Write-Host $procInfo -NoNewline -ForegroundColor Yellow
                    } else {
                        $padding = ' ' * [Math]::Max(0, 120 - $procInfo.Length)
                        Write-Host "`r$procInfo$padding" -NoNewline -ForegroundColor Yellow
                    }

                    Start-Sleep -Seconds 1
                }
            }
            finally {
                Stop-Job -Job $fileJob -ErrorAction SilentlyContinue
                Remove-Job -Job $fileJob -ErrorAction SilentlyContinue
            }
        }
    }
    else {
        # Just watch the file
        Get-Content -Path $Path -Wait -Tail $Lines
    }
}