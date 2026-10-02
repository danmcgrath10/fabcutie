#!/bin/bash
# Builds FabCutie-<version>-macOS.pkg: one installer with a choice of AU,
# VST3, CLAP and the standalone app. Run after a Release build (and after
# signing the bundles).
#
#   packaging/macos/build-pkg.sh <version> <artefacts dir> <output dir>
set -euo pipefail

version="$1"
artefacts="$2"
out="$3"

here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$out" "$work/packages" "$work/resources"
cp "$here/resources/"* "$work/resources/"
cp "$here/../../LICENSE" "$work/resources/LICENSE.txt"

# id, bundle, install location, the user-folder copy older builds left behind
components=(
    "au|AU/FabCutie.component|/Library/Audio/Plug-Ins/Components|Library/Audio/Plug-Ins/Components/FabCutie.component"
    "vst3|VST3/FabCutie.vst3|/Library/Audio/Plug-Ins/VST3|Library/Audio/Plug-Ins/VST3/FabCutie.vst3"
    "clap|CLAP/FabCutie.clap|/Library/Audio/Plug-Ins/CLAP|Library/Audio/Plug-Ins/CLAP/FabCutie.clap"
    "app|Standalone/FabCutie.app|/Applications|"
)

for entry in "${components[@]}"; do
    IFS='|' read -r id bundle location userCopy <<< "$entry"

    root="$work/root-$id"
    mkdir -p "$root"
    ditto "$artefacts/$bundle" "$root/$(basename "$bundle")"

    # Installer otherwise "relocates" a bundle to wherever it finds one with
    # the same identifier (an old copy in ~/Library, a build folder...), so
    # the new version could land somewhere the host never looks.
    # Written by hand: pkgbuild --analyze doesn't list plug-in bundles.
    # Not version checked either, so reinstalling or going back to an
    # older version always replaces what is there.
    plist="$work/$id.plist"
    cat > "$plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
    <dict>
        <key>RootRelativeBundlePath</key>
        <string>$(basename "$bundle")</string>
        <key>BundleIsRelocatable</key>
        <false/>
        <key>BundleIsVersionChecked</key>
        <false/>
        <key>BundleHasStrictIdentifier</key>
        <true/>
        <key>BundleOverwriteAction</key>
        <string>upgrade</string>
    </dict>
</array>
</plist>
PLIST

    # Older FabCutie builds were copied by hand into the user's own plug-in
    # folders. Remove that copy (of this format only) so the host doesn't
    # find two FabCuties and load the old one.
    scripts="$work/scripts-$id"
    mkdir -p "$scripts"

    if [ -n "$userCopy" ]; then
        cat > "$scripts/preinstall" <<SCRIPT
#!/bin/bash
user="\$(stat -f%Su /dev/console)"
home="\$(dscl . -read "/Users/\$user" NFSHomeDirectory 2>/dev/null | awk '{print \$2}')"
[ -n "\$home" ] && rm -rf "\$home/$userCopy"
exit 0
SCRIPT
        chmod +x "$scripts/preinstall"
    fi

    # Make macOS rescan Audio Units so Logic Pro sees the new version.
    if [ "$id" = "au" ]; then
        printf '#!/bin/bash\nkillall -9 AudioComponentRegistrar 2>/dev/null || true\nexit 0\n' > "$scripts/postinstall"
        chmod +x "$scripts/postinstall"
    fi

    pkgbuild --root "$root" \
             --component-plist "$plist" \
             --identifier "io.github.danmcgrath10.fabcutie.$id" \
             --version "$version" \
             --install-location "$location" \
             --scripts "$scripts" \
             "$work/packages/FabCutie-$id.pkg"
done

sed "s/@VERSION@/$version/g" "$here/distribution.xml" > "$work/distribution.xml"

productbuild --distribution "$work/distribution.xml" \
             --package-path "$work/packages" \
             --resources "$work/resources" \
             "$out/FabCutie-$version-macOS.pkg"

echo "Built $out/FabCutie-$version-macOS.pkg"
