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
from pathlib import Path

# ── 默认可执行/工具路径(可用参数或环境变量覆盖) ──────────────────────
DEF_QT = os.environ.get("QTDIR", r"C:/Container/System/Library/QT/5.15.2/msvc2019")
DEF_EVB = os.environ.get(
    "EVB_CONSOLE", r"C:/Program Files (x86)/Enigma Virtual Box/enigmavbconsole.exe")
DEF_GENERATOR = ("Visual Studio 16 2019", "Win32")   # 与本仓库 build/ 现状一致

ROOT = Path(__file__).resolve().parent.parent


def log(msg):
    print(f"[package] {msg}", flush=True)


def fail(msg, code=1):
    print(f"[package][ERROR] {msg}", file=sys.stderr, flush=True)
    sys.exit(code)


def run(cmd, **kw):
    log("$ " + " ".join(str(c) for c in cmd))
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

    out = pack(exe, args.evb)
    size_mb = out.stat().st_size / 1024 / 1024
    log(f"完成 -> {out}  ({size_mb:.1f} MB)  版本 {ver}")


if __name__ == "__main__":
    main()
