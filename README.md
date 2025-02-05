# cupidfetch

## Overview

**this is cupid, the cat.**

![cupid](images/cupid.png)

![baby cupid](images/smol.png)

**cupid the cat loves linux!**

cupidfetch is a system information retrieval tool written in C for Linux systems. It's a beginner-friendly, work-in-progress hobby project aimed at learning and exploring programming concepts.

## Features

**✔️ Fetches and displays various system details:**

- Hostname
- Distribution
- Kernel version
- Uptime
- Package count
- Shell
- Terminal
- Desktop environment
- Username
- Memory
- Signal Handling for Window Resize, Automatically updates display with terminal resizing (`SIGWINCH`)
- and others

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
- Others are in `data/distros.def` (feel free to add yours)

## Dependencies

cupidfetch relies on the following components and tools:

1. **C Compiler:** cupidfetch is written in the C programming language, and compilation requires a C compiler. The recommended compiler is GCC (GNU Compiler Collection).

   - [GCC (GNU Compiler Collection)](https://gcc.gnu.org/): The GNU Compiler Collection is a set of compilers for various programming languages, including C.

2. **cupidconf:**  
   **cupidfetch now uses cupidconf instead of inih!**  
   **cupidconf** is a simple key-value configuration parsing library written for this project. It provides efficient and flexible configuration parsing, allowing users to easily customize `cupidfetch`.  
   
   - Configuration files are now more flexible, supporting wildcards (`*`) for key matching.
   - Supports list retrieval, allowing for more structured settings.

3. **Git:** cupidfetch uses Git to clone the repository for easy deployment.

   - If you don't have Git installed, you can download and install it from the [Git website](https://git-scm.com).

## How to Install Dependencies

### GCC (GNU Compiler Collection)

GCC is often available through the package manager of your Linux distribution. For example, on Debian/Ubuntu-based systems, you can install it using:

```
sudo apt update && sudo apt install build-essential
```

### Git

Git can be installed through the package manager of your Linux distribution. For example, on Debian/Ubuntu-based systems, you can install it using:

```
sudo apt install git 
```

## Usage

1. **Clone**  
   ```
   git clone https://github.com/frankischilling/cupidfetch
   ```

2. **Compilation:**  
   ```
   make
   ```
   or manually:
   ```
   gcc -o cupidfetch src/config.c src/main.c src/modules.c src/print.c libs/cupidconf.c
   ```

3. **Execution:**
   - **To run from the current directory:**  
     ```
     ./cupidfetch
     ```
   - **To run from anywhere:**  
     Move the executable to `/usr/local/bin`:  
     ```
     sudo mv cupidfetch /usr/local/bin
     ```
     Then execute it directly:  
     ```
     cupidfetch
     ```

4. **Debugging:**  
   Use `make clean asan` or `make clean ubsan` to check for overflows/memory leaks or undefined behavior.

5. **Output:** System information with:
   - **(WIP)** ASCII art representing the Linux distribution
   - **(WIP)** Unicode icons for different details

## Configuration File

You can use the `install-config.sh` script to create a configuration file for cupidfetch. 

### **Configuration file location**
The configuration file for `cupidfetch` is located at:
```
${XDG_CONFIG_HOME}/cupidfetch/cupidfetch.conf
```
or, if `$XDG_CONFIG_HOME` is not set:
```
$HOME/.config/cupidfetch/cupidfetch.conf
```
This file allows you to customize the displayed information.

### **Example Configuration (`cupidfetch.conf`)**
```ini
# List of modules (space-separated)
modules = hostname username distro linux_kernel uptime pkg term shell de ip memory cpu storage

# Memory display settings
memory.unit-str = MB
memory.unit-size = 1000000

# Storage display settings
storage.unit-str = GB
storage.unit-size = 1000000000
```

You can test different configurations by modifying this file and running:
```
cupidfetch
```

### **Alternative Storage & Memory Units for Testing**
To test different configurations, try replacing the default settings in `cupidfetch.conf`:

#### **Kilobytes (KB) for Memory, Megabytes (MB) for Storage**
```ini
memory.unit-str = KB
memory.unit-size = 1000

storage.unit-str = MB
storage.unit-size = 1000000
```

#### **Gigabytes (GB) for Memory, Terabytes (TB) for Storage**
```ini
memory.unit-str = GB
memory.unit-size = 1000000000

storage.unit-str = TB
storage.unit-size = 1000000000000
```

## Log File

If it can't create a log at `.../cupidfetch/log.txt`, it will output to stderr.  
To ignore logging, run:
```
cupidfetch 2> /dev/null
```

## Requirements

- Linux system
- Basic understanding of C programming
- Curiosity for exploring system information

## How to Contribute

Everyone is welcome to contribute, regardless of experience level!

- Join our **Discord**: [Discord](https://discord.gg/698GBkg2KR)
- Beginners: Great project to learn and get involved.
- Experienced developers: Help improve and optimize cupidfetch.

## Adding Support for More Distros

To add support for more Linux distros, edit `data/distros.def`.

### **Example distros.def**
```
DISTRO("ubuntu", "Ubuntu", "dpkg -l | tail -n+6 | wc -l")
```
If you wanted to add **cupidOS**, which also uses `dpkg`, you would modify it like this:
```
DISTRO("ubuntu" , "Ubuntu" , "dpkg -l | tail -n+6 | wc -l")
DISTRO("cupidOS", "cupidOS", "dpkg -l | tail -n+6 | wc -l")
```

## To-Do List

- [ ] Add ASCII art for each distro (e.g., Arch kitten saying "I use Arch btw")
- [ ] Auto-detect and update distros in `distros.def`
- [ ] Add colors/user theming
- [ ] Fix alignment for proper ASCII art display
- [ ] Add Unicode icons (Nerd Fonts?)
- [ ] `make install`
- [ ] Arch Linux AUR package
- [ ] Per-module config sections
- [ ] Implement dynamic WM & DE detection (remove hard-coded values)
- [X] Signal Handling for Window Resize (`SIGWINCH`)
- [X] Improve distro detection
- [X] Add memory info
- [X] Add storage info
- [X] Fix terminal info, DE, and WM detection
- [X] Implement a config system (**migrated to cupidconf**)
- [X] Clean up code and improve display formatting

## Notes

This project is a work-in-progress for learning and experimentation.

## License

[GNU General Public License 2.0 or later](https://www.gnu.org/licenses/old-licenses/gpl-2.0-standalone.html)
