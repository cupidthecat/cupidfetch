#!/bin/sh

# Installs the cupidfetch config
# Public domain ~ Arthur Bacci 2024
# Edited by Frank - 2025

if [ -n "${XDG_CONFIG_HOME}" ]; then
    config="${XDG_CONFIG_HOME}"
else
    if [ -n "${HOME}" ]; then
        config="$HOME/.config"
    else
        echo 'Please set your HOME'
        exit 1
    fi
fi

echo "Your config directory is $config"
echo "Press enter to continue"
read -r discard

if [ -f "${config}/cupidfetch" ]; then
    echo "Your ${config}/cupidfetch is a file! Please delete it to continue"
    exit 1
else
    if [ ! -d "${config}/cupidfetch" ]; then
        echo "mkdir ${config}/cupidfetch......"
        mkdir -p "${config}/cupidfetch"
    else
        echo "Your ${config}/cupidfetch already exists"
    fi
fi

if [ -d "${config}/cupidfetch/cupidfetch.conf" ]; then
    echo "Your ${config}/cupidfetch/cupidfetch.conf is a directory! Please delete it to continue"
    exit 1
else
    if [ -f "${config}/cupidfetch/cupidfetch.conf" ]; then
        echo "Your ${config}/cupidfetch/cupidfetch.conf already exists. I won't touch it."
    else
        if [ -f "data/cupidfetch.conf" ]; then
            echo "Creating ${config}/cupidfetch/cupidfetch.conf from the default config in data/"
            cp data/cupidfetch.conf "${config}/cupidfetch/cupidfetch.conf"
        else
            echo "No data/cupidfetch.conf found. Creating a new default config at ${config}/cupidfetch/cupidfetch.conf..."
            cat << 'EOF' > "${config}/cupidfetch/cupidfetch.conf"
# Default cupidfetch configuration

# List of modules (space-separated)
modules = hostname username distro linux_kernel uptime pkg term shell wm session ip memory cpu storage

# Memory display settings
memory.unit-str = MB
memory.unit-size = 1000000

# Storage display settings
storage.unit-str = GB
storage.unit-size = 1000000000
EOF
        fi
    fi
fi

echo "Done! Please run cupidfetch and it will probably find your config."
echo "Feel free to tweak it, it's at ${config}/cupidfetch/cupidfetch.conf"
echo "Your log is probably at ${config}/cupidfetch/log.txt"