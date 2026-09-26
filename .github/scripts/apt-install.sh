#!/bin/sh
# apt-install.sh DIR PACKAGE...
# Installs apt packages from the .deb files kept in DIR (restored by actions/cache, keyed on the
# runner image), which skips the slow `apt-get update`. On a miss, or if the image changed under
# them, downloads them into DIR first.
set -e
mkdir -p "$1"
dir=$(cd "$1" && pwd) # absolute: apt reads a relative Dir::Cache::archives from /var/cache/apt
shift
sudo rm -f /var/lib/man-db/auto-update # no man page index rebuild after installing (seconds per job)

if ls "$dir"/*.deb >/dev/null 2>&1; then
	if sudo apt-get install -y --no-install-recommends "$dir"/*.deb; then exit 0; fi
	echo "::notice::cached .deb files did not install, downloading them again"
	rm -rf "$dir"
fi
mkdir -p "$dir/partial"
sudo apt-get update
sudo apt-get install -y --no-install-recommends --download-only -o Dir::Cache::archives="$dir" "$@"
sudo rm -rf "$dir/partial" "$dir/lock"
sudo chown -R "$(id -u):$(id -g)" "$dir"
if ls "$dir"/*.deb >/dev/null 2>&1; then
	sudo apt-get install -y --no-install-recommends "$dir"/*.deb
else # nothing was downloaded (all on the image already?): the plain way
	sudo apt-get install -y --no-install-recommends "$@"
fi
