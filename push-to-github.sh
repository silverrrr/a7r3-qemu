#!/bin/bash
# 一键推送到 GitHub（需要先能访问 github.com，并已完成 gh 登录）
#
#   bash push-to-github.sh [owner] [repo]
#
# 默认 owner 是 gh 当前登录账号，repo 是 a7r3-qemu。
# 如果仓库已存在，只设置 remote 并 push。
set -eu
OWNER="${1:-}"
REPO="${2:-a7r3-qemu}"
DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$DIR"

if ! command -v gh >/dev/null 2>&1; then
    echo "需要 gh CLI：https://cli.github.com/"
    exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
    echo "还没登录：请先执行  gh auth login"
    exit 1
fi
if [ -z "$OWNER" ]; then
    OWNER="$(gh api user --jq .login)"
fi

echo "== 目标仓库: $OWNER/$REPO"
if gh repo view "$OWNER/$REPO" >/dev/null 2>&1; then
    echo "仓库已存在，直接推送"
    git remote remove origin 2>/dev/null || true
    git remote add origin "https://github.com/$OWNER/$REPO.git"
    git push -u origin main
else
    gh repo create "$OWNER/$REPO" --public --source=. --remote=origin --push
fi

echo "== 完成：https://github.com/$OWNER/$REPO"
