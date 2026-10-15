# 文风检查.ps1 —— 扫描注释与文档里的 AI 文风特征
#
# 用法：
#   pwsh -File 文风检查.ps1                          # 扫默认范围
#   pwsh -File 文风检查.ps1 -Root 'D:\GitHub_Project\EnvMonitor'
#   pwsh -File 文风检查.ps1 -ShowHits                 # 打印每条命中的文件名与行号
#   pwsh -File 文风检查.ps1 -Top 40
#
# 判定依据见 EnvMonitor\项目架构思路设计参考\注释与文档文风规范.md
# 命中数不代表必须改：像 "记住" 这类词在技术说明里偶尔是正当用法，按上下文判断。

[CmdletBinding()]
param(
    [string]$Root = '',
    [string[]]$Include = @('*.c', '*.h', '*.cpp', '*.hpp', '*.md', '*.txt', '*.py', '*.js', '*.html'),
    [string[]]$ExcludeDir = @('build', 'build_verify', 'build_dbg', '.git', 'node_modules', 'Objects', 'Listings', 'RTE'),
    [int]$Top = 25,
    [switch]$ShowHits
)

# 没给 -Root 就自己找仓库根：从脚本位置向上找含 .git 或 EnvMonitor 的目录。
# （原来写死 D:\GitHub_Project，换台电脑/换盘符就跑不起来。）
if (-not $Root) {
    $d = $PSScriptRoot
    while ($d -and -not (Test-Path (Join-Path $d 'EnvMonitor')) -and -not (Test-Path (Join-Path $d '.git'))) {
        $p = Split-Path $d -Parent
        if (-not $p -or $p -eq $d) { break }
        $d = $p
    }
    $Root = if ($d) { $d } else { (Get-Location).Path }
}

# 特征分三类：硬性装饰符号、修辞与情绪、说教与话术
$patterns = [ordered]@{
    # 图标与表情符号：杂项符号与 Dingbats（星号、对勾、叉号、警告三角等）、补充符号、变体选择符、emoji 平面
    '图标与表情符号'  = '[\u2600-\u27BF\u2B00-\u2BFF\uFE0F]|[\uD83C-\uD83E][\uDC00-\uDFFF]'
    '装饰符号 ◆'      = '◆'
    '修辞 值得称赞'   = '值得称赞'
    '修辞 亮点'       = '亮点'
    '修辞 踩雷'       = '踩雷'
    '修辞 名不副实'   = '名不副实'
    '修辞 说白了'     = '说白了'
    '修辞 顺手/顺便'  = '顺手|顺便'
    '修辞 恰恰'       = '恰恰'
    '修辞 反过来'     = '反过来'
    '修辞 其实'       = '其实'
    '修辞 极其/非常关键' = '极其重要|非常关键|极其关键'
    '修辞 一句话'     = '一句话'
    '口语 坑'         = '坑'
    '说教 你会发现'   = '你会发现'
    '说教 你可能想问' = '你可能想问'
    '说教 务必/千万别' = '务必|千万别'
    '说教 记住'       = '记住'
    '话术 面试/答辩'  = '面试|答辩|加分项'
    '话术 演示话术'   = '演示话术'
    '标点 全角感叹号' = '！'
    '标点 连续破折号' = '——[^—]*——'
    '自评 处理得很/设计得好' = '处理得很|设计得好|做得很好'
}

function Get-Text {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -eq 0) { return '' }
    # 先按 UTF-8 严格解码；失败再按 GBK 解码（main.c、uvprojx 等）
    try {
        $strict = New-Object System.Text.UTF8Encoding($false, $true)
        return $strict.GetString($bytes)
    } catch {
        return [System.Text.Encoding]::GetEncoding(936).GetString($bytes)
    }
}

# 规范文件本身要列举这些词，检查时必须排除，否则永远命中
$ExcludeFile = @('注释与文档文风规范.md', '文风检查.ps1')

# 显示相对路径要用绝对根，否则 -Root 传相对路径时会把同名的内层目录名误删
$RootAbs = (Resolve-Path -Path $Root).Path.TrimEnd('\')

$files = Get-ChildItem -Path $Root -Recurse -File -Include $Include -ErrorAction SilentlyContinue |
    Where-Object {
        $rel = $_.FullName   # 供下面按目录名排除用
        ($ExcludeDir | Where-Object { $rel -match "[\\/]$([regex]::Escape($_))[\\/]" }).Count -eq 0 -and
        ($ExcludeFile -notcontains $_.Name)
    }

"扫描根目录: $Root"
"文件数: $($files.Count)"
''

$total = [ordered]@{}
$fileHits = @{}

foreach ($f in $files) {
    $text = Get-Text -Path $f.FullName
    if ([string]::IsNullOrEmpty($text)) { continue }
    $lines = $text -split "`n"
    foreach ($k in $patterns.Keys) {
        $rx = [regex]$patterns[$k]
        $n = 0
        $hitLines = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt $lines.Length; $i++) {
            $m = $rx.Matches($lines[$i])
            if ($m.Count -gt 0) {
                $n += $m.Count
                if ($ShowHits) { [void]$hitLines.Add("      L$($i + 1): $($lines[$i].Trim())") }
            }
        }
        if ($n -gt 0) {
            if (-not $total.Contains($k)) { $total[$k] = 0 }
            $total[$k] += $n
            if (-not $fileHits.ContainsKey($f.FullName)) { $fileHits[$f.FullName] = [ordered]@{} }
            $fileHits[$f.FullName][$k] = $n
            if ($ShowHits) {
                "  [$k] $($f.FullName)"
                $hitLines | ForEach-Object { $_ }
            }
        }
    }
}

'===== 特征合计（按命中数降序）====='
$total.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "{0,6}  {1}" -f $_.Value, $_.Key }
''
"命中总数: $(($total.Values | Measure-Object -Sum).Sum)"
''
"===== 命中最多的文件（前 $Top）====="
$fileHits.GetEnumerator() |
    ForEach-Object { [pscustomobject]@{ File = $_.Key; Hits = ($_.Value.Values | Measure-Object -Sum).Sum } } |
    Sort-Object Hits -Descending | Select-Object -First $Top |
    ForEach-Object { "{0,6}  {1}" -f $_.Hits, $_.File.Replace("$RootAbs\", '') }
