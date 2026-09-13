#!/bin/bash
# Build aria2 with the console progress bar on macOS (Apple Silicon or Intel).
#
#   ./build-macos.sh
#   ./build-macos.sh --install   also copy the result to /usr/local/bin/aria2c
#
# Needs Homebrew (https://brew.sh). Uses the same packages and configure flags
# as aria2's own macOS CI job, with Apple's TLS instead of OpenSSL.
#
# Run it inside a checkout of the fork, or anywhere: it clones the fork's
# branch into ./aria2 when no source is found next to it.
#
# NOT TESTED by the author: written on Windows from aria2's CI recipe. If a
# step fails, the error will name the missing piece.
set -euo pipefail

FORK="https://github.com/akuresonite/aria2.git"
BRANCH="feature/console-progress-bar"

INSTALL=0; [ "${1:-}" = "--install" ] && INSTALL=1
command -v brew >/dev/null || { echo "Install Homebrew first: https://brew.sh"; exit 1; }
xcode-select -p >/dev/null 2>&1 || xcode-select --install

brew install autoconf automake libtool pkg-config gettext libssh2 c-ares sqlite3

# gettext and sqlite are "keg-only" in Homebrew: not on PATH by default.
export PATH="$(brew --prefix gettext)/bin:$PATH"
export PKG_CONFIG_PATH="$(brew --prefix sqlite3)/lib/pkgconfig:$(brew --prefix libssh2)/lib/pkgconfig:$(brew --prefix c-ares)/lib/pkgconfig:$(brew --prefix)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

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

autoreconf -i
./configure --without-openssl --without-gnutls --with-appletls --disable-nls \
  --with-libssh2 --with-libcares --with-sqlite3 --with-libz
make -j"$(sysctl -n hw.ncpu)"
strip src/aria2c
OUT="$SRC/aria2c-macos-$(uname -m)"
cp src/aria2c "$OUT"

echo
echo "built: $OUT"
"$OUT" --version | head -1
echo
if [ "$INSTALL" = 1 ]; then
  sudo install -m 755 "$OUT" /usr/local/bin/aria2c
  echo "installed: $(command -v aria2c)   ($(aria2c --version | head -1))"
  echo "use it:    aria2c <URL>"
else
  echo "use it now:  $OUT <URL>"
  echo "install it:  sudo install -m 755 $OUT /usr/local/bin/aria2c"
  echo "             then plain: aria2c <URL>   (or rerun with --install)"
fi
echo "settings:    ~/.aria2/aria2.conf, e.g. progress-bar-style=dots"
