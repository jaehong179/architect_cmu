<#
  resource_sample.ps1 — Windows 자원(CPU/메모리) 외부 샘플러 (resource_sample.sh 의 Windows판)

  자원은 앱 밖에서 잰다(관측자 효과 회피). Windows 에서는 PowerShell 표준 cmdlet 만 쓴다(설치 불필요).
  ※ 플랫폼 차이:
     - PSS 는 리눅스 smaps 개념 → Windows 엔 없음. 대신 Working Set(=RSS 상당)·Private Bytes(=누수 분석용) 기록.
       누수(증가 추세)는 private_mb(Private Bytes)가 가장 깨끗(앱 고유 커밋 메모리).
     - 발열/스로틀(temp/throttled)은 Pi 전용 → Windows CSV 엔 없음.

  사용:
    powershell -ExecutionPolicy Bypass -File tools\resource_sample.ps1
    powershell -ExecutionPolicy Bypass -File tools\resource_sample.ps1 -Name TimeGrapher -Interval 1 -Out resource_ext.csv

  결과 CSV: epoch_s,cpu_percent,working_set_mb,private_mb     (Ctrl+C 로 종료)
  분석: tools\perf_join.py / analyze_perf.py 가 Windows 컬럼도 인식한다.
#>
param(
  [string]$Name = "TimeGrapher",
  [int]   $Interval = 1,
  [string]$Out = "resource_ext.csv",
  [int]   $ProcId = 0
)
$ErrorActionPreference = "Stop"

# 대상 프로세스
if ($ProcId -gt 0) { $proc = Get-Process -Id $ProcId } else { $proc = Get-Process -Name $Name -ErrorAction SilentlyContinue | Select-Object -First 1 }
if (-not $proc) { Write-Error "프로세스 '$Name' 를 찾을 수 없습니다. 앱을 먼저 실행하세요 (또는 -ProcId)."; exit 1 }
$pid0 = $proc.Id
$cores = [Environment]::ProcessorCount

Write-Host "==> 대상  : $Name (PID $pid0)"
Write-Host "==> 지표  : CPU% + Working Set + Private Bytes (Windows — PSS/발열 없음)"
Write-Host "==> 주기  : ${Interval}s   출력: $Out   (Ctrl+C 로 종료)"
"epoch_s,cpu_percent,working_set_mb,private_mb" | Out-File -FilePath $Out -Encoding ascii

$prevCpu = $null; $prevWall = $null
while ($true) {
  try { $p = Get-Process -Id $pid0 -ErrorAction Stop } catch { Write-Host "`n==> 프로세스 종료됨."; break }
  $nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
  $epoch = [math]::Round($nowMs / 1000.0, 3)
  $cpuSec = $p.TotalProcessorTime.TotalSeconds
  $ws = [math]::Round($p.WorkingSet64 / 1MB, 1)
  $pv = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)

  $cpuPct = ""
  if ($prevCpu -ne $null -and $nowMs -gt $prevWall) {
    $dProc = $cpuSec - $prevCpu
    $dWall = ($nowMs - $prevWall) / 1000.0
    if ($dWall -gt 0) { $cpuPct = [math]::Round(($dProc / ($dWall * $cores)) * 100.0, 1) }
  }
  $prevCpu = $cpuSec; $prevWall = $nowMs

  "$epoch,$cpuPct,$ws,$pv" | Out-File -FilePath $Out -Append -Encoding ascii
  Write-Host -NoNewline ("`r WS=$ws MB  Private=$pv MB  CPU=$cpuPct%   ")
  Start-Sleep -Seconds $Interval
}
