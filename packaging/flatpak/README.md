# FreeFlume Flatpak — self-hosted repo

Two ways to ship the Flatpak:

| Artifact | Updates? | Built by |
|---|---|---|
| **Single-file `.flatpak` bundle** (on GitHub Releases) | ❌ manual re-download | the existing bundle build |
| **Self-hosted OSTree repo** (GitHub Pages) | ✅ `flatpak update` / software center | `build-repo.sh` + `publish-repo.sh` |

The repo is what gives users **automatic background updates without Flathub** — no
store listing, no review, you keep full control. The app is served from your
GitHub Pages; only the shared `org.kde.Platform` runtime comes from Flathub.

## Prerequisites

```bash
flatpak install --user flathub org.kde.Sdk//6.10 org.kde.Platform//6.10
# plus: flatpak-builder, gpg
```

Everything here is **user-space** (`--user`, no root): the build runtimes above,
and the install commands below. flatpak-builder picks up the user-installed SDK
automatically.

## One-time: a signing key

A self-hosted repo should be GPG-signed so users get an authenticity guarantee.
Use a **dedicated** key in an isolated keyring (keep it separate from your
commit/release keys):

```bash
export GPG_HOMEDIR="$PWD/.gnupg-flatpak"        # isolated keyring (git-ignored)
mkdir -p "$GPG_HOMEDIR" && chmod 700 "$GPG_HOMEDIR"
gpg --homedir "$GPG_HOMEDIR" --quick-generate-key \
    "FreeFlume Repo Signing <freeflume@velkadyneslop>" default sign never
gpg --homedir "$GPG_HOMEDIR" --list-keys          # note the key id / email
```

Back up that keyring. If you lose the key you can't sign updates that existing
installs will trust without re-adding the remote.

## Build + publish

```bash
cd packaging/flatpak

# 1. Build the app into a signed OSTree repo under Linux-Release/flatpak-site/
GPG_KEY="freeflume@velkadyneslop" GPG_HOMEDIR="$PWD/.gnupg-flatpak" ./build-repo.sh

# 2. Push that site to the gh-pages branch (force-push, single commit)
./publish-repo.sh
```

Then in GitHub: **Settings → Pages → Branch: `gh-pages` / (root)**. The repo goes
live at `https://velkadyneslop.github.io/freeflume-linux-qt6/`.

Each release: bump the version in `CMakeLists.txt`, re-run both scripts. Returning
users get the delta automatically.

## How users install

```bash
flatpak install --user --from https://velkadyneslop.github.io/freeflume-linux-qt6/com.velkadyne.FreeFlume.flatpakref
flatpak run com.velkadyne.FreeFlume
```

Installs into the user account — no root, nothing system-wide. After that,
`flatpak update --user` (or GNOME Software / KDE Discover in the background)
keeps it current from your repo.

## Notes

- `build-repo.sh` runs **unsigned** if `GPG_KEY` is unset — fine for a local test,
  not for publishing.
- `--generate-static-deltas` makes updates download as small binary diffs.
- The bundled `yt-dlp`/`Deno` versions are pinned in the manifest; bump them there
  when YouTube extraction needs it, then re-publish.
- `com.velkadyne.FreeFlume.metainfo.xml` still points its homepage/bugtracker URLs
  at **Codeberg** (the superseded host) and lists only release `1.0.0` — worth
  updating to GitHub, independent of this repo work.
