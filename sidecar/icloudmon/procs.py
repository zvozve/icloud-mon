# ======================================================================
#  procs.py  -  iCloud 进程检测 (对应 IcloudService::runningProcesses)
#
#  ★ 不要改用 `tasklist` 子进程逐个过滤。C++ 版实测过那条路: 每轮采样
#    派生 4 次子进程 + 阻塞等待, 在 GUI 线程上是 200~400ms 的停顿,
#    就是拖动窗口卡顿的主因。这里同样只读一次内核快照 (Toolhelp32),
#    耗时 1ms 量级 —— 旁路进程不阻塞 UI, 但没必要把已知的坑再踩一遍。
# ======================================================================
from __future__ import annotations

import ctypes
from ctypes import wintypes

from .paths import PROCESS_NAMES

TH32CS_SNAPPROCESS = 0x00000002
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
MAX_PATH = 260


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_wchar * MAX_PATH),
    ]


def running_processes() -> list[str]:
    """返回正在运行的 iCloud 进程名 (按 PROCESS_NAMES 顺序, 去重)。"""
    want = {n.lower(): n for n in PROCESS_NAMES}
    found: list[str] = []

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap == INVALID_HANDLE_VALUE:
        return found

    try:
        pe = PROCESSENTRY32W()
        pe.dwSize = ctypes.sizeof(pe)
        if k32.Process32FirstW(snap, ctypes.byref(pe)):
            while True:
                hit = want.get(pe.szExeFile.lower())
                if hit and hit not in found:
                    found.append(hit)
                if not k32.Process32NextW(snap, ctypes.byref(pe)):
                    break
    finally:
        k32.CloseHandle(snap)
    return found
