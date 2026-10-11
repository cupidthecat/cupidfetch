#!/bin/sh

# Installs the cupidfetch config
# Public domain ~ Arthur Bacci 2024
# Edited by Frank - 2025

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

if [ -n "${XDG_CONFIG_HOME:-}" ]; then
    config=$XDG_CONFIG_HOME
elif [ -n "${HOME:-}" ]; then
    config=$HOME/.config
else
    fail 'Please set HOME or XDG_CONFIG_HOME.'
fi

destination=$config/cupidfetch/cupidfetch.conf
printf 'Your config directory is %s\nPress enter to continue\n' "$config"
read -r discard || fail 'Installation cancelled: no confirmation received.'

if [ -d "$destination" ]; then
    fail "$destination is a directory. Please move it before installing."
fi
if [ -e "$destination" ] || [ -L "$destination" ]; then
    printf '%s already exists. I will leave it unchanged.\n' "$destination"
    exit 0
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) ||
    fail 'Cannot find the installer directory.'
template=$script_dir/data/cupidfetch.ini
[ -f "$template" ] && [ -r "$template" ] ||
    fail "Cannot read the default config: $template"

mkdir -p "$config/cupidfetch" || fail "Cannot create $config/cupidfetch."
# noclobber also protects a config created after the existence check above.
(set -C; cat "$template" > "$destination") ||
    fail "Cannot write $destination."

printf 'Config installed at %s\n' "$destination"
