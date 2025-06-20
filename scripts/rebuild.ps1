$targetPath = "C:\Program Files\Tencent\QQNT-dev\QQ.exe"

#  kill the process if it is running
foreach ($process in Get-Process -ErrorAction SilentlyContinue) {
    if ($process.Path -eq $targetPath) {
        Stop-Process -Id $process.Id -Force || Write-Host "Failed to stop process: $($process.Id) - $($process.Name)"
        Write-Host "Killed process: $($process.Id) - $($process.Name)"
    }
}

xmake b chromatic
Start-Process -FilePath $targetPath -WorkingDirectory "C:\Program Files\Tencent\QQNT-dev" -NoNewWindow