#!/usr/bin/env bash
# Build the AmbiPi .deb. Intended to run INSIDE the Dockerfile.cross container
# (target arch == container arch), with the repo mounted at /src.
set -euo pipefail

ARCH="${DEB_ARCH:-armhf}"
VERSION="${DEB_VERSION:-1.0.0}"
BUILD_DIR="build-deb"
DEB_ROOT="${BUILD_DIR}/root"

cd /src

echo "=== [1/5] Submodules ==="
# Fetch submodules over HTTPS so no SSH key is needed inside the container
# (the .gitmodules uses an git@github.com SSH URL for rpi_ws281x).
git config --global --add safe.directory /src || true
git config --global url."https://github.com/".insteadOf "git@github.com:" || true
git submodule update --init --recursive

echo "=== [2/5] Build (native, arch=${ARCH}) ==="
# distclean (not clean) so the rpi_ws281x / pistache static libs are rebuilt
# for THIS arch — a stale arch's archives would fail the final link.
make distclean || true
make -j"$(nproc)"

echo "=== [3/5] Stage files ==="
rm -rf "${BUILD_DIR}"
mkdir -p "${DEB_ROOT}/usr/bin" \
         "${DEB_ROOT}/usr/share/ambipi/html" \
         "${DEB_ROOT}/etc/ambipi" \
         "${DEB_ROOT}/lib/systemd/system" \
         "${DEB_ROOT}/var/lib/ambipi" \
         "${DEB_ROOT}/DEBIAN"

install -m 0755 ambipi                       "${DEB_ROOT}/usr/bin/ambipi"
cp -r html/.                                  "${DEB_ROOT}/usr/share/ambipi/html/"
install -m 0644 gamewall/public/shelves.json "${DEB_ROOT}/usr/share/ambipi/shelves.json"
install -m 0644 config.json                  "${DEB_ROOT}/etc/ambipi/config.json"
install -m 0644 packaging/ambipi.service     "${DEB_ROOT}/lib/systemd/system/ambipi.service"

install -m 0755 packaging/preinst  "${DEB_ROOT}/DEBIAN/preinst"
install -m 0755 packaging/postinst "${DEB_ROOT}/DEBIAN/postinst"
install -m 0755 packaging/prerm    "${DEB_ROOT}/DEBIAN/prerm"
install -m 0644 packaging/conffiles "${DEB_ROOT}/DEBIAN/conffiles"

echo "=== [4/5] Resolve dependencies (dpkg-shlibdeps) ==="
DEPS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
  mkdir -p debian
  printf 'Source: ambipi\nMaintainer: akw <akw@local>\n\nPackage: ambipi\nArchitecture: %s\nDescription: AmbiPi\n' "${ARCH}" > debian/control
  DEPS="$(dpkg-shlibdeps -O "${DEB_ROOT}/usr/bin/ambipi" 2>/dev/null | sed -n 's/^shlibs:Depends=//p' || true)"
  rm -rf debian
fi
[ -n "${DEPS}" ] || DEPS="libc6, libstdc++6"
# uhubctl (USB capture recovery) and adb (JMGO beamer power-off via /api/beamer/off)
# are invoked at runtime, not linked libs, so shlibdeps can't see them — add explicitly.
DEPS="${DEPS}, uhubctl, adb"
echo "Depends: ${DEPS}"

cat > "${DEB_ROOT}/DEBIAN/control" <<EOF
Package: ambipi
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: akw <akw@local>
Depends: ${DEPS}
Description: AmbiPi Ambilight controller daemon
 Captures video, derives edge colors and drives local WS2812 LED strips plus
 networked WLED (DDP/DNRGB), WiZ and KDP devices on the LAN. Serves a web UI
 and REST control API on port 80.
EOF

echo "=== [5/5] Build .deb ==="
mkdir -p dist
DEB="dist/ambipi_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "${DEB_ROOT}" "${DEB}"
echo "--- Built ${DEB} ---"
dpkg-deb --info "${DEB}"
dpkg-deb --contents "${DEB}"
