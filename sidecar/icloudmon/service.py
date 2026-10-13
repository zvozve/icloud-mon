# ======================================================================
#  service.py  -  iCloud 启停 (对应 IcloudService 的 stopAll / startAll)
#
#  两个必须照抄的经验点, 别"优化":
#    1) 结束用 `taskkill /F /IM <四个进程名>`, 只依赖进程名, 与路径/版本无关;
#       taskkill 返回 128 = 进程不存在, 同样视为成功。
#    2) 启动 Store 应用必须 `explorer.exe shell:AppsFolder\<AUMID>`,
#       ★ 分隔符是**反斜杠**。用正斜杠 explorer 解析不了"应用启动"指令,
#       会静默回退打开默认目录(文档), 表现就是"启动没反应 + 弹出资源管理器"。
# ======================================================================
from __future__ import annotations

import subprocess
import time

from . import paths
from .procs import running_processes


def stop_all(timeout: float = 8.0) -> tuple[bool, str]:
    targets = paths.PROCESS_NAMES
    args = ["taskkill", "/F"]
    for n in targets:
        args += ["/IM", n]
    try:
        p = subprocess.run(args, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, "结束进程超时"
    if p.returncode in (0, 128):
        return True, f"已结束 {len(targets)} 个 iCloud 进程"
    return False, f"结束进程失败 (taskkill={p.returncode})"


def start_all(settle: float = 2.5) -> tuple[bool, str]:
    aumid = paths.icloud_aumid()
    # ★ 注意反斜杠: shell:AppsFolder\<AUMID>
    cmd = f"shell:AppsFolder\\{aumid}"
    try:
        subprocess.Popen(["explorer.exe", cmd])
    except OSError as e:
        return False, f"启动失败: {e}"

    time.sleep(settle)
    if running_processes():
        return True, "iCloud 已启动"
    return False, "已发起启动, 但未检测到进程"


def restart(settle_gap: float = 1.5, settle: float = 2.5) -> tuple[bool, str]:
    """结束 -> 等文件锁/句柄释放 -> 启动 (与 C++ restart 的 1.5s 间隔一致)。"""
    ok, msg = stop_all()
    if not ok:
        return False, msg
    time.sleep(settle_gap)
    return start_all(settle)
