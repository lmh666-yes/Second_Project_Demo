# =====================================================================
#  project_check.ps1 —— Second_Project_Demo 项目综合巡检（v1.0）
# ---------------------------------------------------------------------
#  检查三项：
#    1) 内容齐全性 —— 关键文件 存在 / 空 / 缺失
#    2) 架构吻合度 —— 架构文档所需的驱动模块 vs 两个工程的库现状
#    3) 基础准备度 —— 编译记录 / git 状态 / 仓库卫生
#
#  用法（任选其一）：
#    - 双击 project_check.bat（一键运行）
#    - 命令行： powershell -NoProfile -ExecutionPolicy Bypass -File project_check.ps1
#
#  输出：控制台摘要 + Markdown 报告（默认「检查报告.md」，UTF-8 无 BOM）
#  注意：本脚本自身必须保存为 UTF-8 with BOM，否则 Windows PowerShell 5.1
#        会把中文当 ANSI 读，导致乱码或语法错误。
# =====================================================================

param(
    [string]$Root   = $PSScriptRoot,   # 仓库根目录（默认＝脚本所在目录）
    [string]$Report = ''               # 报告输出路径（默认＝根目录\检查报告.md）
)

if ([string]::IsNullOrWhiteSpace($Root)) { $Root = (Get-Location).Path }
$Root = (Resolve-Path -LiteralPath $Root).Path
if ([string]::IsNullOrWhiteSpace($Report)) { $Report = Join-Path $Root '检查报告.md' }

try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

$now = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'

# ---------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------
$script:rp = New-Object System.Text.StringBuilder
function Add-Rp { param([string]$Text = '') [void]$script:rp.AppendLine([string]$Text) }

function Get-Rel {
    param([string]$Full)
    if ($Full.Length -gt $Root.Length -and $Full.StartsWith($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Full.Substring($Root.Length).TrimStart('\')
    }
    return $Full
}

function Get-DirSize {
    param([string]$Path)
    $m = Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum
    if ($null -eq $m.Sum) { return [double]0 } else { return [double]$m.Sum }
}

function Get-DirFileCount {
    param([string]$Path)
    return @(Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue).Count
}

# 关键文件状态：OK / 空 / 过短 / 缺 / 目录
function Get-ItemState {
    param([string]$FullPath)
    if (-not (Test-Path -LiteralPath $FullPath)) { return @{ S = '缺'; Info = '-' } }
    $it = Get-Item -LiteralPath $FullPath -Force
    if ($it.PSIsContainer) {
        $cnt = Get-DirFileCount $FullPath
        $sz  = Get-DirSize $FullPath
        return @{ S = '目录'; Info = ('{0} 文件 / {1:N1} MB' -f $cnt, ($sz / 1MB)) }
    }
    if ($it.Length -eq 0) { return @{ S = '空'; Info = '0 字节' } }
    if ($it.Length -lt 200 -and $it.Extension -eq '.md') {
        return @{ S = '过短'; Info = ('{0} 字节（疑似占位）' -f $it.Length) }
    }
    return @{ S = 'OK'; Info = ('{0:N1} KB' -f ($it.Length / 1KB)) }
}

# 驱动模块检查：在 工程\FWLIB\inc + src 里按文件名关键词匹配
function Get-ModuleState {
    param([string]$ProjDir, [string]$Pattern)
    $hits = New-Object System.Collections.ArrayList
    foreach ($sub in @('FWLIB\inc', 'FWLIB\src')) {
        $d = Join-Path $ProjDir $sub
        if (Test-Path -LiteralPath $d) {
            Get-ChildItem -LiteralPath $d -File -ErrorAction SilentlyContinue |
                Where-Object { $_.BaseName -match $Pattern } |
                ForEach-Object { [void]$hits.Add($_.Name) }
        }
    }
    if ($hits.Count -gt 0) { return ('有 (' + (@($hits | Select-Object -First 2) -join ', ') + ')') }
    return '缺'
}

# ===============================================================
# 路径常量
# ===============================================================
$P1 = Join-Path $Root 'S_N_sys\Project_1_检测端\Project'
$P2 = Join-Path $Root 'S_N_sys\Project_2_数据通信端\Project'
$QT = Join-Path $Root 'S_N_sys\QT_project\SerialMonitor'

# ===============================================================
# 1) 顶层结构
# ===============================================================
$structRows = New-Object System.Collections.ArrayList
$topDirs = @(Get-ChildItem -LiteralPath $Root -Directory -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne '.git' })
foreach ($d in $topDirs) { [void]$structRows.Add($d) }
$sns = Join-Path $Root 'S_N_sys'
if (Test-Path -LiteralPath $sns) {
    foreach ($d in @(Get-ChildItem -LiteralPath $sns -Directory -Force -ErrorAction SilentlyContinue)) { [void]$structRows.Add($d) }
}

# ===============================================================
# 2) 空文件（全仓库，排除 .git）
# ===============================================================
$emptyFiles = @(Get-ChildItem -Path $Root -Recurse -File -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Length -eq 0 -and $_.FullName -notmatch '\\\.git\\' } |
    Sort-Object FullName)

# ===============================================================
# 3) 关键文件清单
# ===============================================================
$keyGroups = @(
    @{ Title = 'A. 仓库根'; Items = @(
        @{ P = 'README.md';         D = '仓库总说明（<200 字节视为占位）' },
        @{ P = '.gitignore';        D = 'git 忽略规则（缺则编译产物会进仓库）' },
        @{ P = 'project_check.ps1'; D = '本巡检脚本' }
    )},
    @{ Title = 'B. 架构设计参考'; Items = @(
        @{ P = 'S_N_sys\项目架构思路设计参考\方案13.md';               D = '方案13 升级版（双 F407 分布式 + 采购清单）' },
        @{ P = 'S_N_sys\项目架构思路设计参考\核心软件和FreeRTOS架构.md'; D = 'FreeRTOS 任务/队列/信号量架构' }
    )},
    @{ Title = 'C. Project_1 检测端'; Items = @(
        @{ P = 'S_N_sys\Project_1_检测端\检测数据端设计.md';                    D = '检测端设计文档' },
        @{ P = 'S_N_sys\Project_1_检测端\该端目前采用STM32F407_zet6.txt';       D = '检测端板卡说明' },
        @{ P = 'S_N_sys\Project_1_检测端\Project\标准模板.uvprojx';             D = 'Keil 工程（STM32F407ZE）' },
        @{ P = 'S_N_sys\Project_1_检测端\Project\main.c';                       D = '主程序（骨架）' },
        @{ P = 'S_N_sys\Project_1_检测端\Project\README.md';                    D = '库使用手册（GEC-M4 版）' },
        @{ P = 'S_N_sys\Project_1_检测端\Project\build_keil.bat';               D = '一键编译脚本' },
        @{ P = 'S_N_sys\Project_1_检测端\Project\GEC-M4原理图2016-07-29.pdf';   D = '板卡原理图' }
    )},
    @{ Title = 'D. Project_2 数据通信端'; Items = @(
        @{ P = 'S_N_sys\Project_2_数据通信端\数据传输端设计.md';                D = '网关端设计文档' },
        @{ P = 'S_N_sys\Project_2_数据通信端\该端目前采用STM32F407_zgt6.txt';   D = '网关端板卡说明' },
        @{ P = 'S_N_sys\Project_2_数据通信端\Project\000标准模板库.uvprojx';    D = 'Keil 工程（STM32F407ZG）' },
        @{ P = 'S_N_sys\Project_2_数据通信端\Project\main.c';                   D = '主程序（骨架）' },
        @{ P = 'S_N_sys\Project_2_数据通信端\Project\main参考示例.md';          D = 'main 写法参考' },
        @{ P = 'S_N_sys\Project_2_数据通信端\Project\README.md';                D = '库使用手册（天马版）' },
        @{ P = 'S_N_sys\Project_2_数据通信端\Project\build_keil.bat';           D = '一键编译脚本' },
        @{ P = 'S_N_sys\Project_2_数据通信端\Project\普中-天马 F407开发板原理图.pdf'; D = '板卡原理图' },
        @{ P = 'S_N_sys\Project_2_数据通信端\Project\0001_标准模板库';          D = '参考夹（按流程编译后应删除）' }
    )},
    @{ Title = 'E. Qt 上位机'; Items = @(
        @{ P = 'S_N_sys\QT_project\SerialMonitor\SerialMonitor.pro'; D = 'Qt 工程文件' },
        @{ P = 'S_N_sys\QT_project\SerialMonitor\main.cpp';          D = '程序入口' },
        @{ P = 'S_N_sys\QT_project\SerialMonitor\mainwindow.cpp';    D = '主窗口实现（串口+曲线）' },
        @{ P = 'S_N_sys\QT_project\SerialMonitor\mainwindow.h';      D = '主窗口头文件' },
        @{ P = 'S_N_sys\QT_project\SerialMonitor\mainwindow.ui';     D = '界面文件' },
        @{ P = 'S_N_sys\QT_project\SerialMonitor\qcustomplot.cpp';   D = '绘图库实现' },
        @{ P = 'S_N_sys\QT_project\SerialMonitor\qcustomplot.h';     D = '绘图库头文件' }
    )}
)

$keyTotal = 0; $keyBad = 0; $keyResults = @()
foreach ($g in $keyGroups) {
    $rows = @()
    foreach ($it in $g.Items) {
        $st = Get-ItemState (Join-Path $Root $it.P)
        $keyTotal++
        if ($st.S -in @('空', '过短', '缺')) { $keyBad++ }
        $rows += [PSCustomObject]@{ S = $st.S; Info = $st.Info; P = $it.P; D = $it.D }
        $keyResults += $rows[-1]
    }
    $g.Rows = $rows
}

# ===============================================================
# 4) 架构吻合度 —— 驱动模块覆盖
# ===============================================================
$board1Needs = @(
    @{ N = 'SHT30 温湿度 (I2C)';    P = 'sht30' },
    @{ N = 'BMP280 气压 (SPI)';     P = 'bmp280' },
    @{ N = 'BH1750 光照 (I2C)';     P = 'bh1750' },
    @{ N = 'CCS811 空气质量 (I2C)'; P = 'ccs811' },
    @{ N = 'LoRa E22 无线 (UART)';  P = 'lora|e22|sx127' },
    @{ N = 'MQ-135 空气 (ADC；sys_adc 可直采)'; P = 'mq_?135' },
    @{ N = 'OLED 显示 (I2C)';       P = 'oled' }
)
$board2Needs = @(
    @{ N = 'LoRa E22 无线 (UART)';  P = 'lora|e22|sx127' },
    @{ N = 'ESP8266 WiFi';          P = 'esp8266' },
    @{ N = 'MQTT 客户端';           P = 'mqtt' },
    @{ N = '串口帧协议 (sys_frame)'; P = 'sys_frame' },
    @{ N = '看门狗心跳 (sys_wdg)';   P = 'sys_wdg' }
)
$commonNeeds = @(
    @{ N = 'sys_usart 串口(DMA+空闲中断)'; P = 'sys_usart' },
    @{ N = 'sys_dma  DMA 搬运';            P = 'sys_dma' },
    @{ N = 'sys_i2c  I2C 总线';            P = 'sys_i2c' },
    @{ N = 'sys_spi  SPI 主机';            P = 'sys_spi' },
    @{ N = 'sys_adc  ADC 采集';            P = 'sys_adc' }
)

$b1ok = 0; $b1rows = @()
foreach ($n in $board1Needs) {
    $st = Get-ModuleState $P1 $n.P
    if ($st.StartsWith('有')) { $b1ok++ }
    $b1rows += [PSCustomObject]@{ N = $n.N; St = $st }
}
$b2ok = 0; $b2rows = @()
foreach ($n in $board2Needs) {
    $st = Get-ModuleState $P2 $n.P
    if ($st.StartsWith('有')) { $b2ok++ }
    $b2rows += [PSCustomObject]@{ N = $n.N; St = $st }
}
$cmRows = @()
foreach ($n in $commonNeeds) {
    $s1 = Get-ModuleState $P1 $n.P
    $s2 = Get-ModuleState $P2 $n.P
    $cmRows += [PSCustomObject]@{ N = $n.N; S1 = $s1; S2 = $s2 }
}
$rtos1 = (Test-Path -LiteralPath (Join-Path $P1 'FreeRTOS\src\tasks.c'))
$rtos2 = (Test-Path -LiteralPath (Join-Path $P2 'FreeRTOS\src\tasks.c'))

# main.c 是否已开始写业务任务
function Test-MainStarted {
    param([string]$MainPath)
    if (-not (Test-Path -LiteralPath $MainPath)) { return '无 main.c' }
    $c = Get-Content -LiteralPath $MainPath -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    if ($null -eq $c) { return '空' }
    if ($c -match 'xTaskCreate|vTaskDelay|Task') { return '已开始写任务' }
    return '未开始（骨架）'
}
$main1 = Test-MainStarted (Join-Path $P1 'main.c')
$main2 = Test-MainStarted (Join-Path $P2 'main.c')

# ===============================================================
# 5) 编译记录（找 build_verify.log 并读尾部）
# ===============================================================
$compileRows = @()
$logs = @(Get-ChildItem -Path $Root -Recurse -File -Filter 'build_verify.log' -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\\.git\\' })
foreach ($lg in $logs) {
    $tail = @(Get-Content -LiteralPath $lg.FullName -Tail 60 -ErrorAction SilentlyContinue)
    $line = @($tail | Where-Object { $_ -match 'Error\(s\)' } | Select-Object -Last 1)
    $res = if ($line.Count -gt 0) { ($line[0]).Trim() } else { '未找到统计行' }
    $compileRows += [PSCustomObject]@{ F = (Get-Rel $lg.FullName); R = $res; T = $lg.LastWriteTime.ToString('yyyy-MM-dd HH:mm') }
}
$compileSummary = '无编译记录'
if ($compileRows.Count -gt 0) {
    $compileSummary = (@($compileRows | ForEach-Object { $_.F + ' → ' + $_.R }) -join ' ｜ ')
}
if (-not (Test-Path -LiteralPath (Join-Path $P1 'build_verify.log'))) {
    $compileSummary = $compileSummary + ' ｜ Project_1 无记录（建议跑一次 build_keil.bat）'
}

# ===============================================================
# 6) git 状态
# ===============================================================
$gitRepo = $false; $gitBranch = ''; $gitStatus = @(); $gitLog = @(); $gitTracked = 0
try {
    $chk = & git -C $Root rev-parse --is-inside-work-tree 2>$null
    if ($LASTEXITCODE -eq 0 -and (($chk -join '').Trim()) -eq 'true') {
        $gitRepo = $true
        $gitBranch = ((& git -C $Root rev-parse --abbrev-ref HEAD 2>$null) | Select-Object -First 1)
        $gitStatus = @(& git -C $Root -c core.quotepath=false status --short 2>$null)
        $gitLog    = @(& git -C $Root log --oneline -5 2>$null)
        $gitTracked = @(& git -C $Root ls-files 2>$null).Count
    }
} catch { }
$hasGitignore = Test-Path -LiteralPath (Join-Path $Root '.gitignore')

# ===============================================================
# 7) 仓库卫生
# ===============================================================
$bigFiles = @(Get-ChildItem -Path $Root -Recurse -File -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Length -gt 5MB -and $_.FullName -notmatch '\\\.git\\' } |
    Sort-Object Length -Descending)

$prodNames = @('Objects', 'Listings', 'build', '.qtcreator')
$prodDirs = @(Get-ChildItem -Path $Root -Recurse -Directory -Force -ErrorAction SilentlyContinue |
    Where-Object { $prodNames -contains $_.Name -and $_.FullName -notmatch '\\\.git\\' })
$prodRows = @()
$prodTotal = [double]0
foreach ($pd in $prodDirs) {
    $sz = Get-DirSize $pd.FullName
    $prodTotal += $sz
    $prodRows += [PSCustomObject]@{ P = (Get-Rel $pd.FullName); MB = ($sz / 1MB) }
}

$dupGroups = @($bigFiles | Group-Object { $_.Name + '|' + $_.Length } | Where-Object { $_.Count -gt 1 })

$refDir = Join-Path $P2 '0001_标准模板库'
$refExists = Test-Path -LiteralPath $refDir

# ===============================================================
# 拼报告
# ===============================================================
Add-Rp '# Second_Project_Demo 项目综合巡检报告'
Add-Rp ''
Add-Rp ('> 生成时间 : ' + $now)
Add-Rp ('> 根目录   : ' + $Root)
Add-Rp ('> 巡检脚本 : project_check.ps1 v1.0')
Add-Rp ''

Add-Rp '## 0. 摘要'
Add-Rp ''
Add-Rp '| 维度 | 结果 |'
Add-Rp '|---|---|'
Add-Rp ('| 关键文件 | {0} 项，异常 {1} 项（空/过短/缺） |' -f $keyTotal, $keyBad)
Add-Rp ('| 空文件总数 | {0} 个 |' -f $emptyFiles.Count)
Add-Rp ('| 板1 驱动覆盖 | {0}/{1} |' -f $b1ok, $board1Needs.Count)
Add-Rp ('| 板2 驱动覆盖 | {0}/{1} |' -f $b2ok, $board2Needs.Count)
Add-Rp ('| 编译记录 | ' + $compileSummary + ' |')
Add-Rp ('| git | 分支 {0}；未跟踪/未提交 {1} 项；.gitignore {2} |' -f $gitBranch, $gitStatus.Count, $(if ($hasGitignore) { '有' } else { '无' }))
Add-Rp ('| 仓库卫生 | 大文件 {0} 个；编译产物目录 {1} 个共 {2:N1} MB |' -f $bigFiles.Count, $prodRows.Count, ($prodTotal / 1MB))
Add-Rp ('| 业务进度 | 板1 main.c: {0}；板2 main.c: {1} |' -f $main1, $main2)
Add-Rp ''

Add-Rp '## 1. 目录结构与体积'
Add-Rp ''
Add-Rp '| 目录 | 文件数 | 体积 |'
Add-Rp '|---|---|---|'
foreach ($d in $structRows) {
    $sz = Get-DirSize $d.FullName
    $cnt = Get-DirFileCount $d.FullName
    Add-Rp ('| ' + (Get-Rel $d.FullName) + (' | {0} | {1:N1} MB |' -f $cnt, ($sz / 1MB)))
}
Add-Rp ''

Add-Rp '## 2. 空文件（需要补写）'
Add-Rp ''
if ($emptyFiles.Count -eq 0) {
    Add-Rp '（无）'
} else {
    Add-Rp '| 文件 | 大小 |'
    Add-Rp '|---|---|'
    foreach ($f in $emptyFiles) { Add-Rp ('| ' + (Get-Rel $f.FullName) + ' | 0 字节 |') }
}
Add-Rp ''

Add-Rp '## 3. 关键文件清单'
Add-Rp ''
foreach ($g in $keyGroups) {
    Add-Rp ('### ' + $g.Title)
    Add-Rp ''
    Add-Rp '| 状态 | 信息 | 文件 | 说明 |'
    Add-Rp '|---|---|---|---|'
    foreach ($r in $g.Rows) {
        Add-Rp ('| ' + $r.S + ' | ' + $r.Info + ' | ' + $r.P + ' | ' + $r.D + ' |')
    }
    Add-Rp ''
}

Add-Rp '## 4. 架构吻合度（驱动模块覆盖）'
Add-Rp ''
Add-Rp '### 4.1 板1 检测端需求（对照方案13 采购清单）'
Add-Rp ''
Add-Rp '| 需求模块 | Project_1 现状 |'
Add-Rp '|---|---|'
foreach ($r in $b1rows) { Add-Rp ('| ' + $r.N + ' | ' + $r.St + ' |') }
Add-Rp ''
Add-Rp '### 4.2 板2 网关需求'
Add-Rp ''
Add-Rp '| 需求模块 | Project_2 现状 |'
Add-Rp '|---|---|'
foreach ($r in $b2rows) { Add-Rp ('| ' + $r.N + ' | ' + $r.St + ' |') }
Add-Rp ''
Add-Rp '### 4.3 公共底层（信息性）'
Add-Rp ''
Add-Rp '| 模块 | Project_1 | Project_2 |'
Add-Rp '|---|---|---|'
foreach ($r in $cmRows) { Add-Rp ('| ' + $r.N + ' | ' + $r.S1 + ' | ' + $r.S2 + ' |') }
Add-Rp ('| FreeRTOS 内核 | ' + $(if ($rtos1) { '有' } else { '缺' }) + ' | ' + $(if ($rtos2) { '有' } else { '缺' }) + ' |')
Add-Rp ''

Add-Rp '## 5. 编译记录（build_verify.log）'
Add-Rp ''
if ($compileRows.Count -eq 0) {
    Add-Rp '仓库内没有 build_verify.log —— 两个工程都建议跑一次 build_keil.bat 生成编译证据。'
} else {
    Add-Rp '| 日志 | 结果 | 时间 |'
    Add-Rp '|---|---|---|'
    foreach ($r in $compileRows) { Add-Rp ('| ' + $r.F + ' | ' + $r.R + ' | ' + $r.T + ' |') }
}
Add-Rp ''

Add-Rp '## 6. git 状态'
Add-Rp ''
if (-not $gitRepo) {
    Add-Rp '当前目录不是 git 仓库（或未安装 git）。'
} else {
    Add-Rp ('- 当前分支 : ' + $gitBranch)
    Add-Rp ('- 已跟踪文件 : ' + $gitTracked + ' 个')
    Add-Rp ('- .gitignore : ' + $(if ($hasGitignore) { '有' } else { '无（建议补：Objects/Listings/build/uvguix/JLinkSettings/build_verify.log 等）' }))
    Add-Rp ('- 最近提交 :')
    if ($gitLog.Count -eq 0) { Add-Rp '  （无）' } else { foreach ($l in $gitLog) { Add-Rp ('  - ' + $l) } }
    Add-Rp ('- 未提交/未跟踪（前 20 条）:')
    if ($gitStatus.Count -eq 0) {
        Add-Rp '  （干净）'
    } else {
        $show = @($gitStatus | Select-Object -First 20)
        foreach ($s in $show) { Add-Rp ('  - ' + $s) }
        if ($gitStatus.Count -gt 20) { Add-Rp ('  - ...（共 ' + $gitStatus.Count + ' 条）') }
    }
}
Add-Rp ''

Add-Rp '## 7. 仓库卫生'
Add-Rp ''
Add-Rp '### 7.1 大文件（>5MB）'
Add-Rp ''
if ($bigFiles.Count -eq 0) {
    Add-Rp '（无）'
} else {
    Add-Rp '| 大小 | 文件 |'
    Add-Rp '|---|---|'
    foreach ($f in $bigFiles) { Add-Rp ('| {0:N1} MB | {1} |' -f ($f.Length / 1MB), (Get-Rel $f.FullName)) }
}
Add-Rp ''
Add-Rp '### 7.2 编译产物目录（提交前应加入 .gitignore 或清理）'
Add-Rp ''
if ($prodRows.Count -eq 0) {
    Add-Rp '（无）'
} else {
    Add-Rp '| 目录 | 体积 |'
    Add-Rp '|---|---|'
    foreach ($r in $prodRows) { Add-Rp ('| ' + $r.P + (' | {0:N1} MB |' -f $r.MB)) }
}
Add-Rp ''
Add-Rp '### 7.3 重复大文件（同名同大小出现多次）'
Add-Rp ''
if ($dupGroups.Count -eq 0) {
    Add-Rp '（无）'
} else {
    foreach ($g in $dupGroups) {
        Add-Rp ('- ' + $g.Name.Split('|')[0] + ('（{0} 份）:' -f $g.Count))
        foreach ($f in $g.Group) { Add-Rp ('  - ' + (Get-Rel $f.FullName)) }
    }
}
Add-Rp ''
Add-Rp '### 7.4 参考夹 / 临时目录'
Add-Rp ''
if ($refExists) {
    Add-Rp '- [提示] Project_2 下存在参考夹「0001_标准模板库」——按工作流编译通过后应删除（由用户操作）。'
} else {
    Add-Rp '- 参考夹已清理（无 0001_标准模板库）。'
}
Add-Rp ''

Add-Rp '## 8. 建议待办（按检测结果自动生成）'
Add-Rp ''
$todo = New-Object System.Collections.ArrayList
if ($keyBad -gt 0) { [void]$todo.Add('处理关键文件异常 ' + $keyBad + ' 项（见第 3 节：空/过短/缺）') }
if ($emptyFiles.Count -gt 0) { [void]$todo.Add('补写空文件 ' + $emptyFiles.Count + ' 个（见第 2 节）') }
$miss1 = @($b1rows | Where-Object { $_.St -eq '缺' })
if ($miss1.Count -gt 0) { [void]$todo.Add('板1 缺驱动 ' + $miss1.Count + ' 个：' + (($miss1 | ForEach-Object { $_.N }) -join '、')) }
$miss2 = @($b2rows | Where-Object { $_.St -eq '缺' })
if ($miss2.Count -gt 0) { [void]$todo.Add('板2 缺驱动 ' + $miss2.Count + ' 个：' + (($miss2 | ForEach-Object { $_.N }) -join '、')) }
if (-not $hasGitignore) { [void]$todo.Add('补 .gitignore（Objects/Listings/build/.qtcreator/*.uvguix.*/JLinkSettings.ini/build_verify.log/*.o/*.exe 等）后再提交') }
if ($gitStatus.Count -gt 0) { [void]$todo.Add('git 有 ' + $gitStatus.Count + ' 项未提交/未跟踪，整理后提交') }
if ($refExists) { [void]$todo.Add('删除 Project_2 参考夹「0001_标准模板库」（用户操作）') }
if (-not (Test-Path -LiteralPath (Join-Path $P1 'build_verify.log'))) { [void]$todo.Add('为 Project_1 跑一次 build_keil.bat，留下 0 Error/0 Warning 证据') }
if ($dupGroups.Count -gt 0) { [void]$todo.Add('清理重复大文件（见 7.3，可保留一份或移出仓库）') }
if ($todo.Count -eq 0) {
    Add-Rp '- 全部通过，无需处理。'
} else {
    foreach ($t in $todo) { Add-Rp ('- [ ] ' + $t) }
}
Add-Rp ''

# ===============================================================
# 写报告 + 控制台摘要
# ===============================================================
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
try {
    [System.IO.File]::WriteAllText($Report, $script:rp.ToString(), $utf8NoBom)
    Write-Host ''
    Write-Host ('[巡检完成] 报告已生成: ' + $Report)
} catch {
    Write-Host ('[警告] 报告写入失败: ' + $_.Exception.Message)
}

Write-Host ''
Write-Host '===== 摘要 ====='
Write-Host ('  关键文件  : {0} 项，异常 {1} 项' -f $keyTotal, $keyBad)
Write-Host ('  空文件    : {0} 个' -f $emptyFiles.Count)
Write-Host ('  板1 驱动  : {0}/{1}' -f $b1ok, $board1Needs.Count)
Write-Host ('  板2 驱动  : {0}/{1}' -f $b2ok, $board2Needs.Count)
Write-Host ('  编译      : ' + $compileSummary)
Write-Host ('  git       : 分支 {0}；未提交 {1} 项；.gitignore {2}' -f $gitBranch, $gitStatus.Count, $(if ($hasGitignore) { '有' } else { '无' }))
Write-Host ('  卫生      : 大文件 {0} 个；产物目录 {1} 个共 {2:N1} MB' -f $bigFiles.Count, $prodRows.Count, ($prodTotal / 1MB))
Write-Host ''
if ($todo.Count -gt 0) {
    Write-Host '===== 待办 ====='
    foreach ($t in $todo) { Write-Host ('  - ' + $t) }
} else {
    Write-Host '全部通过，无需处理。'
}
