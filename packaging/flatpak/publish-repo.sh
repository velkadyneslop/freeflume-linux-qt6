#!/usr/bin/env bash
# Publish the built Flatpak site/ to the gh-pages branch of the GitHub repo.
# Each publish is a single orphan commit (force-pushed) so the OSTree blobs
# don't pile up in git history. Run build-repo.sh first.
#
#   ./publish-repo.sh                 # push to origin gh-pages
#   REMOTE=origin BRANCH_PAGES=gh-pages ./publish-repo.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SITE="$ROOT/Linux-Release/flatpak-site"
REMOTE="${REMOTE:-origin}"
BRANCH_PAGES="${BRANCH_PAGES:-gh-pages}"
WT="$ROOT/.flatpak-pages-worktree"

[ -d "$SITE/repo" ] || { echo "ERROR: $SITE/repo missing — run build-repo.sh first." >&2; exit 1; }

# Commit identity (matches the project's release identity; no extra trailers).
export GIT_AUTHOR_NAME="velkadyne"
export GIT_AUTHOR_EMAIL="292470688+velkadyneslop@users.noreply.github.com"
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME"
export GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"

cleanup() { git -C "$ROOT" worktree remove --force "$WT" 2>/dev/null || true; rm -rf "$WT"; }
trap cleanup EXIT

git -C "$ROOT" worktree remove --force "$WT" 2>/dev/null || true
rm -rf "$WT"

# Fresh orphan branch each publish (single-commit gh-pages, no blob bloat).
git -C "$ROOT" worktree add --detach "$WT" >/dev/null
git -C "$WT" checkout --orphan "$BRANCH_PAGES" >/dev/null 2>&1
git -C "$WT" rm -rf . >/dev/null 2>&1 || true
find "$WT" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +

cp -a "$SITE/." "$WT/"
git -C "$WT" add -A
git -C "$WT" commit -q -m "Publish Flatpak repo ($(cat "$ROOT/CMakeLists.txt" | sed -n 's/.*VERSION \([0-9.]*\).*/\1/p' | head -1))"
echo "Force-pushing $BRANCH_PAGES to $REMOTE ..."
git -C "$WT" push --force "$REMOTE" "$BRANCH_PAGES"

echo
echo "Published. In GitHub: Settings -> Pages -> Branch: $BRANCH_PAGES / (root)."
echo "Users install (user-space, no root) with:"
echo "  flatpak install --user --from https://velkadyneslop.github.io/freeflume-linux-qt6/com.velkadyne.FreeFlume.flatpakref"
