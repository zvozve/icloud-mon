# ======================================================================
#  icloudmon  -  iCloudMon 的 Python 旁路数据层 (方案 A)
#
#  职责边界: 只负责"取数 + 判定", 不做界面、不做托盘、不做通知。
#  界面仍在 Qt/QML 侧, 通过 stdin/stdout 的 NDJSON 协议通信
#  (见 serve.py 与 sidecar/README.md)。
# ======================================================================
__version__ = "0.1.0"

from .snapshot import Snapshot, sample, human_size  # noqa: F401
from .stall import StallInfo, detect_stall, reset_history  # noqa: F401
from .bridge import build  # noqa: F401
from .procs import running_processes  # noqa: F401
