#!/usr/bin/env bash
# Build FreeFlume as a Flatpak and export it into a self-hosted, GPG-signed
# OSTree repository — the thing that makes `flatpak update` work. Publish the
# resulting site/ directory to GitHub Pages (see publish-repo.sh). No Flathub.
#
#   GPG_KEY=<key-id-or-email> ./build-repo.sh      # signed (recommended)
#   ./build-repo.sh                                # unsigned (testing only)
#
# Override BASE_URL/BRANCH/GPG_HOMEDIR via env as needed.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"           # repo root (BravePipeDesktop)
APP_ID="com.velkadyne.FreeFlume"
MANIFEST="$HERE/${APP_ID}.yml"

# --- config (override via env) ---
# Where the repo will be served from. Default = GitHub Pages for the project.
BASE_URL="${BASE_URL:-https://velkadyneslop.github.io/freeflume-linux-qt6}"
BRANCH="${BRANCH:-stable}"                   # Flatpak branch (not a git branch)
GPG_KEY="${GPG_KEY:-}"                       # signing key id/email; empty = unsigned
GPG_HOMEDIR="${GPG_HOMEDIR:-}"               # optional isolated gnupg homedir

SITE="$ROOT/Linux-Release/flatpak-site"      # <- push THIS to GitHub Pages
REPO="$SITE/repo"                            # the OSTree repo (served at BASE_URL/repo)
STATEDIR="$ROOT/.flatpak-builder"            # reuse the cached build state (git-ignored)
BUILDDIR="$STATEDIR/build-repo"

command -v flatpak-builder >/dev/null || { echo "ERROR: flatpak-builder not installed." >&2; exit 1; }

# Assemble GPG args once; reused for builder + update-repo so both are signed.
GPG_ARGS=()
if [ -n "$GPG_KEY" ]; then
    GPG_ARGS=(--gpg-sign="$GPG_KEY")
    [ -n "$GPG_HOMEDIR" ] && GPG_ARGS+=(--gpg-homedir="$GPG_HOMEDIR")
else
    echo "WARNING: building an UNSIGNED repo. Set GPG_KEY=<id> to sign it." >&2
    echo "         Unsigned repos give users no authenticity guarantee." >&2
fi

rm -rf "$BUILDDIR"
mkdir -p "$SITE"

# 1. Build the app and export it straight into the signed repo. --state-dir
#    pins the download/build cache to the repo root (reused across runs, and
#    git-ignored) instead of a cwd-relative .flatpak-builder.
flatpak-builder --force-clean --state-dir="$STATEDIR" --repo="$REPO" "${GPG_ARGS[@]}" \
    --default-branch="$BRANCH" \
    "$BUILDDIR" "$MANIFEST"

# 2. Regenerate the repo summary/metadata (also signed), build static deltas so
#    updates download as small binary diffs, and prune old commits to cap size.
flatpak build-update-repo "${GPG_ARGS[@]}" \
    --generate-static-deltas \
    --prune --prune-depth=20 \
    "$REPO"

# 3. GitHub Pages must not run Jekyll over an OSTree repo (it would drop files
#    whose names start with '_' / '.').
touch "$SITE/.nojekyll"
cp "$ROOT/packaging/icons/freeflume.svg" "$SITE/icon.svg"

# 4. Export the public key (clients verify signatures against it) and inline it
#    base64 into the install files so users don't fetch it separately.
GPG_LINE=""
if [ -n "$GPG_KEY" ]; then
    KEYFILE="$SITE/freeflume.gpg"
    if [ -n "$GPG_HOMEDIR" ]; then
        gpg --homedir "$GPG_HOMEDIR" --export "$GPG_KEY" > "$KEYFILE"
    else
        gpg --export "$GPG_KEY" > "$KEYFILE"
    fi
    GPG_LINE="GPGKey=$(base64 -w0 "$KEYFILE")"
fi

# 5a. .flatpakrepo — adds the remote (users can browse/install/update from it).
{
    echo "[Flatpak Repo]"
    echo "Title=FreeFlume"
    echo "Url=$BASE_URL/repo"
    echo "Homepage=https://github.com/velkadyneslop/freeflume-linux-qt6"
    echo "Comment=Self-hosted FreeFlume Flatpak repository"
    echo "Description=Fast native desktop YT client"
    echo "Icon=$BASE_URL/icon.svg"
    echo "DefaultBranch=$BRANCH"
    [ -n "$GPG_LINE" ] && echo "$GPG_LINE"
} > "$SITE/freeflume.flatpakrepo"

# 5b. .flatpakref — one-shot install: adds the remote AND installs the app.
#     RuntimeRepo points at Flathub so the org.kde.Platform runtime resolves
#     even if the user has no remotes yet — this does NOT list FreeFlume on
#     Flathub, it only tells Flatpak where to get the shared KDE runtime.
{
    echo "[Flatpak Ref]"
    echo "Title=FreeFlume"
    echo "Name=$APP_ID"
    echo "Branch=$BRANCH"
    echo "Url=$BASE_URL/repo"
    echo "Homepage=https://github.com/velkadyneslop/freeflume-linux-qt6"
    echo "Comment=Fast native desktop YT client"
    echo "Icon=$BASE_URL/icon.svg"
    echo "IsRuntime=false"
    echo "SuggestRemoteName=freeflume"
    echo "RuntimeRepo=https://dl.flathub.org/repo/flathub.flatpakrepo"
    [ -n "$GPG_LINE" ] && echo "$GPG_LINE"
} > "$SITE/$APP_ID.flatpakref"

# 6. A tiny landing page with copy-paste install instructions.
cat > "$SITE/index.html" <<HTML
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FreeFlume — Flatpak</title>
<style>body{font:16px/1.6 system-ui,sans-serif;max-width:42rem;margin:3rem auto;padding:0 1rem}
code,pre{background:#f4f4f4;border-radius:6px}pre{padding:1rem;overflow:auto}code{padding:.15em .4em}</style>
</head><body>
<h1>FreeFlume</h1>
<p>Fast native desktop YT client. Install into your user account (no root) from
this self-hosted Flatpak repo — updates arrive through <code>flatpak update</code>
/ your software center:</p>
<pre>flatpak install --user --from $BASE_URL/$APP_ID.flatpakref</pre>
<p>Then run <code>flatpak run $APP_ID</code>. Update later with:</p>
<pre>flatpak update --user $APP_ID</pre>
<p><a href="https://github.com/velkadyneslop/freeflume-linux-qt6">Source &amp; releases on GitHub</a></p>
</body></html>
HTML

echo
echo "DONE. Publishable site at: $SITE"
echo "  repo:        $SITE/repo"
echo "  install ref: $SITE/$APP_ID.flatpakref"
echo "  add-remote:  $SITE/freeflume.flatpakrepo"
[ -n "$GPG_KEY" ] && echo "  signed with: $GPG_KEY" || echo "  (UNSIGNED — set GPG_KEY to sign)"
echo "Next: ./publish-repo.sh   (pushes the site to the gh-pages branch)"
