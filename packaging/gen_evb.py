#!/usr/bin/env python3
# ======================================================================
#  gen_evb.py  -  把 windeployqt 部署目录生成 Enigma Virtual Box 工程(.evb)
#
#  作用: 把 build/Release 整个目录(含 Qt DLL / 插件 / QML 模块)塞进一个
#  单文件 exe, 双击即用、无需安装 —— 即"绿色单文件版"。
#
#  .evb 是 XML 格式(UTF-16), 结构依据开源工具 generate-evb 的模板:
#    根 <Files> 里一个 %DEFAULT FOLDER% 目录节点(Type=3), 它的子节点镜像
#    整个待打包目录树; 文件 Type=2(带 <File> 源路径), 目录 Type=3(带嵌套 <Files>)。
#
#  真正打包用:  "<EVB目录>/enigmavbconsole.exe" project.evb
#
#  用法:  python gen_evb.py <部署目录> <输入exe> <输出exe> [更多排除名...]
# ======================================================================
import os
import sys


def esc(s: str) -> str:
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def file_node(abs_path: str, name: str) -> str:
    return (
        "    <File>\n"
        "      <Type>2</Type>\n"
        f"      <Name>{esc(name)}</Name>\n"
        f"      <File>{esc(abs_path)}</File>\n"
        "      <ActiveX>false</ActiveX>\n"
        "      <ActiveXInstall>false</ActiveXInstall>\n"
        "      <Action>0</Action>\n"
        "      <OverwriteDateTime>false</OverwriteDateTime>\n"
        "      <OverwriteAttributes>false</OverwriteAttributes>\n"
        "      <PassCommandLine>false</PassCommandLine>\n"
        "    </File>\n"
    )


def dir_node(abs_path: str, name: str, excludes) -> str:
    inner = []
    try:
        entries = sorted(os.listdir(abs_path))
    except OSError as e:
        print(f"  [skip] 读取失败 {abs_path}: {e}")
        entries = []
    for entry in entries:
        if entry in excludes:
            continue
        child = os.path.join(abs_path, entry)
        if os.path.isdir(child):
            inner.append(dir_node(child, entry, excludes))
        else:
            inner.append(file_node(child, entry))
    files_xml = "".join(inner)
    return (
        "    <File>\n"
        "      <Type>3</Type>\n"
        f"      <Name>{esc(name)}</Name>\n"
        "      <Action>0</Action>\n"
        "      <OverwriteDateTime>false</OverwriteDateTime>\n"
        "      <OverwriteAttributes>false</OverwriteAttributes>\n"
        f"      <Files>\n{files_xml}      </Files>\n"
        "    </File>\n"
    )


def build_evb(deploy_dir: str, input_exe: str, output_exe: str, excludes) -> str:
    root_children = dir_node(deploy_dir, "%DEFAULT FOLDER%", excludes)\
        .split("\n", 1)[1]  # 去掉 dir_node 自己那层 <File> 包装, 复用其子项
    # 上面的 split 只是为了拿子项; 这里直接重建 %DEFAULT FOLDER% 节点
    inner = []
    for entry in sorted(os.listdir(deploy_dir)):
        if entry in excludes:
            continue
        child = os.path.join(deploy_dir, entry)
        if os.path.isdir(child):
            inner.append(dir_node(child, entry, excludes))
        else:
            inner.append(file_node(child, entry))
    files_body = "".join(inner)

    return (
        '<?xml encoding="utf-16"?>\n'
        "<>\n"
        f"  <InputFile>{esc(input_exe)}</InputFile>\n"
        f"  <OutputFile>{esc(output_exe)}</OutputFile>\n"
        "  <Files>\n"
        "    <Enabled>true</Enabled>\n"
        "    <DeleteExtractedOnExit>true</DeleteExtractedOnExit>\n"
        "    <CompressFiles>true</CompressFiles>\n"
        "    <Files>\n"
        "      <File>\n"
        "        <Type>3</Type>\n"
        "        <Name>%DEFAULT FOLDER%</Name>\n"
        "        <Action>0</Action>\n"
        "        <OverwriteDateTime>false</OverwriteDateTime>\n"
        "        <OverwriteAttributes>false</OverwriteAttributes>\n"
        f"        <Files>\n{files_body}        </Files>\n"
        "      </File>\n"
        "    </Files>\n"
        "  </Files>\n"
        "  <Registries>\n"
        "    <Enabled>false</Enabled>\n"
        "    <Registries></Registries>\n"
        "  </Registries>\n"
        "  <Packaging>\n"
        "    <Enabled>false</Enabled>\n"
        "  </Packaging>\n"
        "  <Options>\n"
        "    <ShareVirtualSystem>false</ShareVirtualSystem>\n"
        "    <MapExecutableWithTemporaryFile>true</MapExecutableWithTemporaryFile>\n"
        "    <AllowRunningOfVirtualExeFiles>true</AllowRunningOfVirtualExeFiles>\n"
        "  </Options>\n"
        "</>\n"
    )


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    deploy_dir = os.path.abspath(sys.argv[1])
    input_exe = os.path.abspath(sys.argv[2])
    output_exe = os.path.abspath(sys.argv[3])
    extra_excludes = set(sys.argv[4:])

    # 固定排除: 输入 exe 本身(它是基 exe)、日志目录、pdb 调试符号
    excludes = {"log", "iCloudMon.exe", "iCloudMon.pdb", ".wtest"}
    excludes |= extra_excludes

    if not os.path.isdir(deploy_dir):
        print(f"部署目录不存在: {deploy_dir}")
        sys.exit(1)
    if not os.path.isfile(input_exe):
        print(f"输入 exe 不存在: {input_exe}")
        sys.exit(1)

    xml = build_evb(deploy_dir, input_exe, output_exe, excludes)

    evb_path = os.path.splitext(output_exe)[0] + ".evb"
    os.makedirs(os.path.dirname(output_exe), exist_ok=True)
    with open(evb_path, "w", encoding="utf-16") as f:
        f.write(xml)

    # 统计一下会打包多少文件
    n_files = sum(1 for line in xml.splitlines() if "<Type>2</Type>" in line)
    print(f"已生成 EVB 工程: {evb_path}")
    print(f"  待打包文件数: {n_files}")
    print(f"  排除项: {sorted(excludes)}")


if __name__ == "__main__":
    main()