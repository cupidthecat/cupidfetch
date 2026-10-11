#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/unrelated" "$tmp/home"
cd "$tmp/unrelated"

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

run_install() {
    printf '\n' | env HOME="$tmp/home" XDG_CONFIG_HOME="$1" sh "$root/install-config.sh"
}

# Resolve the packaged template relative to the installer, including spaces.
run_install "$tmp/xdg config"
cmp "$root/data/cupidfetch.ini" "$tmp/xdg config/cupidfetch/cupidfetch.conf" ||
    fail 'installed config differs from the shipped template'
[ ! -e "$tmp/home/.config" ] || fail 'XDG_CONFIG_HOME was ignored'

# Existing user content must survive another installation.
printf 'modules = hostname\n; keep my settings\n' > "$tmp/xdg config/cupidfetch/cupidfetch.conf"
cp "$tmp/xdg config/cupidfetch/cupidfetch.conf" "$tmp/expected"
run_install "$tmp/xdg config"
cmp "$tmp/expected" "$tmp/xdg config/cupidfetch/cupidfetch.conf" ||
    fail 'existing configuration changed'

# Fall back to HOME when XDG_CONFIG_HOME is empty.
run_install ''
cmp "$root/data/cupidfetch.ini" "$tmp/home/.config/cupidfetch/cupidfetch.conf" ||
    fail 'HOME fallback did not install the shipped template'

mkdir -p "$tmp/directory/cupidfetch/cupidfetch.conf"
if run_install "$tmp/directory"; then
    fail 'accepted a directory as the config file'
fi
mkdir -p "$tmp/blocked"
printf 'file\n' > "$tmp/blocked/cupidfetch"
if run_install "$tmp/blocked"; then
    fail 'reported success when the config directory could not be created'
fi

# A missing template must fail instead of installing a stale fallback.
mkdir -p "$tmp/package"
cp "$root/install-config.sh" "$tmp/package/install-config.sh"
if printf '\n' | env HOME="$tmp/home" XDG_CONFIG_HOME="$tmp/missing" \
    sh "$tmp/package/install-config.sh"; then
    fail 'accepted a missing template'
fi
[ ! -e "$tmp/missing/cupidfetch/cupidfetch.conf" ] ||
    fail 'created a config without the template'

if printf '\n' | env HOME='' XDG_CONFIG_HOME='' sh "$root/install-config.sh"; then
    fail 'accepted missing HOME and XDG_CONFIG_HOME'
fi
if env HOME="$tmp/home" XDG_CONFIG_HOME="$tmp/unconfirmed" \
    sh "$root/install-config.sh" </dev/null; then
    fail 'installed without confirmation'
fi
[ ! -e "$tmp/unconfirmed/cupidfetch/cupidfetch.conf" ] ||
    fail 'created a config without confirmation'

printf 'Installer tests passed.\n'
