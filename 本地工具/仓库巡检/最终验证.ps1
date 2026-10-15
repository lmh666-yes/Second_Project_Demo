# 最终验证.ps1
# 用途：一次跑完三端验证并打印结论表。
#   1) 复编 Qt 上位机（qmake + mingw32-make），跑 --selftest / --rangecheck / --cmdselftest
#   2) 用 Keil 复编板1、板2 两个工程（全量 -r），统计 Error / Warning
#   3) 跑文风检查.ps1，统计 AI 文风特征命中数
# 用法：powershell.exe -NoProfile -ExecutionPolicy Bypass -File 本地工具\仓库巡检\最终验证.ps1

param(
    [string]$RepoRoot = '',
    [string]$QtBin    = '',   # 留空 = 自动找（QTDIR/QT_ROOT → 各盘 \Qt → PATH）
    [string]$MakeBin  = '',   # 留空 = 自动找（同上，找配套 MinGW）
    [string]$Keil     = '',   # 留空 = 自动找常见安装位
    [switch]$SkipQt,
    [switch]$SkipKeil,
    [switch]$SkipStyle
)

$ErrorActionPreference = 'Continue'

# ---------- 路径自动探测（原来写死 D:\GitHub_Project / E:\Qt\... / E:\k5_v5，换机器就跑不起来） ----------
if (-not $RepoRoot) {
    $d = $PSScriptRoot
    while ($d -and -not (Test-Path (Join-Path $d 'EnvMonitor')) -and -not (Test-Path (Join-Path $d '.git'))) {
        $p = Split-Path $d -Parent
        if (-not $p -or $p -eq $d) { break }
        $d = $p
    }
    $RepoRoot = if ($d) { $d } else { (Get-Location).Path }
}

function First-Existing([string[]]$paths) {
    # 注意：Test-Path **不支持通配符**（会一直返回 false），所以这里用 Get-Item 展开。
    foreach ($p in $paths) {
        if (-not $p) { continue }
        $hit = Get-Item -Path $p -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

if (-not $QtBin -or -not $MakeBin) {
    $roots = @()
    foreach ($e in @($env:QT_ROOT, $env:QTDIR)) { if ($e) { $roots += $e } }
    foreach ($drv in @('C:', 'D:', 'E:', 'F:', 'G:')) { $roots += "$drv\Qt"; $roots += "$drv\Qt6" }
    $qhit = First-Existing @($roots | ForEach-Object { "$_\*\mingw*\bin\qmake.exe" })
    $ghit = First-Existing @($roots | ForEach-Object { "$_\Tools\mingw*\bin\g++.exe" })
    if (-not $QtBin -and $qhit)   { $QtBin   = Split-Path $qhit -Parent }
    if (-not $MakeBin -and $ghit) { $MakeBin = Split-Path $ghit -Parent }
    # 最后兜底：PATH 上现成的
    if (-not $QtBin)   { $q = Get-Command qmake -ErrorAction SilentlyContinue; if ($q) { $QtBin = Split-Path $q.Source -Parent } }
    if (-not $MakeBin) { $g = Get-Command g++   -ErrorAction SilentlyContinue; if ($g) { $MakeBin = Split-Path $g.Source -Parent } }
}
if (-not $Keil) {
    $Keil = First-Existing @('E:\k5_v5\UV4\UV4.exe', 'C:\Keil_v5\UV4\UV4.exe',
                             'D:\Keil_v5\UV4\UV4.exe', 'E:\Keil_v5\UV4\UV4.exe',
                             'C:\Keil\UV4\UV4.exe')
}

# 缺件要说清楚缺什么、怎么办，而不是等中间报一堆莫名其妙的下游错误
if (-not $SkipQt -and (-not $QtBin -or -not $MakeBin)) {
    Write-Host "[错误] 找不到 Qt / MinGW：QtBin=$QtBin  MakeBin=$MakeBin" -ForegroundColor Red
    Write-Host "       找过 QTDIR/QT_ROOT、各盘 \Qt（\Qt6）与 PATH。装 Qt 6.x + MinGW，或设 QT_ROOT 后重跑；"
    Write-Host "       只想跳过 Qt 这一步就加 -SkipQt。"
    exit 1
}
if (-not $SkipKeil -and -not $Keil) {
    Write-Host "[错误] 找不到 Keil UV4.exe（找过 E:\k5_v5、C:\Keil_v5 等常见位置）。" -ForegroundColor Red
    Write-Host "       用 -Keil <UV4.exe 路径> 指定，或加 -SkipKeil 跳过复编。"
    exit 1
}

$QtProjDir = Join-Path $RepoRoot 'EnvMonitor\QT_project\SerialMonitor'
$QtBuildDir = Join-Path $QtProjDir 'build_verify'
$Board1Proj = Join-Path $RepoRoot 'EnvMonitor\Project_1_检测端\Project\标准模板.uvprojx'
$Board2Proj = Join-Path $RepoRoot 'EnvMonitor\Project_2_数据通信端\Project\000标准模板库.uvprojx'
$LogDir = Join-Path $RepoRoot '本地工具\仓库巡检\验证日志'
if (-not (Test-Path $LogDir)) { [void](New-Item -ItemType Directory -Path $LogDir) }

$result = New-Object System.Collections.ArrayList
function Add-Result($name, $ok, $detail) {
    [void]$result.Add([pscustomobject]@{ 项目 = $name; 结果 = $(if ($ok) { 'PASS' } else { 'FAIL' }); 说明 = $detail })
}

# ---------- 1) Qt ----------
if (-not $SkipQt) {
    $env:PATH = "$QtBin;$MakeBin;" + $env:PATH
    Push-Location $QtBuildDir
    & qmake "$QtProjDir\SerialMonitor.pro" 2>&1 | Out-File "$LogDir\qt_qmake.txt" -Encoding UTF8
    $rcQmake = $LASTEXITCODE
    Add-Result 'Qt qmake' ($rcQmake -eq 0) "exit $rcQmake"

    & mingw32-make -j4 2>&1 | Out-File "$LogDir\qt_make.txt" -Encoding UTF8
    $rcMake = $LASTEXITCODE
    $makeText = Get-Content "$LogDir\qt_make.txt" -Encoding UTF8 -Raw
    $errCnt = ([regex]::Matches($makeText, '(?m)^.*\berror\b.*$')).Count
    Add-Result 'Qt make' ($rcMake -eq 0) "exit $rcMake，含 error 字样行 $errCnt"

    $exe = Join-Path $QtBuildDir 'release\SerialMonitor.exe'
    foreach ($t in @('--selftest', '--rangecheck', '--cmdselftest')) {
        $tp = Start-Process -FilePath $exe -ArgumentList $t -WorkingDirectory (Split-Path $exe) -Wait -PassThru
        Add-Result "Qt $t" ($tp.ExitCode -eq 0) "exit $($tp.ExitCode)"
    }
    Pop-Location
}

# ---------- 2) Keil 两个工程 ----------
if (-not $SkipKeil) {
    foreach ($item in @(@('板1', $Board1Proj), @('板2', $Board2Proj))) {
        $name = $item[0]; $proj = $item[1]
        $log = Join-Path $LogDir "keil_$name.log"
        if (Test-Path $log) { Remove-Item $log -Force }
        & $Keil -r -j0 -o "$log" "$proj" | Out-Null
        $rc = $LASTEXITCODE
        $text = ''
        if (Test-Path $log) {
            # Keil 的日志有 GBK 段落，按 GBK 读再兜底 UTF-8
            try { $text = [System.Text.Encoding]::GetEncoding(936).GetString([System.IO.File]::ReadAllBytes($log)) }
            catch { $text = Get-Content $log -Raw }
        }
        $m = [regex]::Match($text, '(\d+)\s+Error\(s\),\s+(\d+)\s+Warning\(s\)')
        if ($m.Success) {
            $e = [int]$m.Groups[1].Value; $w = [int]$m.Groups[2].Value
            Add-Result "Keil $name" (($e -eq 0) -and ($w -eq 0)) "$e Error / $w Warning（UV4 exit $rc）"
        } else {
            # 没有汇总行说明根本没编成，报错数按日志里 Error: 行数估
            $fake = ([regex]::Matches($text, '(?m)^.*\bError:\s')).Count
            Add-Result "Keil $name" $false "日志里没有 Error/Warning 汇总行，疑似工具链未启动（UV4 exit $rc，Error: 行 $fake）"
        }
        $size = [regex]::Match($text, 'Program Size:\s*(.+)')
        if ($size.Success) { Add-Result "Keil $name 尺寸" $true $size.Groups[1].Value.Trim() }
    }
}

# ---------- 3) 文风检查 ----------
if (-not $SkipStyle) {
    $styleLog = Join-Path $LogDir '文风检查.txt'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot '本地工具\仓库巡检\文风检查.ps1') -Root (Join-Path $RepoRoot 'EnvMonitor') -Top 15 2>&1 | Out-File $styleLog -Encoding UTF8
    $styleText = Get-Content $styleLog -Raw
    $m = [regex]::Match($styleText, '命中总数:\s*(\d+)')
    $n = if ($m.Success) { [int]$m.Groups[1].Value } else { -1 }
    Add-Result '文风检查' ($n -ge 0) "命中总数 $n（明细见 $styleLog）"
}

'===== 验证结论 ====='
$result | Format-Table -AutoSize | Out-String -Width 200
$failed = ($result | Where-Object { $_.结果 -eq 'FAIL' }).Count
"FAIL 项数: $failed"
exit $(if ($failed -eq 0) { 0 } else { 1 })
