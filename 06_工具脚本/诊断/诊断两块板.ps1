param([int]$Seconds = 12)

# 诊断两块板：同时采样所有串口，自动识别板1/板2，给出结论
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::GetEncoding(936)

Write-Host ""
Write-Host "==================== 板1 / 板2 串口自检 ====================" -ForegroundColor Cyan
Write-Host "采样 $Seconds 秒，期间请不要用其他串口工具打开同一端口" -ForegroundColor DarkGray
Write-Host ""

$ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
if ($ports.Count -eq 0) { Write-Host "没有找到任何串口设备" -ForegroundColor Red; exit 1 }

$open = @{}
foreach ($p in $ports) {
    $sp = New-Object System.IO.Ports.SerialPort $p,115200,None,8,One
    $sp.ReadTimeout = 200
    try { $sp.Open(); $open[$p] = $sp } catch { Write-Host ("[{0}] 打不开（被占用？）：{1}" -f $p, $_.Exception.Message) -ForegroundColor Yellow }
}

if ($open.Count -eq 0) { Write-Host "所有串口都打不开，请先关闭串口助手 / Keil 调试 / Qt" -ForegroundColor Red; exit 1 }

$buf = @{}
foreach ($k in $open.Keys) { Start-Sleep -Milliseconds 150; try { $open[$k].DiscardInBuffer() } catch {}; $buf[$k] = New-Object System.IO.MemoryStream }

$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    foreach ($k in @($open.Keys)) {
        try {
            $n = $open[$k].BytesToRead
            if ($n -gt 0) { $b = New-Object byte[] $n; $r = $open[$k].Read($b,0,$n); $buf[$k].Write($b,0,$r) }
        } catch {}
    }
    Start-Sleep -Milliseconds 120
}
foreach ($k in @($open.Keys)) { try { $open[$k].Close() } catch {} }

function Stat-Board1($txt) {
    $ok   = [regex]::Matches($txt,'TX_OK=(\d+)')   | ForEach-Object { [int]$_.Groups[1].Value }
    $fail = [regex]::Matches($txt,'SHT30_FAIL=(\d+)') | ForEach-Object { [int]$_.Groups[1].Value }
    $frames = ([regex]::Matches($txt,'AA 55 ')).Count
    [pscustomobject]@{
        HasData = $txt.Length -gt 0
        TxFirst = if ($ok.Count)   { $ok[0] }   else { -1 }
        TxLast  = if ($ok.Count)   { $ok[$ok.Count-1] } else { -1 }
        ShtLast = if ($fail.Count) { $fail[$fail.Count-1] } else { -1 }
        Frames  = $frames
    }
}
function Stat-Board2($txt) {
    $rx   = [regex]::Matches($txt,'MON\]\s+rx=(\d+)')   | ForEach-Object { [int]$_.Groups[1].Value }
    $bad  = [regex]::Matches($txt,'bad=(\d+)')  | ForEach-Object { [int]$_.Groups[1].Value }
    $cach = [regex]::Matches($txt,'cache=(\d+)/(\d+)')
    $lost = [regex]::Matches($txt,'lost=(\d+)') | ForEach-Object { [int]$_.Groups[1].Value }
    [pscustomobject]@{
        HasData = $txt.Length -gt 0
        RxFirst = if ($rx.Count) { $rx[0] } else { -1 }
        RxLast  = if ($rx.Count) { $rx[$rx.Count-1] } else { -1 }
        BadLast = if ($bad.Count) { $bad[$bad.Count-1] } else { -1 }
        Cache   = if ($cach.Count) { $cach[$cach.Count-1].Groups[1].Value + '/' + $cach[$cach.Count-1].Groups[2].Value } else { '?' }
        LostLast= if ($lost.Count) { $lost[$lost.Count-1] } else { -1 }
    }
}

$b1 = $null; $b1port = ''; $b2 = $null; $b2port = ''
foreach ($k in $buf.Keys) {
    $txt = [Text.Encoding]::GetEncoding(936).GetString($buf[$k].ToArray())
    if ($txt -match 'TX_OK=')          { $b1 = Stat-Board1 $txt; $b1port = $k }
    elseif ($txt -match 'rx=')         { $b2 = Stat-Board2 $txt; $b2port = $k }
    elseif ($txt.Length -gt 0)         { Write-Host ("[{0}] 收到 {1} 字节，但没识别出是哪块板" -f $k, $txt.Length) -ForegroundColor Yellow }
}

Write-Host "------------------------- 采样结果 -------------------------"
if ($b1) {
    Write-Host ("板1 [{0}]  TX_OK {1} → {2}   SHT30_FAIL={3}   完整帧={4}" -f $b1port,$b1.TxFirst,$b1.TxLast,$b1.ShtLast,$b1.Frames)
} else { Write-Host "板1  没有识别到（串口无输出）" -ForegroundColor Yellow }
if ($b2) {
    Write-Host ("板2 [{0}]  rx {1} → {2}   bad={3}   cache={4}   lost={5}" -f $b2port,$b2.RxFirst,$b2.RxLast,$b2.BadLast,$b2.Cache,$b2.LostLast)
} else { Write-Host "板2  没有识别到（串口无输出）" -ForegroundColor Yellow }

Write-Host ""
Write-Host "--------------------------- 结论 ---------------------------"
$b1ok = $b1 -and $b1.HasData -and (($b1.TxLast -gt $b1.TxFirst) -or ($b1.Frames -ge 2))
$b2ok = $b2 -and $b2.HasData -and ($b2.RxLast -gt $b2.RxFirst)

if (-not $b1 -and -not $b2) {
    Write-Host "两块板都没有串口输出，按顺序查：" -ForegroundColor Red
    Write-Host "  1) 板子有没有上电（看电源灯 / LCD 背光）"
    Write-Host "  2) USB 线是不是只充电不过数据（换一根）"
    Write-Host "  3) 串口号有没有变（拔插后 COM 号会跳）"
    Write-Host "  4) 板1 是不是被 J-Link 按在 halt（拔掉 J-Link 再上电）"
    exit 0
}

if ($b1 -and -not $b1ok) {
    if ($b1.Frames -ge 2) {
        Write-Host ("板1 在发：抓到 {0} 个完整帧（TX_OK 统计行每 5 秒才打一次，{1} 秒内可能只采到 1 行）" -f $b1.Frames,$Seconds) -ForegroundColor Green
    } else {
    Write-Host "板1 串口有输出但 TX_OK 不涨 → 内核在跑但没发出去" -ForegroundColor Red
    Write-Host "   a) 看 SHT30_FAIL 是否在涨：涨 = 采集卡住"
    Write-Host "   b) 拔掉 J-Link 断电重上电（可能被 halt）"
    Write-Host "   c) LoRa 的 M1 是否接回 GND（悬空会不发射）"
    }
} elseif ($b1ok) {
    Write-Host ("板1 正常：{0} 秒内 TX_OK +{1}，约 {2:N1} 帧/秒" -f $Seconds, ($b1.TxLast-$b1.TxFirst), (($b1.TxLast-$b1.TxFirst)/[double]$Seconds)) -ForegroundColor Green
    if ($b1.ShtLast -gt 0) { Write-Host ("   注意：SHT30_FAIL={0}，湿度会显示 0" -f $b1.ShtLast) -ForegroundColor Yellow }
}

if ($b2 -and -not $b2ok) {
    Write-Host "板2 串口有输出但 rx 不涨 → 没收到板1 的帧" -ForegroundColor Red
    Write-Host "   a) 板1 是否在发（看上面）"
    Write-Host "   b) 板2 的 LoRa 接线：PC6/PC7 是否接反"
    Write-Host "   c) 两块板的 M0/M1 是否都接 GND"
    Write-Host "   d) 天线是否接好"
} elseif ($b2ok) {
    Write-Host ("板2 正常：{0} 秒内 rx +{1}   cache={2}" -f $Seconds, ($b2.RxLast-$b2.RxFirst), $b2.Cache) -ForegroundColor Green
    if ($b2.BadLast -gt 0) { Write-Host ("   注意：bad={0}，有帧但解析失败（查 CRC/长度）" -f $b2.BadLast) -ForegroundColor Yellow }
}

if ($b1ok -and $b2ok) { Write-Host ""; Write-Host "链路正常：板1 在发，板2 在收。若屏幕仍无数据，问题在显示/刷新逻辑。" -ForegroundColor Green }
Write-Host ""
