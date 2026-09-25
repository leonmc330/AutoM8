#!/usr/bin/env bash
# Picks this build's release number and prints it for $GITHUB_OUTPUT:
#   number=N  label=vNNN  publish=true    on a push to main: highest vNNN tag + 1
#   number=0  label=dev   publish=false   anywhere else
# Runs on main never overlap (see "concurrency" in build.yml), so every job of a run
# computes the same number.
set -euo pipefail

if [ "${GITHUB_EVENT_NAME:-}" = pull_request ] || [ "${GITHUB_REF:-}" != refs/heads/main ]; then
	printf 'number=0\nlabel=dev\npublish=false\n'
	exit 0
fi
last=$(git ls-remote --tags --refs origin | grep -oE 'refs/tags/v[0-9]{3}$' | sed 's#refs/tags/v##' | sort -n | tail -1 || true)
number=$((10#${last:-0} + 1))
if [ "$number" -gt 999 ]; then
	echo "::error::v999 reached: time for a new version scheme" >&2
	exit 1
fi
printf 'number=%d\nlabel=v%03d\npublish=true\n' "$number" "$number"
