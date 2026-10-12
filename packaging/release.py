#!/usr/bin/env python3
# ======================================================================
#  release.py  -  把打包好的单文件 exe 发布到 GitHub Releases
#
#  只用"仓库 + 版本号 + token"三样必要信息, 其余自动: 仓库从 git remote 读,
#  版本号从 CMakeLists.txt 读, 说明默认取 packaging/RELEASE_NOTES_v<版本>.md。
#
#  用法:
#    set GITHUB_TOKEN=ghp_xxx            (或 --token)
#    python packaging/release.py                      # 发当前版本
#    python packaging/release.py --version 1.0.3
#    python packaging/release.py --asset dist/iCloudMon.exe --tag v1.0.3
#    python packaging/release.py --notes-file packaging/RELEASE_NOTES_v1.0.3.md
#    python packaging/release.py --draft              # 先发草稿, 确认后再转正
#
#  不依赖 gh CLI, 直接走 GitHub REST API(urllib)。
# ======================================================================
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.request
import urllib.error
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
API = "https://api.github.com"


def log(m):
    print(f"[release] {m}", flush=True)


def fail(m, code=1):
    print(f"[release][ERROR] {m}", file=sys.stderr, flush=True)
    sys.exit(code)


def read_version() -> str:
    m = re.search(r"project\(\s*iCloudMon\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
                  (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not m:
        fail("找不到 CMakeLists.txt 里的版本号")
    return m.group(1)


def repo_slug() -> str:
    try:
        url = subprocess.check_output(
            ["git", "-C", str(ROOT), "remote", "get-url", "origin"],
            text=True).strip()
    except Exception as e:
        fail(f"读取 git remote 失败: {e}")
    m = re.search(r"github\.com[:/]([^/]+/[^/]+?)(?:\.git)?$", url)
    if not m:
        fail(f"无法从 remote 解析 owner/repo: {url}")
    return m.group(1)


def api(method: str, url: str, token: str, data=None, content_type="application/json"):
    headers = {
        "Authorization": f"token {token}",
        "Accept": "application/vnd.github+json",
        "User-Agent": "icloud-mon-release",
    }
    body = None
    if data is not None:
        body = data if isinstance(data, bytes) else json.dumps(data).encode()
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, json.loads(r.read().decode() or "{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode() or "{}")


def find_notes(ver: str) -> str:
    cand = ROOT / "packaging" / f"RELEASE_NOTES_v{ver}.md"
    if cand.exists():
        return cand.read_text(encoding="utf-8")
    log(f"未找到 {cand.name}, 使用最小说明")
    return f"## iCloudMon v{ver}\n\n单文件绿色版, 双击即用, 无需安装。\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version")
    ap.add_argument("--tag", help="默认 v<version>")
    ap.add_argument("--asset", help="默认 dist/iCloudMon.exe")
    ap.add_argument("--asset-name", help="上传后的文件名, 默认 iCloudMon-v<version>.exe")
    ap.add_argument("--notes-file")
    ap.add_argument("--token", help="默认读环境变量 GITHUB_TOKEN / GH_TOKEN")
    ap.add_argument("--draft", action="store_true")
    args = ap.parse_args()

    token = args.token or os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if not token:
        fail("缺少 token: 设环境变量 GITHUB_TOKEN, 或传 --token")

    ver = args.version or read_version()
    tag = args.tag or f"v{ver}"
    asset = Path(args.asset) if args.asset else (ROOT / "dist" / "iCloudMon.exe")
    asset_name = args.asset_name or f"iCloudMon-v{ver}.exe"
    slug = repo_slug()

    if not asset.exists():
        fail(f"找不到资产文件: {asset} (先跑 packaging/package.py)")
    notes = (Path(args.notes_file).read_text(encoding="utf-8")
             if args.notes_file else find_notes(ver))

    log(f"仓库={slug}  版本={ver}  tag={tag}  资产={asset.name} -> {asset_name}")

    # 1. 建 release(已存在则复用)
    status, resp = api("GET", f"{API}/repos/{slug}/releases/tags/{tag}", token)
    if status == 200:
        log(f"release {tag} 已存在, 复用 (id={resp['id']})")
        rel = resp
    else:
        status, rel = api("POST", f"{API}/repos/{slug}/releases", token, {
            "tag_name": tag,
            "name": f"iCloudMon {tag}",
            "body": notes,
            "draft": args.draft,
            "prerelease": False,
        })
        if status not in (200, 201):
            fail(f"创建 release 失败: HTTP {status} {rel}")
        log(f"已创建 release {tag} (id={rel['id']}, draft={args.draft})")

    # 2. 上传资产
    upload_url = rel["upload_url"].split("{")[0]
    data = asset.read_bytes()
    status, resp = api(
        "POST", f"{upload_url}?name={urllib.parse.quote(asset_name)}", token,
        data=data, content_type="application/octet-stream")
    if status not in (200, 201):
        fail(f"上传资产失败: HTTP {status} {resp}")
    log(f"上传成功: {resp.get('browser_download_url')}")
    log(f"Release 页面: {rel['html_url']}")


if __name__ == "__main__":
    main()
