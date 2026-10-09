#!/bin/sh
set -eu
cd "$(dirname "$0")"
make
exec python3 web/server.py --allow-origin https://shellforge-terminal.mellowsole1.chatgpt.site
