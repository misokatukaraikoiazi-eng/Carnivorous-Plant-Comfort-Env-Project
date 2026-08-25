#!/usr/bin/env bash
# Stage all changes, commit with a message, and push to the current branch.
set -e

cd "$(dirname "$0")"

MESSAGE="${1:-update}"

git add -A
if git diff --cached --quiet; then
    echo "コミットする変更がありません。"
    exit 0
fi

git commit -m "$MESSAGE"
git push origin "$(git rev-parse --abbrev-ref HEAD)"
