#!/bin/sh
# Fetch + extract Mesa 7.0.4 (the vendored GL front-end for the ICD).
# The source tree is large/third-party -> gitignored; regenerate with this script.
set -e
cd "$(dirname "$0")"
URL=https://archive.mesa3d.org/older-versions/7.x/7.0/MesaLib-7.0.4.tar.gz
[ -f MesaLib-7.0.4.tar.gz ] || curl -L --connect-timeout 30 -o MesaLib-7.0.4.tar.gz "$URL"
tar xzf MesaLib-7.0.4.tar.gz
echo "Mesa 7.0.4 extracted to $(pwd)/Mesa-7.0.4"
