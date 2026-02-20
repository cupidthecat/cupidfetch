
# cupidfetch

## Overview

**This is Cupid, the cat.**

![cupid](images/cupid.png)

![baby cupid](images/smol.png)

**Cupid the cat loves Linux!**

cupidfetch is a system information retrieval tool written in C for Linux systems. It's a beginner-friendly, work-in-progress hobby project aimed at learning and exploring programming concepts.

## Features

**✔️ Fetches and displays various system details:**

- Hostname  
- Distribution (with **auto-add** for unknown distros — see below!)  
- Kernel version  
- Uptime  
- Package count  
- Package count (with distro command + package-manager fallback detection)  
- Shell  
- Terminal  
- Desktop environment  
- Window manager  
- Display server (Wayland/X11)  
- Network status (interface state, local/public IP with IPv4/IPv6 local detection)  
- Battery level  
- GPU  
- Username  
- Memory usage  
- CPU model + usage  
- Storage/disk usage per mount  
- Signal Handling for Window Resize, automatically updates display with terminal resizing (`SIGWINCH`)  
- And more

**✔️ Auto-add unknown distros to `distros.def`:**  
If cupidfetch detects an unrecognized distro in `/etc/os-release`, it automatically inserts a new line into `distros.def` (under an "auto added" section) so the distro becomes recognized in subsequent runs.  

**⬜ Enhance visual representation (soon):**  

- ASCII art for distributions  
- Unicode icons  

**✔️ Built for beginners:**  

- Simple code  
- Easy to understand and contribute  

**Supported Distros:**  

- Debian (Ubuntu, elementary, Mint) [Verified ✔️]  
- Arch (Manjaro, Artix, EndeavourOS) [Verified ✔️]  
- Fedora [Verified ✔️]  
- Others are in `data/distros.def` (now automatically updated if not recognized)

## Dependencies

cupidfetch relies on the following components and tools:

1. **C Compiler:**  
   Written in C, requiring a C compiler. The recommended compiler is GCC.

2. **cupidconf:**  
   **cupidfetch now uses cupidconf instead of inih!**  
   - Allows more flexible configuration parsing, supporting wildcards (`*`) for key matching, list retrieval, etc.

3. **Git:**  
   - For cloning the repository.

## How to Install Dependencies

### GCC (GNU Compiler Collection)

Often available via your distro’s package manager. On Debian/Ubuntu-based systems:
```bash
sudo apt update && sudo apt install build-essential
```

### Git

Install through your distro’s package manager. On Debian/Ubuntu-based systems:
```bash
sudo apt install git
```

## Usage

1. **Clone** the repository:
   ```bash
   git clone https://github.com/frankischilling/cupidfetch
   ```
2. **Compile**:
   ```bash
   make
   ```
   Or manually:
   ```bash
   gcc -o cupidfetch src/config.c src/main.c src/modules.c src/print.c libs/cupidconf.c
   ```
3. **Run**:
   - **From the current directory**:
     ```bash
     ./cupidfetch
     ```
   - **From anywhere** (optional):
     ```bash
     sudo mv cupidfetch /usr/local/bin
     cupidfetch
     ```
4. **Debug**:
   - Use `make clean asan` or `make clean ubsan` to check for overflows/memory leaks or undefined behavior.

5. **View the Output**:  
   Prints system info such as distro, kernel, uptime, etc., and displays ASCII art for recognized distros.

## Configuration File

You can use the `install-config.sh` script to create a configuration file for cupidfetch. 

- **Location**:
  ```
  ${XDG_CONFIG_HOME}/cupidfetch/cupidfetch.conf
  ```
  or, if `$XDG_CONFIG_HOME` is not set:
  ```
  $HOME/.config/cupidfetch/cupidfetch.conf
  ```

### Example `cupidfetch.conf`
```ini
# List of modules (space-separated)
modules = hostname username distro linux_kernel uptime pkg term shell de wm display_server net ip battery gpu memory cpu storage

# Memory display settings
memory.unit-str = MB
memory.unit-size = 1000000

# Storage display settings
storage.unit-str = GB
storage.unit-size = 1000000000

# Network display settings
# false = mask public IP (default), true = show full public IP
network.show-full-public-ip = false
```
Adjust as needed; e.g., switch units to test different scale factors.

## Auto-Add Unknown Distros

Whenever cupidfetch encounters a distro that isn’t listed in `data/distros.def`, it:

1. Warns you that the distro is unknown.  
2. Inserts a new `DISTRO("shortname", "Capitalized", "pacman -Q | wc -l")` entry into `distros.def`, under an `/* auto added */` comment.
3. Re-parses `distros.def`, so subsequent runs show the proper distro name.

> **Note**: The default package command is set to `pacman -Q | wc -l`. If your newly added distro uses a different package manager (e.g., `dpkg`, `dnf`, or something else), you might want to edit `distros.def` manually to change the package count command.

## Adding Support Manually

If you prefer **manual** updates (or want to tweak the auto-added lines), edit `data/distros.def`. For example, to add “cupidOS” which uses `dpkg`:
```text
DISTRO("ubuntu" , "Ubuntu" , "dpkg -l | tail -n+6 | wc -l")
DISTRO("cupidOS", "cupidOS", "dpkg -l | tail -n+6 | wc -l")
```
However, thanks to auto-add, you often won’t need to touch this file for new distros—cupidfetch will do it for you!

## Log File

If `cupidfetch` cannot create a log file at `.../cupidfetch/log.txt`, it falls back to `stderr`.  
To suppress logging:
```bash
cupidfetch 2> /dev/null
```

## Requirements

- Linux system  
- Basic knowledge of C programming  
- Curiosity for exploring system information  

## How to Contribute

Everyone is welcome—beginner or expert!

- **Discord:** [Join here](https://discord.gg/698GBkg2KR)  
- Beginners: Great project to learn, ask questions, and try new ideas.  
- Experienced devs: Help refine, optimize, or expand coverage for more distros.

## To-Do List

- [ ] Add ASCII art for each distro (e.g., “Arch kitten saying ‘I use Arch btw’”)
- [ ] Add colors/user theming
- [ ] Fix alignment for proper ASCII art display
- [ ] Add Unicode icons (Nerd Fonts?)
- [ ] `make install`
- [ ] Arch Linux AUR package
- [ ] Per-module config sections
- [ ] Implement dynamic WM & DE detection (remove hard-coded checks)
- [ ] Wayland
- [X] Auto-detect and update distros in `distros.def`
- [X] Signal Handling for Window Resize (`SIGWINCH`)
- [X] Improve distro detection
- [X] Add memory info
- [X] Add storage info
- [X] Fix terminal info, DE, and WM detection
- [X] Implement a config system (**migrated to cupidconf**)
- [X] Clean up code and improve display formatting

## Notes

This project is a continuous work-in-progress for learning and experimentation. Expect frequent changes or refactoring as new features are added.

## License

[GNU General Public License 2.0 or later](https://www.gnu.org/licenses/old-licenses/gpl-2.0-standalone.html)
