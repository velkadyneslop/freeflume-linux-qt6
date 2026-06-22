#!/usr/bin/env bash
# One-command FreeFlume Flatpak release.
#
# Ensures a signing key exists (creates one the first time), builds the signed
# OSTree repo, and publishes it to the gh-pages branch. After the one-time Pages
# toggle on GitHub, every future release is just:
#
#     ./release.sh
#
# Options:
#     ./release.sh --build-only    build the repo but don't push to gh-pages
#     ./release.sh -h | --help     show this help
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GPG_HOMEDIR="$HERE/.gnupg-flatpak"                       # isolated, git-ignored
KEY_UID="FreeFlume Repo Signing <freeflume@velkadyneslop>"
PAGES_URL="https://velkadyneslop.github.io/freeflume-linux-qt6"

PUBLISH=1
for a in "$@"; do
    case "$a" in
        --build-only) PUBLISH=0 ;;
        -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $a (try --help)" >&2; exit 1 ;;
    esac
done

# --- prerequisites --------------------------------------------------------
miss=0
for t in flatpak-builder flatpak gpg git; do
    command -v "$t" >/dev/null || { echo "ERROR: '$t' is not installed." >&2; miss=1; }
done
[ "$miss" = 1 ] && exit 1

if ! flatpak list --columns=ref 2>/dev/null | grep -q "org.kde.Sdk/.*/6.10"; then
    echo "ERROR: the KDE 6.10 SDK/Platform isn't installed. Install it (user scope) with:" >&2
    echo "  flatpak install --user flathub org.kde.Sdk//6.10 org.kde.Platform//6.10" >&2
    exit 1
fi

# --- 1. ensure a signing key ---------------------------------------------
mkdir -p "$GPG_HOMEDIR"; chmod 700 "$GPG_HOMEDIR"
keyfpr() { gpg --homedir "$GPG_HOMEDIR" --list-keys --with-colons 2>/dev/null \
               | awk -F: '/^fpr:/{print $10; exit}'; }
KEY_FPR="$(keyfpr)"
if [ -z "$KEY_FPR" ]; then
    echo "No signing key found — generating one (no passphrase, for unattended signing)…"
    gpg --homedir "$GPG_HOMEDIR" --batch --pinentry-mode loopback --passphrase '' \
        --quick-generate-key "$KEY_UID" default sign never
    KEY_FPR="$(keyfpr)"
    cat <<EOF

############################################################################
#  A signing key was created:
#    $KEY_FPR
#
#  BACK UP this folder and keep it private — without it you cannot sign
#  updates that existing installs will trust:
#    $GPG_HOMEDIR
#
#  (Prefer a passphrase? Delete that folder, create your own key there, and
#   re-run — build-repo.sh will prompt you to unlock it at sign time.)
############################################################################

EOF
fi
echo "==> Signing with key: $KEY_FPR"

# --- 2. build the signed repo --------------------------------------------
echo "==> Building the signed OSTree repo…"
GPG_KEY="$KEY_FPR" GPG_HOMEDIR="$GPG_HOMEDIR" "$HERE/build-repo.sh"

# --- 3. publish -----------------------------------------------------------
if [ "$PUBLISH" = 1 ]; then
    echo "==> Publishing to gh-pages…"
    "$HERE/publish-repo.sh"
    cat <<EOF

==> Done. If this is the first publish, enable Pages once:
      GitHub repo -> Settings -> Pages -> Source: Deploy from branch
                  -> Branch: gh-pages / (root)

    Users install (and then auto-update) with:
      flatpak install --user --from $PAGES_URL/com.velkadyne.FreeFlume.flatpakref
EOF
else
    cat <<EOF

==> Built only (not published). Inspect: Linux-Release/flatpak-site/
    Publish when ready:  $HERE/publish-repo.sh
EOF
fi
