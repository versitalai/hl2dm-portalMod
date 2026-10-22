#!/usr/bin/env sh
# Linux/macOS launcher. Usage: ./start.sh [--mock]
cd "$(dirname "$0")" || exit 1
if ! command -v node >/dev/null 2>&1; then
	echo "Node.js 18+ is required: https://nodejs.org (or your package manager)." >&2
	exit 1
fi
node doctor.js --first-run
exec node server.js --open "$@"
