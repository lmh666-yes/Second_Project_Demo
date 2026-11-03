param(
    [double]$Hours = 12,
    [string]$PortB1 = "COM7",
    [string]$PortB2 = "COM10",
    [int]$SampleSec = 10
)
# 长稳测试：同时盯板1、板2 的串口，每 SampleSec 秒记一行 CSV，结束时给统计
# 用法：双击 长稳测试.bat，或 powershell -File 长稳测试.ps1 -Hours 12

[Console]::OutputEncoding = [System.Text.Encoding]::GetEncoding(936)
$ErrorActionPreference = 'Continue'

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$csv   = Join-Path $PSScriptRoot ("长稳_$stamp.csv")
$sumF  = Join-Path $PSScriptRoot ("长稳_{0}_汇总.txt" -f $stamp)

$header = "时间,板1_TX_OK,板1_丢帧,板1_SHT失败,板2_rx,板2_bad,板2_pub,板2_fail,板2_cfail,板2_cache,板2_lost,板2_net,串口重连"
[System.IO.File]::WriteAllText($csv, $header + "`r`n", [System.Text.Encoding]::GetEncoding(936))

function Open-Port([string]$name) {
    try {
        $p = New-Object System.IO.Ports.SerialPort $name,115200,None,8,One
        $p.ReadTimeout = 300
        $p.Open()
        Start-Sleep -Milliseconds 200
        $p.DiscardInBuffer()
        return $p
    } catch {
        Write-Host ("  [{0}] 打不开：{1}" -f $name, $_.Exception.Message) -ForegroundColor Yellow
        return $null
    }
}

$p1 = Open-Port $PortB1
$p2 = Open-Port $PortB2
$reconnect = 0
$reboots = 0
$prev = @{ b1_tx=0; b2_rx=0; b2_pub=0 }

$v = @{ b1_tx=0; b1_drop=0; b1_sht=0; b2_rx=0; b2_bad=0; b2_pub=0; b2_fail=0; b2_cfail=0; b2_cache=0; b2_cap=0; b2_lost=0; b2_net=0 }
$buf1 = ""; $buf2 = ""

$first = $null
$total = [TimeSpan]::FromHours($Hours)
$t0 = Get-Date
$nextSample = $t0

Write-Host "=== 长稳测试开始 ===" -ForegroundColor Green
Write-Host ("时长 {0} 小时，采样间隔 {1} 秒" -f $Hours, $SampleSec)
Write-Host ("板1={0}  板2={1}" -f $PortB1, $PortB2)
Write-Host ("数据文件：{0}" -f $csv)
Write-Host "按 Ctrl+C 可提前结束并出汇总`n"

while (((Get-Date) - $t0).TotalSeconds -lt $total.TotalSeconds) {

    if ($p1 -ne $null -and $p1.IsOpen) {
        try { $n = $p1.BytesToRead; if ($n -gt 0) { $b = New-Object byte[] $n; $r = $p1.Read($b,0,$n); $buf1 += [System.Text.Encoding]::GetEncoding(936).GetString($b,0,$r) } } catch {}
    }
    if ($p2 -ne $null -and $p2.IsOpen) {
        try { $n = $p2.BytesToRead; if ($n -gt 0) { $b = New-Object byte[] $n; $r = $p2.Read($b,0,$n); $buf2 += [System.Text.Encoding]::GetEncoding(936).GetString($b,0,$r) } } catch {}
    }

    if ($buf1.Length -gt 20000) { $buf1 = $buf1.Substring($buf1.Length-4000) }
    if ($buf2.Length -gt 20000) { $buf2 = $buf2.Substring($buf2.Length-4000) }

    foreach ($m in [regex]::Matches($buf1, 'TX_OK=(\d+)\s+TX_FAIL=(\d+)\s+SHT30_FAIL=(\d+)')) {
        $v.b1_tx = [int]$m.Groups[1].Value; $v.b1_sht = [int]$m.Groups[3].Value
    }
    foreach ($m in [regex]::Matches($buf1, '丢帧=(\d+)')) { $v.b1_drop = [int]$m.Groups[1].Value }
    foreach ($m in [regex]::Matches($buf2, 'MON\]\s+rx=(\d+) bad=(\d+) pub=(\d+) fail=(\d+) cfail=(\d+) cache=(\d+)/(\d+) lost=(\d+) net=(\d+)')) {
        $v.b2_rx=[int]$m.Groups[1].Value; $v.b2_bad=[int]$m.Groups[2].Value
        $v.b2_pub=[int]$m.Groups[3].Value; $v.b2_fail=[int]$m.Groups[4].Value
        $v.b2_cfail=[int]$m.Groups[5].Value; $v.b2_cache=[int]$m.Groups[6].Value
        $v.b2_cap=[int]$m.Groups[7].Value; $v.b2_lost=[int]$m.Groups[8].Value
        $v.b2_net=[int]$m.Groups[9].Value
    }

    if ($null -eq $first -and $v.b1_tx -gt 0 -and $v.b2_rx -gt 0) { $first = $v.Clone() }

    if ((Get-Date) -ge $nextSample) {
        $row = "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12}" -f `
            (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $v.b1_tx, $v.b1_drop, $v.b1_sht, `
            $v.b2_rx, $v.b2_bad, $v.b2_pub, $v.b2_fail, $v.b2_cfail, $v.b2_cache, $v.b2_lost, $v.b2_net, $reconnect
        [System.IO.File]::AppendAllText($csv, $row + "`r`n", [System.Text.Encoding]::GetEncoding(936))
        $spent = [int]((Get-Date) - $t0).TotalMinutes
        Write-Host ("[{0,4}m] 板1 TX_OK={1,-6} 板2 rx={2,-6} pub={3,-6} fail={4,-4} cfail={5,-4} cache={6}/{7} lost={8} net={9}" -f `
            $spent, $v.b1_tx, $v.b2_rx, $v.b2_pub, $v.b2_fail, $v.b2_cfail, $v.b2_cache, $v.b2_cap, $v.b2_lost, $v.b2_net)
        # 计数器变小 = 那块板重启过（板1 的 TX_OK、板2 的 rx/pub 都是上电清零的）
        if (($v.b1_tx -lt $prev.b1_tx) -or ($v.b2_rx -lt $prev.b2_rx) -or ($v.b2_pub -lt $prev.b2_pub)) {
            $reboots++
            $first = $null
            Write-Host ("  !! 检测到重启，计数已重置（累计 {0} 次）" -f $reboots) -ForegroundColor Red
        }
        $prev.b1_tx = $v.b1_tx; $prev.b2_rx = $v.b2_rx; $prev.b2_pub = $v.b2_pub
        $nextSample = (Get-Date).AddSeconds($SampleSec)
    }

    if ($p1 -ne $null -and -not $p1.IsOpen) { $p1 = Open-Port $PortB1; if ($p1 -ne $null) { $reconnect++ } }
    if ($p2 -ne $null -and -not $p2.IsOpen) { $p2 = Open-Port $PortB2; if ($p2 -ne $null) { $reconnect++ } }

    Start-Sleep -Milliseconds 200
}

try { if ($p1 -and $p1.IsOpen) { $p1.Close() } } catch {}
try { if ($p2 -and $p2.IsOpen) { $p2.Close() } } catch {}

$mins = ((Get-Date) - $t0).TotalMinutes
if ($null -ne $first) {
    $d_tx   = $v.b1_tx   - $first.b1_tx
    $d_rx   = $v.b2_rx   - $first.b2_rx
    $d_pub  = $v.b2_pub  - $first.b2_pub
    $d_fail = $v.b2_fail - $first.b2_fail
    $d_bad  = $v.b2_bad  - $first.b2_bad
    $d_sht  = $v.b1_sht  - $first.b1_sht
    $loss   = if ($d_tx -gt 0) { [math]::Round(([math]::Max(0,$d_tx - $d_rx)) * 100.0 / $d_tx, 2) } else { 0 }
    $sum = @"
长稳测试汇总
================
开始时间    : $($t0.ToString('yyyy-MM-dd HH:mm:ss'))
总时长      : $([math]::Round($mins,1)) 分钟
板1 新增发送: $d_tx 帧
板2 新增接收: $d_rx 帧   无线丢包率 = $loss %
板2 解析错误: $d_bad
板2 发布成功: $d_pub   发布失败 = $d_fail
板1 采集失败: $d_sht（SHT30）
板2 落盘失败: $($v.b2_cfail)
缓存水位    : $($v.b2_cache)/$($v.b2_cap)   溢出丢 = $($v.b2_lost)
板1/板2 重启: $reboots 次
串口重连次数: $reconnect
数据文件    : $csv
"@
} else {
    $sum = "长稳测试汇总`r`n================`r`n未采集到有效数据（检查 COM 口和波特率）"
}
[System.IO.File]::WriteAllText($sumF, $sum, [System.Text.Encoding]::GetEncoding(936))
Write-Host "`n$sum" -ForegroundColor Cyan
Write-Host ("汇总已写入：{0}" -f $sumF)
