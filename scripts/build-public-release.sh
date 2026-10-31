#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Create a "public" release commit from a branch tree.
#
# Policy: the Gitea repository is the development repository and holds
# everything, including the agent transcripts.  GitHub only ever receives
# release commits, authored as the project owner, containing just what is
# needed to build and release.
#
# Usage: scripts/build-public-release.sh <tree-ish> <parent-release-commit> <message-file>
#
# See docs/RELEASING.md.
set -eu

TREE_OF=$1
PARENT=$2
MESSAGE_FILE=$3

GIT_AUTHOR_NAME="lupin3rd"
GIT_AUTHOR_EMAIL="1455316+lupin3rd@users.noreply.github.com"
GIT_COMMITTER_NAME="lupin3rd"
GIT_COMMITTER_EMAIL="1455316+lupin3rd@users.noreply.github.com"
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL

# A throw-away index, so the working tree and the real index are untouched.
GIT_INDEX_FILE=$(mktemp /tmp/public-index.XXXXXX)
export GIT_INDEX_FILE
rm -f "$GIT_INDEX_FILE"

git read-tree "$TREE_OF^{tree}"

# Directories that belong to the local repository only.
git rm -r --cached --ignore-unmatch agent >/dev/null

NEW_TREE=$(git write-tree)
rm -f "$GIT_INDEX_FILE"
unset GIT_INDEX_FILE

git commit-tree "$NEW_TREE" -p "$PARENT" -F "$MESSAGE_FILE"
