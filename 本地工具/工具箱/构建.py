# -*- coding: utf-8 -*-
r"""重新构建本工作区里的 Qt 程序（工具箱 + Git 上传）。

改动 工具箱\shared\ 或 Git上传\src\ 下的源码后必须跑一次。

为什么需要这个脚本：
    qmake 生成 Makefile 时要探测编译器的默认 include / lib 路径，
    本机环境下这一步会失败（qmake 报 QProcess 建管道失败 / failed to parse
    default include paths）。脚本先把探测结果写进各 build\.qmake.stash，
    qmake 读缓存就会跳过探测。

为什么工具链要"自动找"：
    原来脚本里写死 E:\Qt\...，换台电脑（或 Qt 装到别的盘）就直接跑不起来。
    现在按 QTDIR / QT_ROOT 环境变量 → 常见安装位（各盘 \Qt）→ PATH 的顺序找，
    找不到给一条**看得懂**的报错，而不是丢一句"qmake 不是内部或外部命令"。

历史：2026-10 之前这里编三个程序（综合程序 + 协议工作台 + 项目起手助手两个独立入口）。
后两者本来就是 Toolbox.exe 里的页面，独立入口只是重复，已随源码归档删除。
2026-10-15 起把 Git 上传也纳进来（它仍是独立程序，只是共用这一个构建脚本），
这样"重编全部 Qt 程序"就是一条命令。

用法：
    python 本地工具\工具箱\构建.py
    python 本地工具\工具箱\构建.py --clean     # 先清中间产物再编
    python 本地工具\工具箱\构建.py --cleanonly # 只清中间产物，不编译
"""
import glob
import os
import shutil
import subprocess
import sys

# 控制台按 UTF-8 输出：本脚本大量中文提示，在英文 Windows（默认 cp1252 代码页）
# 上不设这一句会满屏乱码。
try:
    sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))        # 本地工具/工具箱

# 相对 HERE 的目标：(程序目录, .pro 名, 产物名)
TARGETS = [
    ('程序/嵌入式工具箱', 'Toolbox.pro', 'Toolbox'),
    ('../Git上传', 'GitUploader.pro', 'GitUploader'),     # 独立程序，与工具箱同级
]

MID_FILES = ('.o', '.cpp')          # 只删 moc_*.cpp，不删源码
CLEAN_NAMES = ('Makefile', 'Makefile.Debug', 'Makefile.Release', '.qmake.stash')


# ---------------------------------------------------------------- 工具链探测

def qt_root_candidates():
    r"""Qt 安装根目录的候选（Qt 根下应有 6.x\mingw_64\bin 与 Tools\mingw*\bin）。"""
    roots = []
    for env in ('QT_ROOT', 'QTDIR'):
        v = os.environ.get(env)
        if v:
            roots.append(v)
    # 常见安装位：各盘根下的 \Qt、\Qt6
    for drive in ('C:', 'D:', 'E:', 'F:', 'G:'):
        roots += [drive + r'\Qt', drive + r'\Qt6', drive + r'\Qt5']
    return roots


def find_qt_kit():
    r"""找 Qt 的 mingw kit：返回 (qt_bin, mingw_bin)；找不到返回 (None, None)。

    Qt 的 mingw kit 布局：<root>\<版本>\mingw_64\bin\qmake.exe
    配套编译器：        <root>\Tools\mingw*\bin\g++.exe
    """
    for root in qt_root_candidates():
        if not os.path.isdir(root):
            continue
        qm = sorted(glob.glob(os.path.join(root, '*', 'mingw*', 'bin', 'qmake.exe')))
        if not qm:
            continue
        qt_bin = os.path.dirname(qm[-1])                      # 取版本最高的那个
        gpp = sorted(glob.glob(os.path.join(root, 'Tools', 'mingw*', 'bin', 'g++.exe')))
        if gpp:
            return qt_bin, os.path.dirname(gpp[-1])
        # 找得到 Qt 却没有 MinGW：至少把 qt_bin 报回去，让报错更具体
        return qt_bin, None
    # 最后兜底：PATH 里现成的
    qmake = shutil.which('qmake') or shutil.which('qmake.exe')
    if qmake:
        return os.path.dirname(qmake), os.path.dirname(shutil.which('g++') or shutil.which('g++.exe') or '')
    return None, None


QT_BIN, MINGW_BIN = find_qt_kit()


def env():
    e = dict(os.environ)
    parts = [p for p in (QT_BIN, MINGW_BIN) if p]
    e['PATH'] = os.pathsep.join(parts + [e.get('PATH', '')])
    return e


def toolchain_ok():
    if not QT_BIN or not os.path.isfile(os.path.join(QT_BIN, 'qmake.exe')):
        print('[错误] 找不到 Qt 的 mingw kit（qmake.exe）。')
        print('       找过：QTDIR / QT_ROOT 环境变量，以及 C:\\Qt、D:\\Qt、E:\\Qt … 和 PATH。')
        print('       装一个 Qt 6.x + MinGW，或先设环境变量再跑，例如：')
        print(r'         set QT_ROOT=E:\Qt')
        return False
    if not MINGW_BIN or not os.path.isfile(os.path.join(MINGW_BIN, 'g++.exe')):
        print('[错误] 找到 Qt（%s），但没找到配套的 MinGW（g++.exe）。' % QT_BIN)
        print('       通常在同级 <Qt根>\\Tools\\mingw*\\bin 下；装 Qt 时勾上 MinGW 组件。')
        return False
    return True


def probe_compiler(e):
    """跑 g++ -E -v 提取 include / lib 搜索路径，供 qmake 缓存使用。"""
    gpp = os.path.join(MINGW_BIN, 'g++.exe')
    out = subprocess.run([gpp, '-E', '-v', '-xc++', '-'],
                         input=b'', stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, env=e).stdout.decode('utf-8', 'replace')
    lines = out.splitlines()
    inc, lib, grab = [], [], None
    for ln in lines:
        s = ln.strip()
        if s.startswith('#include <...> search starts here'):
            grab = inc
            continue
        if s.startswith('End of search'):
            grab = None
            continue
        if s.startswith('LIBRARY_PATH='):
            lib = [p for p in s[len('LIBRARY_PATH='):].split(';') if p]
            continue
        if grab is not None and s and not s.startswith('#'):
            grab.append(s)
    return inc, lib


def write_stash(build_dir, inc, lib):
    c = ['QMAKE_CXX.COMPILER_MACROS = QMAKE_GCC_MAJOR_VERSION QMAKE_GCC_MINOR_VERSION QMAKE_GCC_PATCH_VERSION',
         'QMAKE_CXX.QMAKE_GCC_MAJOR_VERSION = 13',
         'QMAKE_CXX.QMAKE_GCC_MINOR_VERSION = 1',
         'QMAKE_CXX.QMAKE_GCC_PATCH_VERSION = 0',
         'QMAKE_GCC_MAJOR_VERSION = 13',
         'QMAKE_GCC_MINOR_VERSION = 1',
         'QMAKE_GCC_PATCH_VERSION = 0',
         'QMAKE_CXX.INCDIRS = ' + ' '.join(inc),
         'QMAKE_CXX.LIBDIRS = ' + ' '.join(lib)]
    with open(os.path.join(build_dir, '.qmake.stash'), 'w',
              encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(c) + '\n')


# ---------------------------------------------------------------- 运行库兜底
# 打包好的 exe 要能在"没装 Qt / MinGW 的机器"上直接双击运行，所以 release\ 下必须齐
# 这几样。qmake 只编 exe，Qt 的 DLL 与插件目录历史上是手工拷的 —— 少一个就是
# 0xC0000135（找不到 DLL）连窗口都起不来（2026-10-17 真踩到：工具箱漏了三个 MinGW
# 运行库，只在装了 Qt 的本机能跑）。这里每次构建后自检一遍：缺的从工具链补，
# 工具链里也找不到就大声报出来，别让"能编过"骗成"能跑"。
RUNTIME_DLLS = ('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll',
                'D3Dcompiler_47.dll', 'opengl32sw.dll',
                'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
                'Qt6Svg.dll')
PLUGIN_DIRS = ('platforms', 'imageformats', 'iconengines', 'styles', 'tls',
               'generic', 'networkinformation', 'translations')
# Qt 插件的来源：<Qt 根>\<版本>\mingw_64\bin 的上一级还有个 plugins\
PLUGIN_SRC = os.path.join(os.path.dirname(QT_BIN), 'plugins') if QT_BIN else None


def ensure_runtime(release_dir):
    """补齐 release\\ 下的运行库与插件目录。返回 (补了哪些, 工具链里也找不到哪些)。"""
    copied, missing = [], []
    for name in RUNTIME_DLLS:
        dst = os.path.join(release_dir, name)
        if os.path.isfile(dst):
            continue
        for src_bin in (QT_BIN, MINGW_BIN):
            cand = os.path.join(src_bin, name) if src_bin else ''
            if cand and os.path.isfile(cand):
                shutil.copy2(cand, dst)
                copied.append(name)
                break
        else:
            missing.append(name)
    if PLUGIN_SRC and os.path.isdir(PLUGIN_SRC):
        for d in PLUGIN_DIRS:
            src, dst = os.path.join(PLUGIN_SRC, d), os.path.join(release_dir, d)
            if os.path.isdir(src) and not os.path.isdir(dst):
                shutil.copytree(src, dst)
                copied.append(d + '/')
    return copied, missing


def clean_build(build_dir):
    """删掉中间产物，保留 release\\ 里的 exe / dll / 插件目录。"""
    removed = 0
    for dirpath, dirnames, filenames in os.walk(build_dir):
        for fn in filenames:
            p = os.path.join(dirpath, fn)
            drop = (fn in CLEAN_NAMES
                    or fn.startswith('object_script.')
                    or fn == 'moc_predefs.h'
                    or (fn.startswith('moc_') and fn.endswith('.cpp'))
                    or (fn.endswith('.o')))
            if drop:
                os.remove(p)
                removed += 1
    # 空的 debug 目录也清掉
    dbg = os.path.join(build_dir, 'debug')
    if os.path.isdir(dbg) and not os.listdir(dbg):
        os.rmdir(dbg)
    return removed


def main(argv):
    do_clean = '--clean' in argv
    clean_only = '--cleanonly' in argv

    if clean_only:
        total = 0
        for rel, pro, tgt in TARGETS:
            bdir = os.path.join(HERE, rel, 'build')
            if not os.path.isdir(bdir):
                continue
            n = clean_build(bdir)
            total += n
            print('%s：清理 %d 个中间文件' % (tgt, n))
        print('\n共清理 %d 个。产物 exe / dll / 插件目录均保留。' % total)
        return 0

    if not toolchain_ok():
        return 1
    print('工具链：Qt  %s' % QT_BIN)
    print('        MinGW %s' % MINGW_BIN)

    e = env()
    inc, lib = probe_compiler(e)
    if not inc or not lib:
        print('无法从编译器探测 include / lib 路径，构建可能失败。')
        print('  include: %d 条, lib: %d 条' % (len(inc), len(lib)))
    else:
        print('编译器路径探测完成：include %d 条，lib %d 条' % (len(inc), len(lib)))

    fail = 0
    for rel, pro, tgt in TARGETS:
        base = os.path.join(HERE, rel)
        bdir = os.path.join(base, 'build')
        if not os.path.isfile(os.path.join(base, pro)):
            print('\n---- %s ----' % tgt)
            print('  找不到工程文件 %s，跳过。' % os.path.join(base, pro))
            continue
        os.makedirs(bdir, exist_ok=True)

        if do_clean:
            n = clean_build(bdir)
            print('\n---- %s ----（清理 %d 个中间文件）' % (tgt, n))
        else:
            print('\n---- %s ----' % tgt)

        write_stash(bdir, inc, lib)

        r = subprocess.run([os.path.join(QT_BIN, 'qmake.exe'),
                            '..\\' + pro, '-spec', 'win32-g++', 'CONFIG+=release'],
                           cwd=bdir, env=e, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        if r.returncode != 0:
            print('  qmake 失败：')
            print(r.stdout.decode('utf-8', 'replace').strip())
            fail += 1
            continue

        r = subprocess.run([os.path.join(MINGW_BIN, 'mingw32-make.exe'), '-j4'],
                           cwd=bdir, env=e, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        log = r.stdout.decode('utf-8', 'replace')
        errs = [l for l in log.splitlines() if 'error:' in l]
        exe = os.path.join(bdir, 'release', tgt + '.exe')
        if r.returncode != 0 or errs:
            print('  编译失败：')
            for l in errs[:10]:
                print('   ' + l)
            fail += 1
        elif not os.path.isfile(exe):
            print('  没找到产物 %s' % exe)
            fail += 1
        else:
            print('  OK  %s' % exe)
            copied, missing = ensure_runtime(os.path.dirname(exe))
            if copied:
                print('     补运行库：%s' % '、'.join(copied))
            if missing:
                print('     [警告] release 里还缺 %d 个运行库，工具链里也找不到：' % len(missing))
                print('       %s' % '、'.join(missing))
                print('       缺这些会在没装 Qt / MinGW 的机器上以 0xC0000135 起不来。')

    print()
    if fail:
        print('有 %d 个目标失败。' % fail)
    else:
        print('全部构建成功。')
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
