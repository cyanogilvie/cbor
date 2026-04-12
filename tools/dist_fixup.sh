#!/bin/sh
# Called by meson.add_dist_script() during "meson dist".
set -e

dist="$MESON_DIST_ROOT"
build="$MESON_BUILD_ROOT"

# Bake generated docs for users who don't have pandoc
for f in cbor.html cbor.n README.md; do
    if [ -f "$build/doc/$f" ]; then
        cp "$build/doc/$f" "$dist/doc/$f"
    fi
done

# Remove .github — not useful in release tarballs
rm -rf "$dist/.github"

# Replace symlinks with copies of their targets so the tarball works
# on Windows (which doesn't support symlinks without special permissions)
find "$dist" -type l | while read -r link; do
    target=$(readlink -f "$link")
    if [ -e "$target" ]; then
        rm "$link"
        if [ -d "$target" ]; then
            cp -a "$target" "$link"
        else
            cp "$target" "$link"
        fi
    fi
done
