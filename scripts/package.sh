#!/usr/bin/env bash
# Build a release tarball. Run this on the Steam Frame itself: the binary has to link against the
# headset's glibc, because the eye server's shared mutex uses glibc's layout (a static musl build
# would lock it with an incompatible layout).
set -euo pipefail
cd "$(dirname "$0")/.."

cargo="${CARGO:-cargo}"
command -v "$cargo" >/dev/null || cargo="$HOME/.cargo/bin/cargo"
version="$(sed -n 's/^version = "\(.*\)"/\1/p' Cargo.toml | head -n 1)"
name="frameeyeosc-$version-steamframe-aarch64"

"$cargo" test --release
"$cargo" build --release

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/frameeyeosc"
cp target/release/frameeyeosc install.sh contrib/frameeyeosc.service contrib/frameeyeosc.env.example \
    LICENSE THIRD_PARTY_LICENSES.md README.md README.ja.md CHANGELOG.md "$stage/frameeyeosc/"
mkdir -p dist
tar -C "$stage" -czf "dist/$name.tar.gz" frameeyeosc
echo "dist/$name.tar.gz"
