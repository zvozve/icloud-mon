#!/usr/bin/env python3
# ======================================================================
#  package.py  -  iCloudMon 一键打包(编译 -> 部署 Qt -> 打进单文件 exe)
#
#  一条命令出单文件绿色版, 只需要知道"版本号"这一件事, 其余全部自动探测:
#     编译(MSVC) -> windeployqt 收依赖 -> Enigma Virtual Box 压成一个 exe
#
#  用法:
#    python packaging/package.py                    # 用 CMakeLists 里的当前版本
#    python packaging/package.py --version 1.0.4    # 先把版本改成 1.0.4 再打包
#    python packaging/package.py --skip-build       # 只重新打包(不重编译)
#    python packaging/package.py --qt D:/Qt/5.15.2/msvc2019 --evb <console路径>
#
#  版本号单一真相源在 CMakeLists.txt 的 project(iCloudMon VERSION x.y.z),
#  代码通过 APP_VERSION 宏引用; 本脚本 --version 也只改这一处。
#
#  产物: dist/iCloudMon.exe  (单文件, 双击即用)
# ======================================================================
import argparse
import os
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

# ── 默认可执行/工具路径(可用参数或环境变量覆盖) ──────────────────────
# ★ Qt 路径必须与构建用的 kit 一致: 拿 Qt5 的 windeployqt 去部署 Qt6 的 exe
#   会打印 "does not seem to be a Qt executable" 然后失败(因为 Qt5 的
#   windeployqt 只认 Qt5 的导入表)。Qt5 32 位套件仍留在
#   .../QT/5.15.2/msvc2019, 供公司项目使用, 本工程不用它。
DEF_QT = os.environ.get("QTDIR", r"C:/Container/System/Library/QT/6.8.3/msvc2022_64")
DEF_EVB = os.environ.get(
    "EVB_CONSOLE", r"C:/Program Files (x86)/Enigma Virtual Box/enigmavbconsole.exe")
DEF_GENERATOR = ("Visual Studio 17 2022", "x64")   # 与本仓库 build/ 现状一致

ROOT = Path(__file__).resolve().parent.parent

# ── 内嵌 Python 旁路运行时 ───────────────────────────────────────────
#
#  数据层在 Python 旁路(sidecar/serve.py), C++ 侧只用它的 JSON。旁路是
#  **纯标准库**实现(json/os/sqlite3/winreg/ctypes/threading...), 所以内嵌
#  python-embed 就够, 不需要 site-packages, 整体约 11 MB。
#
#  不内嵌的后果: 单文件 exe 找不到旁路 -> 界面一直显示"监控不可用",
#  一个数据都取不到。所以这一步不是优化, 是"发布版能不能用"的前提。
#
#  部署布局(与 SidecarClient 的查找顺序对应):
#    <exe目录>/python-embed/python.exe    ← 内嵌解释器
#    <exe目录>/sidecar/serve.py           ← 旁路入口
PY_EMBED_VERSION = "3.13.14"
PY_EMBED_URL = ("https://www.python.org/ftp/python/{v}/"
                "python-{v}-embed-amd64.zip").format(v=PY_EMBED_VERSION)
PY_EMBED_ZIP = ROOT / "build-tools" / "python-embed-amd64.zip"


def log(msg):
    print(f"[package] {msg}", flush=True)


def fail(msg, code=1):
    print(f"[package][ERROR] {msg}", file=sys.stderr, flush=True)
    sys.exit(code)


def _clean_env() -> dict:
    """返回一份"没有大小写重复键"的环境副本, 交给子进程用。

    ★ Windows 的环境变量名大小写不敏感, 但**可以同时存在** HTTP_PROXY 和
      http_proxy。MSBuild(以及 VS 安装器)内部用 .NET 的 StringDictionary
      装载环境变量, 碰到这种重复会直接抛
        Item has already been added. Key in dictionary: 'HTTPS_PROXY'
         Key being added: 'https_proxy'
      表现是莫名其妙的 MSB6001 / 0x80070057, 报告里全是 MSBuild 内部调用栈,
      跟被编译的代码一点关系都没有 —— 极难往"环境变量"上想(踩过一次)。
      这里统一只保留先出现的那一个, 让构建与环境无关。
    """
    out: dict = {}
    seen: set = set()
    for k, v in os.environ.items():
        lk = k.lower()
        if lk in seen:
            continue
        seen.add(lk)
        out[k] = v
    return out


def run(cmd, **kw):
    log("$ " + " ".join(str(c) for c in cmd))
    kw.setdefault("env", _clean_env())
    return subprocess.run([str(c) for c in cmd], **kw)


# ── 版本读写 ─────────────────────────────────────────────────────────
def read_version(cmake: Path) -> str:
    m = re.search(r"project\(\s*iCloudMon\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
                  cmake.read_text(encoding="utf-8"))
    if not m:
        fail(f"找不到 project(iCloudMon VERSION ...) : {cmake}")
    return m.group(1)


def write_version(cmake: Path, ver: str) -> None:
    txt = cmake.read_text(encoding="utf-8")
    new, n = re.subn(
        r"(project\(\s*iCloudMon\s+VERSION\s+)[0-9]+\.[0-9]+\.[0-9]+",
        r"\g<1>" + ver, txt)
    if n == 0:
        fail(f"未能在 CMakeLists.txt 写入版本号: {cmake}")
    cmake.write_text(new, encoding="utf-8")
    log(f"版本号已写入 CMakeLists.txt -> {ver}")


# ── 编译 ─────────────────────────────────────────────────────────────
def build(qt: str, config: str) -> None:
    cache = ROOT / "build" / "CMakeCache.txt"
    if not cache.exists():
        gen, plat = DEF_GENERATOR
        log("首次配置 CMake ...")
        r = run(["cmake", "-S", str(ROOT), "-B", str(ROOT / "build"),
                 "-G", gen, "-A", plat,
                 f"-DCMAKE_PREFIX_PATH={qt}",
                 f"-DCMAKE_BUILD_TYPE={config}"])
        if r.returncode != 0:
            fail("CMake 配置失败(是否已装 MSVC 2019 + Qt?)")
    else:
        # 复用已有 build/(生成器类型由缓存决定, 不重配, 避免生成器冲突)
        log("复用已有 build/ 缓存")
    r = run(["cmake", "--build", str(ROOT / "build"), "--config", config])
    if r.returncode != 0:
        fail("编译失败")


def find_exe(config: str) -> Path:
    for cand in (ROOT / "build" / config / "iCloudMon.exe",
                 ROOT / "build" / "iCloudMon.exe"):
        if cand.exists():
            return cand
    fail("找不到编译产物 iCloudMon.exe(检查 build/ 目录)")


# ── 内嵌 Python 旁路运行时 ───────────────────────────────────────────
def ensure_python_embed() -> Path:
    """本地没有就下载 python-embed 压缩包(约 11 MB), 返回其路径。"""
    if PY_EMBED_ZIP.exists() and PY_EMBED_ZIP.stat().st_size > 1_000_000:
        return PY_EMBED_ZIP
    PY_EMBED_ZIP.parent.mkdir(parents=True, exist_ok=True)
    log(f"下载 python-embed {PY_EMBED_VERSION}: {PY_EMBED_URL}")
    tmp = PY_EMBED_ZIP.with_name(PY_EMBED_ZIP.name + ".part")
    with urllib.request.urlopen(PY_EMBED_URL, timeout=180) as r, \
            open(tmp, "wb") as f:
        shutil.copyfileobj(r, f)
    tmp.replace(PY_EMBED_ZIP)
    log(f"已缓存: {PY_EMBED_ZIP} "
        f"({PY_EMBED_ZIP.stat().st_size / 1024 / 1024:.1f} MB)")
    return PY_EMBED_ZIP


def stage_sidecar(exe_dir: Path) -> None:
    """把内嵌 Python + 旁路脚本放进部署目录, 由 EVB 一起打进单文件。

    放进部署目录(而不是另设目录)是刻意的: gen_evb.py 打包的就是
    build/<Config>/ 整棵树, 放进去就自动被收走, 不需要额外登记。
    """
    zip_path = ensure_python_embed()

    embed = exe_dir / "python-embed"
    if embed.exists():
        shutil.rmtree(embed)
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(embed)

    sc = exe_dir / "sidecar"
    if sc.exists():
        shutil.rmtree(sc)
    (sc / "icloudmon").mkdir(parents=True)

    shutil.copy2(ROOT / "sidecar" / "serve.py", sc / "serve.py")
    # 只拷 .py: 运行时的 __pycache__ 是本地产物, 进包会带一堆无效字节码
    n = 0
    for p in sorted((ROOT / "sidecar" / "icloudmon").glob("*.py")):
        shutil.copy2(p, sc / "icloudmon" / p.name)
        n += 1

    mb = sum(f.stat().st_size for f in embed.rglob("*") if f.is_file()) \
        / 1024 / 1024
    log(f"旁路已就位: python-embed {mb:.1f} MB + serve.py + {n} 个模块")


# ── 部署 Qt 运行时 ───────────────────────────────────────────────────
def deploy_qt(qt: str, exe: Path, config: str) -> None:
    windeployqt = Path(qt) / "bin" / "windeployqt.exe"
    if not windeployqt.exists():
        fail(f"找不到 windeployqt: {windeployqt}")
    r = run([windeployqt, "--release", "--no-translations",
             f"--qmldir={ROOT / 'ui'}", str(exe)])
    if r.returncode != 0:
        fail("windeployqt 失败")


# ── 打进单文件 exe ───────────────────────────────────────────────────
def pack(exe: Path, evb_console: str) -> Path:
    dist = ROOT / "dist"
    out_exe = dist / "iCloudMon.exe"
    gen_evb = ROOT / "packaging" / "gen_evb.py"
    r = run([sys.executable, str(gen_evb), str(exe.parent), str(exe), str(out_exe)])
    if r.returncode != 0:
        fail("生成 EVB 工程失败")
    evb = dist / "iCloudMon.evb"
    if not Path(evb_console).exists():
        fail(f"找不到 Enigma Virtual Box 命令行: {evb_console}")
    r = run([evb_console, str(evb)])
    if r.returncode != 0 or not out_exe.exists():
        fail("Enigma Virtual Box 打包失败")
    return out_exe


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--version", help="先设版本号(只改 CMakeLists.txt 一处)再打包")
    ap.add_argument("--qt", default=DEF_QT, help="Qt 5.15 msvc2019 目录")
    ap.add_argument("--evb", default=DEF_EVB, help="enigmavbconsole.exe 路径")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--skip-build", action="store_true", help="跳过编译/部署, 只重新打包")
    ap.add_argument("--no-sidecar", action="store_true",
                    help="不内嵌 Python 旁路(仅供排障: 打出来的 exe 取不到数据)")
    args = ap.parse_args()

    cmake = ROOT / "CMakeLists.txt"
    if args.version:
        write_version(cmake, args.version)
    ver = read_version(cmake)
    log(f"打包版本: {ver}")

    if args.skip_build:
        exe = find_exe(args.config)
    else:
        build(args.qt, args.config)
        exe = find_exe(args.config)
        deploy_qt(args.qt, exe, args.config)

    if args.no_sidecar:
        log("! --no-sidecar: 跳过旁路内嵌(打出来的 exe 会一直显示「监控不可用」)")
    else:
        stage_sidecar(exe.parent)

    out = pack(exe, args.evb)
    size_mb = out.stat().st_size / 1024 / 1024
    log(f"完成 -> {out}  ({size_mb:.1f} MB)  版本 {ver}")


if __name__ == "__main__":
    main()
