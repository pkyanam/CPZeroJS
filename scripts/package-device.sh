#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
app_id="${1:-cpzero-demo}"
app_version="${2:-0.1.0}"
host="${CPZERO_HOST:-$root/native/build/cpzero-host}"
bundle="${CPZERO_BUNDLE:-$root/dist/app.js}"
if [[ ! "$app_id" =~ ^[a-z][a-z0-9-]+$ ]] || [[ ! "$app_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo 'Usage: package-device.sh <lowercase-app-id> <major.minor.patch>' >&2; exit 2
fi
if [[ "$(uname -s)" != Linux ]]; then
  echo 'Package on Debian Linux with an ARM64 framebuffer host binary; Mac binaries cannot run on the Zero.' >&2; exit 1
fi
for tool in dpkg-deb readelf; do command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }; done
readelf -h "$host" | grep -q 'Machine:.*AArch64' || { echo 'CPZERO_HOST must be an ARM64 Linux ELF binary.' >&2; exit 1; }
[[ -f "$bundle" ]] || { echo "Missing bundle: $bundle. Run npm run build first." >&2; exit 1; }
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/DEBIAN" "$stage/usr/lib/$app_id" "$stage/usr/bin" "$stage/usr/share/APPLaunch/applications"
mkdir -p "$stage/usr/share/doc/$app_id"
install -m644 "$root/LICENSE" "$stage/usr/share/doc/$app_id/copyright"
cp "$root"/third_party/*LICENSE* "$stage/usr/share/doc/$app_id/"
install -m755 "$host" "$stage/usr/lib/$app_id/cpzero-host"
install -m644 "$bundle" "$stage/usr/lib/$app_id/app.js"
cat > "$stage/usr/bin/$app_id" <<EOF
#!/bin/sh
export CPZERO_DATA_DIR="\${XDG_DATA_HOME:-\$HOME/.local/share}/$app_id"
exec /usr/lib/$app_id/cpzero-host /usr/lib/$app_id/app.js "\$@"
EOF
chmod 755 "$stage/usr/bin/$app_id"
cat > "$stage/DEBIAN/control" <<EOF
Package: $app_id
Version: $app_version
Architecture: arm64
Maintainer: CPZeroJS developers
Depends: libc6, libstdc++6, libgcc-s1
Description: CPZeroJS native application for Cardputer Zero
EOF
cat > "$stage/usr/share/APPLaunch/applications/$app_id.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$app_id
Exec=/usr/bin/$app_id
Terminal=false
Categories=Utility;
EOF
mkdir -p "$root/dist"
dpkg-deb --root-owner-group --build "$stage" "$root/dist/${app_id}_${app_version}_arm64.deb"
echo 'Package created. Target ABI, framebuffer, keyboard permissions and launcher integration require device validation.'
