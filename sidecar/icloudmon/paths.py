# ======================================================================
#  paths.py  -  本机 iCloud 相关路径与常量 (对应 inc/IcloudPaths.h)
#
#  设计要点与 C++ 版保持一致:
#    所有路径集中此处, 不散落到各模块;
#    优先级: 环境变量 > 用户配置(注册表) > 内置默认值。
#
#  ★ 用户配置的存储位置必须和 Qt 的 QSettings(NativeFormat, UserScope)
#    完全一致, 否则两边读到的同步根目录会不一样:
#      HKEY_CURRENT_USER\Software\iCloudMon\iCloudMon
#    键名里的 '/' 在注册表里表现为子键 (QSettings NativeFormat 行为),
#    所以 "ui/closeToTray" -> ...\iCloudMon\ui 下的 closeToTray 值。
#
#  ★ 组织名曾是 zvozve(作者账号名), v1.0.4 起统一为产品名 iCloudMon。
#    老用户升级时 C++ 侧 main.cpp 的 migrateLegacyUserData() 会把
#    HKCU\Software\zvozve\iCloudMon 一次性搬到新键。
# ======================================================================
from __future__ import annotations

import os
import winreg
from pathlib import Path

ORG = "iCloudMon"
APP = "iCloudMon"

# 需要管理的 iCloud 进程 (与 C++ 四个稳定进程名一致)
PROCESS_NAMES = [
    "iCloudDrive.exe",
    "iCloudHome.exe",
    "iCloudCKKS.exe",
    "ApplePhotoStreams.exe",
]

DEFAULT_PACKAGE_FAMILY = "AppleInc.iCloud_nzyj5cx40ttqa"
DEFAULT_APP_ID = "iCloud"

_REG_ROOT = rf"Software\{ORG}\{APP}"


# ── 用户配置 (QSettings NativeFormat 兼容读写) ──────────────────────
def _reg_path(name: str) -> tuple[str, str]:
    """'ui/closeToTray' -> ('Software\\iCloudMon\\iCloudMon\\ui', 'closeToTray')"""
    parts = name.replace("/", "\\").strip("\\").split("\\")
    return "\\".join([_REG_ROOT] + parts[:-1]), parts[-1]


def setting_get(name: str, default=None):
    sub, value = _reg_path(name)
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, sub) as k:
            v, _ = winreg.QueryValueEx(k, value)
            return v
    except OSError:
        return default


def setting_set(name: str, value) -> bool:
    sub, val = _reg_path(name)
    try:
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, sub) as k:
            winreg.SetValueEx(k, val, 0, winreg.REG_SZ, str(value))
        return True
    except OSError:
        return False


def setting_delete(name: str) -> bool:
    sub, val = _reg_path(name)
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, sub, 0,
                            winreg.KEY_SET_VALUE) as k:
            winreg.DeleteValue(k, val)
        return True
    except OSError:
        return False


# ── 同步根目录 ──────────────────────────────────────────────────────
#
# ★ 解析优先级(与 C++ 侧 inc/IcloudPaths.h 的说明一致):
#     ① 环境变量 ICLOUD_DRIVE_ROOT   (调试)
#     ② 注册表 driveRoot             (用户在设置页显式覆盖)
#     ③ 数据库推导                    ← 权威源, 见 snapshot.current_drive_root()
#     ④ %USERPROFILE%\iCloudDrive    (标准安装位置, 机器无关兜底)
#
#   这里**只提供 ④ 的兜底值**; 把 ①②③ 串起来的那一步统一放在
#   snapshot.current_drive_root() 里(它才拿得到数据库), 避免同一件事
#   在多个模块各写一份。
#
#   ★ 曾经这里硬编码成 "C:/Container/Cloud/iCloudDrive" —— 那是作者本机
#     自己改过的位置, 换台机器必然指空。已删除。
def drive_root_fallback() -> str:
    """标准安装下 iCloud Drive 的同步根: %USERPROFILE%\\iCloudDrive。"""
    return str(Path.home() / "iCloudDrive").replace("\\", "/")


def set_drive_root(path: str) -> bool:
    return setting_set("driveRoot", path)


def clear_drive_root() -> bool:
    return setting_delete("driveRoot")


# ── Local AppData / Store 包目录 ────────────────────────────────────
def local_app_data() -> str:
    env = os.environ.get("LOCALAPPDATA")
    if env:
        return env.replace("\\", "/")
    return str(Path.home() / "AppData" / "Local").replace("\\", "/")


def package_dir() -> str:
    """Store 版 iCloud 的包目录。包族名后缀由 Apple 证书决定, 全球一致;
    仅在默认名不存在时退化为枚举 AppleInc.iCloud*。"""
    env = os.environ.get("ICLOUD_PKG_DIR")
    if env:
        return env.replace("\\", "/")

    base = Path(local_app_data()) / "Packages"
    fixed = base / DEFAULT_PACKAGE_FAMILY
    if fixed.is_dir():
        return str(fixed).replace("\\", "/")

    hits = sorted(p for p in base.glob("AppleInc.iCloud*") if p.is_dir())
    if hits:
        return str(hits[0]).replace("\\", "/")
    return str(fixed).replace("\\", "/")


# ── 各类数据源 ──────────────────────────────────────────────────────
def db_dir() -> str:
    return package_dir() + "/LocalCache/Local/Apple Inc/iCloudDrive"


def db_file() -> str:
    return db_dir() + "/iCloudDrive.db"


def bookmarks_db() -> str:
    return (package_dir()
            + "/LocalCache/Local/Apple Inc/ChromeBookmarks/ChromeBookmarks.db")


def keychain_db() -> str:
    return (package_dir()
            + "/LocalCache/Roaming/Apple Computer/Keychains/keychain-2.db")


def media_stream_db() -> str:
    return (package_dir()
            + "/LocalCache/Roaming/Apple Computer/MediaStream/local.db")


def photo_library_dir() -> str:
    return package_dir() + "/LocalCache/Local/Apple Inc/iCloudPhotoLibrary"


def quota_pref_file() -> str:
    return (package_dir()
            + "/LocalCache/Roaming/Apple Computer/Preferences/iCloudWinPref.plist")


# ── 启动 iCloud (Store 应用 AUMID) ──────────────────────────────────
def icloud_aumid() -> str:
    """<PackageFamilyName>!<Application Id>
    ★ 包族名取自 package_dir() 末级目录名, 不写死发布者 Id 后缀。"""
    app_id = os.environ.get("ICLOUD_APP_ID") or DEFAULT_APP_ID
    family = Path(package_dir()).name
    return f"{family}!{app_id}"


# ── 状态历史 (卡死检测基线) ─────────────────────────────────────────
def state_file() -> str:
    """与 QStandardPaths::AppDataLocation 对齐:
    %APPDATA%/<org>/<app>/icloud_state.jsonl"""
    appdata = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
    d = Path(appdata) / ORG / APP
    d.mkdir(parents=True, exist_ok=True)
    return str(d / "icloud_state.jsonl").replace("\\", "/")
