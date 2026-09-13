#!/bin/bash
# Build aria2 with the console progress bar on Linux (x86_64 or arm64).
#
#   ./build-linux.sh            native build, links to your distro's libraries
#   ./build-linux.sh --static   portable static binary, needs Docker (runs on
#                               any Linux of the same CPU type)
#   ./build-linux.sh --install  also copy the result to /usr/local/bin/aria2c,
#                               so plain "aria2c" runs it (may be combined)
#
# Run it inside a checkout of the fork, or anywhere: it clones the fork's
# branch into ./aria2 when no source is found next to it.
set -euo pipefail

FORK="https://github.com/akuresonite/aria2.git"
BRANCH="feature/console-progress-bar"
STATIC=0; INSTALL=0
for a in "$@"; do
  case "$a" in
    --static)  STATIC=1 ;;
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

# ---------- static build inside Alpine (portable) ----------
if [ "$STATIC" = 1 ]; then
  command -v docker >/dev/null || { echo "--static needs Docker"; exit 1; }
  ARCH=$(uname -m)
  case "$ARCH" in
    x86_64)  IMG=amd64/alpine:3.20 ;;
    aarch64|arm64) IMG=arm64v8/alpine:3.20 ;;
    *) echo "no static recipe for $ARCH"; exit 1 ;;
  esac
  TMP=$(mktemp -d)
  cat > "$TMP/inner.sh" <<'EOF'
set -e
apk add --no-cache build-base autoconf automake libtool pkgconf linux-headers perl curl \
  c-ares-dev c-ares-static zlib-dev zlib-static libssh2-dev libssh2-static \
  sqlite-dev sqlite-static expat-dev expat-static gmp-dev gettext-dev >/dev/null
# OpenSSL from source with no-module: aria2 loads the "legacy" provider for
# RC4, and a static binary cannot load it from a .so, so it must be built in.
V=3.3.7
cd /tmp && curl -sSL -o o.tgz "https://github.com/openssl/openssl/releases/download/openssl-$V/openssl-$V.tar.gz"
tar xzf o.tgz && cd "openssl-$V"
./Configure no-shared no-module no-tests --prefix=/usr/local/ossl --openssldir=/etc/ssl --libdir=lib >/dev/null
make -j"$(nproc)" >/dev/null && make install_sw >/dev/null
rm -rf /b && mkdir /b && tar -C /src --exclude=.git -cf - . | tar -C /b -xf - && cd /b
autoreconf -i >/dev/null 2>&1
PKG_CONFIG_PATH=/usr/local/ossl/lib/pkgconfig \
./configure ARIA2_STATIC=yes --disable-nls --without-gnutls --with-openssl \
  --with-libcares --with-sqlite3 --with-libz --with-libexpat --without-libxml2 \
  --with-libssh2 --with-libgmp >/dev/null
make -j"$(nproc)" >/dev/null
strip src/aria2c && cp src/aria2c "/out/aria2c-linux-$(uname -m)"
EOF
  docker run --rm -v "$SRC:/src:ro" -v "$TMP:/bd:ro" -v "$PWD:/out" "$IMG" sh /bd/inner.sh
  rm -rf "$TMP"
  OUT="$PWD/aria2c-linux-$ARCH"
else
# ---------- native build with the distro's packages ----------
  if command -v apt-get >/dev/null; then
    sudo apt-get update -qq
    sudo apt-get install -y build-essential autoconf automake autotools-dev autopoint \
      libtool pkg-config libssl-dev libc-ares-dev zlib1g-dev libsqlite3-dev \
      libssh2-1-dev libexpat1-dev libgmp-dev
  elif command -v dnf >/dev/null; then
    sudo dnf install -y gcc-c++ make autoconf automake libtool pkgconf gettext-devel \
      openssl-devel c-ares-devel zlib-devel sqlite-devel libssh2-devel expat-devel gmp-devel
  elif command -v pacman >/dev/null; then
    sudo pacman -S --needed --noconfirm base-devel autoconf automake libtool pkgconf \
      openssl c-ares zlib sqlite libssh2 expat gmp
  elif command -v apk >/dev/null; then
    sudo apk add build-base autoconf automake libtool pkgconf linux-headers gettext-dev \
      openssl-dev c-ares-dev zlib-dev libssh2-dev sqlite-dev expat-dev gmp-dev
  else
    echo "unknown package manager: install a C++ compiler, autotools, pkg-config and the"
    echo "dev packages for openssl, c-ares, zlib, sqlite3, libssh2, expat, gmp, then rerun"
    exit 1
  fi
  autoreconf -i
  ./configure --disable-nls --without-gnutls --with-openssl --with-libcares \
    --with-sqlite3 --with-libz --with-libexpat --without-libxml2 --with-libssh2 --with-libgmp
  make -j"$(nproc)"
  strip src/aria2c
  OUT="$SRC/aria2c-linux-$(uname -m)"
  cp src/aria2c "$OUT"
fi

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
