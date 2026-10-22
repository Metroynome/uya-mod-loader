#!/bin/sh
# Prepare an Alpine PS2DEV container. Run with sh; no executable bit required.
set -eu

apk add --no-cache make git build-base ca-certificates zip unzip
: "${PS2SDK:?Use a PS2DEV image with PS2SDK configured}"
command -v "${EE_PREFIX:-ee-}gcc" >/dev/null

LIBRAC3_DIR=${LIBRAC3_DIR:-/opt/uya-mod-loader-deps/librac3}
LIBUYA_DIR=${LIBUYA_DIR:-/opt/uya-mod-loader-deps/horizon-uya-patch/libuya}
LIBRAC3_REF=${LIBRAC3_REF:-b14f8259e6072d8a770d86b08895ad33fb398dc8}
LIBUYA_REF=${LIBUYA_REF:-737dd562c7ea142de3e708594f74c239f19af4b0}
export LIBRAC3_DIR LIBUYA_DIR

clone_library() {
    url=$1
    destination=$2
    reference=$3
    if [ -e "$destination" ]; then
        # Do not reset or discard an existing checkout's work.
        actual=$(git -C "$destination" rev-parse HEAD)
        expected=$(git -C "$destination" rev-parse "$reference^{commit}")
        if [ "$actual" != "$expected" ]; then
            echo "Library revision mismatch in $destination; use a fresh dependency directory." >&2
            exit 1
        fi
        if [ -n "$(git -C "$destination" status --porcelain)" ]; then
            echo "Library checkout is modified: $destination" >&2
            exit 1
        fi
    else
        mkdir -p "$(dirname "$destination")"
        git clone "$url" "$destination"
        git -C "$destination" checkout --detach "$reference"
    fi
}

clone_library https://github.com/Metroynome/librac3.git "$LIBRAC3_DIR" "$LIBRAC3_REF"
clone_library https://github.com/Horizon-Private-Server/horizon-uya-patch.git "$(dirname "$LIBUYA_DIR")" "$LIBUYA_REF"
test -f "$LIBUYA_DIR/Makefile.ntsc"

# Makefile.eeglobal compiles isolated copies of these sources for each supported region.
# Optional arguments let this script initialize and build in a single invocation.
if [ "$#" -gt 0 ]; then
    exec "$@"
fi
