#!/bin/bash
# Build aria2c.exe (Windows 64-bit) with the console progress bar.
#
#   ./build-windows.sh            cross-compile in Docker, the way aria2's own
#                                 Windows release is built (tested)
#   ./build-windows.sh --msys2    native build inside an MSYS2 "MINGW64" shell
#                                 (written from aria2's docs, NOT tested)
#   ./build-windows.sh --install  also copy aria2c.exe into a folder that is on
#                                 your PATH (Git Bash only), so "aria2c" runs it
#
# Run it from Git Bash or from WSL. The Docker route needs Docker Desktop or
# Docker inside WSL. First run builds the dependency image (about 20 min,
# 2 GB download); later runs take about 3 minutes.
#
# Run it inside a checkout of the fork, or anywhere: it clones the fork's
# branch into ./aria2 when no source is found next to it.
set -euo pipefail

FORK="https://github.com/akuresonite/aria2.git"
BRANCH="feature/console-progress-bar"
MODE=docker; INSTALL=0
for a in "$@"; do
  case "$a" in
    --msys2)   MODE=--msys2 ;;
    --install) INSTALL=1 ;;
    *) echo "unknown option: $a"; exit 1 ;;
  esac
done

# ---------- find or fetch the source ----------
find_src() {
  for d in "$PWD" "$(dirname "$0")" "$(dirname "$0")/.." "$(dirname "$0")/../.." "$(dirname "$0")/../aria2-src"; do
    if [ -f "$d/configure.ac" ] && grep -q "aria2" "$d/configure.ac"; then
      cd "$d" && pwd && return
    fi
  done
  [ -d aria2 ] || git clone -b "$BRANCH" --depth 1 "$FORK" aria2
  cd aria2 && pwd
}
SRC=$(find_src)
# find_src changes directory inside a subshell only; move the script there too.
cd "$SRC"
echo "source: $SRC"

if [ "$MODE" = "--msys2" ]; then
  # ---------- native MSYS2 / MINGW64 ----------
  [ "${MSYSTEM:-}" = "MINGW64" ] || { echo "open the 'MSYS2 MINGW64' shell and run again"; exit 1; }
  pacman -S --needed --noconfirm autoconf automake libtool make pkgconf gettext-devel \
    mingw-w64-x86_64-toolchain mingw-w64-x86_64-libssh2 mingw-w64-x86_64-c-ares \
    mingw-w64-x86_64-sqlite3 mingw-w64-x86_64-expat mingw-w64-x86_64-zlib mingw-w64-x86_64-gmp
  cd "$SRC"
  autoreconf -i
  ./configure --disable-nls --without-gnutls --without-openssl --with-libcares \
    --with-sqlite3 --with-libz --with-libexpat --without-libxml2 --with-libssh2 \
    --with-libgmp ARIA2_STATIC=yes
  make -j"$(nproc)"
  strip src/aria2c.exe
  cp src/aria2c.exe "$SRC/aria2c.exe"
  OUT="$SRC/aria2c.exe"
else
  # ---------- Docker cross-compile (mingw-w64), same recipe as upstream ----------
  command -v docker >/dev/null || { echo "Docker is needed for this route (or use --msys2)"; exit 1; }
  # Docker wants a path it can see: /mnt/c/... under WSL, C:/... for Docker Desktop.
  host_path() {
    if command -v wslpath >/dev/null 2>&1; then wslpath -a "$1";
    elif command -v cygpath >/dev/null 2>&1; then cygpath -m "$1";
    else echo "$1"; fi
  }
  TMP=$(mktemp -d)
  # The official recipe (Dockerfile.mingw in the aria2 repo), switched to 64-bit
  # and stopped before the clone step so it builds OUR source instead.
  sed -e 's/^ARG HOST=i686-w64-mingw32/ARG HOST=x86_64-w64-mingw32/' \
      -e '/^ARG ARIA2_VERSION=master/,$d' "$SRC/Dockerfile.mingw" > "$TMP/Dockerfile"
  cat > "$TMP/build.sh" <<'EOF'
set -e
export HOST=x86_64-w64-mingw32
rm -rf /aria2 && mkdir -p /aria2
tar -C /src --exclude=.git -cf - . | tar -C /aria2 -xf -
cd /aria2 && autoreconf -i && ./mingw-config && make -j"$(nproc)"
$HOST-strip src/aria2c.exe && cp src/aria2c.exe /out/aria2c.exe
EOF
  docker build -t aria2-pb-mingw -f "$TMP/Dockerfile" "$TMP"
  docker run --rm -v "$(host_path "$SRC"):/src:ro" -v "$(host_path "$TMP"):/bd:ro" \
    -v "$(host_path "$PWD"):/out" aria2-pb-mingw bash /bd/build.sh
  rm -rf "$TMP"
  OUT="$PWD/aria2c.exe"
fi

echo
echo "built: $OUT"
ls -l "$OUT"
echo
# %LOCALAPPDATA%\Microsoft\WindowsApps is on every user's PATH and needs no admin.
if [ "$INSTALL" = 1 ] && [ -n "${LOCALAPPDATA:-}" ]; then
  DEST="$(cygpath "$LOCALAPPDATA")/Microsoft/WindowsApps/aria2c.exe"
  cp "$OUT" "$DEST" && echo "installed: $DEST" && echo "use it:    aria2c <URL>   (open a new terminal first)"
else
  echo "use it now:  $OUT <URL>"
  echo "install it:  copy aria2c.exe into a folder on your PATH, for example"
  echo "             %LOCALAPPDATA%\Microsoft\WindowsApps   (or rerun with --install from Git Bash)"
fi
echo "settings:    %USERPROFILE%\.aria2\aria2.conf, e.g. progress-bar-style=dots"
