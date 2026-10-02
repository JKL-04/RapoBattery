#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""工具脚本共用的路径常量。

单独成文件的原因：这个工作区路径在本项目里被反复手写，任何一处拼错都会让
脚本中途崩溃并留下不可信的半截输出。集中定义 + 启动时断言，可让错误在第一
步就暴露，而不是跑到一半才失败。

注：项目命名已于 2026-10-03 全部统一为 `RapoBattery`（一个 o）。
此前产品名含两个 o，与目录名不一致，极易看错。

用法：
    from paths import ROOT, REPO, GIT, REPO_URL, DEPLOY, EXE
"""
import os
import sys

# ── 目录 ──
ROOT = r"D:\Logic\Documents\Per\Rapo"
REPO = os.path.join(ROOT, "RapoBattery")                    # 仓库（含 .git）
DEPLOY = r"D:\Program Files\RapoBattery"                    # 部署目录

# ── 产物 ──
EXE_NAME = "RapoBattery.exe"
EXE = os.path.join(REPO, EXE_NAME)                          # 仓库内的构建产物
EXE_DEPLOYED = os.path.join(DEPLOY, EXE_NAME)               # 部署后的可执行文件

# ── GitHub Desktop 自带的 git（本机没有独立安装 Git）──
GIT = os.path.join(
    os.environ.get("LOCALAPPDATA", r"C:\Users\Logic\AppData\Local"),
    "GitHubDesktop", "app-3.6.6", "resources", "app", "git", "cmd", "git.exe")

# ── 仓库信息 ──
GH_USER = "JKL-04"
GH_REPO = "RapoBattery"
REPO_URL = "https://github.com/%s/%s.git" % (GH_USER, GH_REPO)


def check(need_deploy=False):
    """启动时调用：路径不存在就立刻报错，而不是跑到一半才崩。

    need_deploy=True 时额外要求部署目录存在（只有涉及部署的脚本才需要）。
    """
    problems = []
    if not os.path.isdir(ROOT):
        problems.append("ROOT 不存在: %s" % ROOT)
    if not os.path.isdir(REPO):
        problems.append("REPO 不存在: %s" % REPO)
    if not os.path.exists(GIT):
        problems.append("git.exe 不存在: %s" % GIT)
    if need_deploy and not os.path.isdir(DEPLOY):
        problems.append("DEPLOY 不存在: %s" % DEPLOY)
    if problems:
        for p in problems:
            print("路径检查失败: " + p, file=sys.stderr)
        return False
    return True


if __name__ == "__main__":
    print("ROOT          =", ROOT, "->", os.path.isdir(ROOT))
    print("REPO          =", REPO, "->", os.path.isdir(REPO))
    print("EXE           =", EXE, "->", os.path.exists(EXE))
    print("DEPLOY        =", DEPLOY, "->", os.path.isdir(DEPLOY))
    print("EXE_DEPLOYED  =", EXE_DEPLOYED, "->", os.path.exists(EXE_DEPLOYED))
    print("GIT           =", GIT, "->", os.path.exists(GIT))
    print("REPO_URL      =", REPO_URL)
    print()
    print("检查通过" if check() else "检查失败")
