# OpenRGB Wallpaper Engine Sync Plugin

A high-performance companion plugin for **OpenRGB** that synchronizes your PC case lighting with **Wallpaper Engine** using Wallpaper Engine's native **Razer Chroma / Corsair iCUE** output stream.

* **Zero File Tampering**: No DLL injection, memory hooking, or modified Steam files.
* **Native Integration**: Integrates directly as a tab inside OpenRGB.
* **Hardware-Safe**: High-speed reader decoupled from an adaptive, throttled hardware dispatcher.
* **Update-Proof**: Steam updates to Wallpaper Engine will never break your lighting sync.

---

## Compatibility

| Component | Requirement |
| :--- | :--- |
| **OpenRGB** | **v0.9, v1.0, and 1.0 Release Builds** (Plugin API Version 5) |
| **Wallpaper Engine** | **v2.0+ via Steam** (iCUE & Chroma SDK plugin enabled) |
| **Platforms** | **Windows** (x64, x86) &bull; **Linux** (amd64, arm64, armhf, i386) |
| **Chroma Runtime** | [Standalone ZIP](https://github.com/Emieldev/OpenRGBWallpaperEngineSyncPlugin/releases/latest/download/razer-chroma-runtime-standalone.zip) (No Razer software/hardware required) or [Official Installer](https://rzr.to/chroma-download) |

> [!IMPORTANT]
> **Dynamic Wallpaper Disclaimer**: This plugin is designed for **ambient, atmospheric, and scenic wallpapers** (e.g. nature, cityscapes, cinematic videos, aesthetic art). **It is not recommended for fast rainbow color-cycling backgrounds (e.g. *Razer Bedroom*)**. Internal PC case buses (I²C on GPUs, 100 kHz SMBus on RAM) cannot sustain 30 Hz multi-color strobing without hardware bus saturation or LED flickering.

---

## Quick Start (3 Steps)

### 1. Install Chroma Runtime
Wallpaper Engine emits its lighting stream through the standard Razer Chroma SDK. You **do not** need any Razer hardware or Razer Synapse installed.
* **Recommended (Zero Bloat)**: Download **[`razer-chroma-runtime-standalone.zip`](https://github.com/Emieldev/OpenRGBWallpaperEngineSyncPlugin/releases/latest/download/razer-chroma-runtime-standalone.zip)**, extract, right-click `install.bat`, and select **Run as administrator**.
* *Alternative*: If you already use Razer peripherals, install [Razer Synapse 4](https://www.razer.com/synapse-4) with the **Chroma Connect** module enabled.

### 2. Enable Plugin in Wallpaper Engine
In Wallpaper Engine: open **Settings** $\to$ **Plugins**, and ensure **iCUE & Chroma SDK** is checked.

### 3. Install Plugin into OpenRGB
1. Download `OpenRGBWallpaperEngineSyncPlugin-windows-x64.zip` from [Releases](https://github.com/Emieldev/OpenRGBWallpaperEngineSyncPlugin/releases).
2. Copy `OpenRGBWallpaperEngineSyncPlugin.dll` into:
   ```
   %APPDATA%\OpenRGB\plugins\OpenRGBWallpaperEngineSyncPlugin.dll
   ```
   *(On Linux: extract `libOpenRGBWallpaperEngineSyncPlugin.so` into `~/.config/OpenRGB/plugins/`)*
3. Restart **OpenRGB** (Run as Administrator for hardware bus access).
4. In OpenRGB's **Wallpaper Engine** tab, select your devices and check **Enable Wallpaper Engine Lighting Sync**.

---

## Key Features

* **Calibrated Color & Diffuser Bleed Correction**: Perceptual gamma (~2.1) and white-floor cancellation eliminate blue/white wash on frosted diffusers. Deep reds stay rich crimson (no pink) and dark greens glow with a soft turquoise hue matching calibrated keyboards.
* **Hardware-Safe Bus Pacing**: Dedicated, collision-free intervals for GPU I²C (100ms), DRAM SMBus (60ms), and ARGB fans/coolers (40ms) eliminate flickering, PWM resets, and packet congestion.
* **Ultra-Low Workload (< 0.1% CPU, 0% GPU, Flat RAM)**: Pre-caches device modes, zones, and LED structures for zero-allocation 30 FPS dispatch. Idle frames bypass parsing entirely.
* **Razer-Style Flow Smoothing & Zero Blackouts**: Sub-pixel bilinear diffusion and semi-instant crossfading (~360ms) glide smoothly into new wallpapers with zero blackout gaps.
* **Automatic Spatial Case Mapping**: Maps the wallpaper across top fans, front intake, RAM, GPU, and cooler zones with zero manual configuration (manual overrides available).
* **Freeze on Pause**: Automatically holds lighting static when Wallpaper Engine pauses, exactly matching native Razer Chroma behavior (with an optional toggle to loop ambient colors).

---

## Configuration Tips

* **0 dB Silent GPU Fans**: If your graphics card stops its fans at idle (< 50°C) and the fan LEDs turn off with the motors, set a gentle minimum fan curve (e.g. 20% RPM in MSI Afterburner) to keep them illuminated continuously.

---

## Building from Source

```bash
# Using CMake (Cross-Platform)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Using QMake
qmake OpenRGBWallpaperEngineSyncPlugin.pro && make -j$(nproc)

# Windows MSVC Batch Script
build.bat
```

---

## Legal Notices & Disclaimers

* **Use at Your Own Risk**: This software is provided "as is", without warranty of any kind. Controlling computer lighting involves low-level hardware communication over I²C/SMBus. While this plugin enforces strict rate limits and uses OpenRGB's hardware abstraction layer, users install and run this software entirely at their own risk. The authors accept no liability for any hardware damage, data loss, or system instability.
* **Trademarks**: Wallpaper Engine is a trademark of Kristjan Skutta / Wallpaper Engine Team. Steam is a trademark of Valve Corporation. Razer, Chroma, and iCUE are trademarks of their respective owners. OpenRGB is an open-source project by Adam Honse (CalcProgrammer1). This project is an independent open-source plugin and is not affiliated with or endorsed by these entities.
* **Fair Use & DRM Compliance**: This plugin does not modify, hook, or tamper with proprietary binaries of Wallpaper Engine or Razer, and does not bypass DRM or licensing controls. A legitimate copy of Wallpaper Engine via Steam is required.
* **AI Assistance**: Developed with pair-programming assistance from Google DeepMind's Antigravity AI assistant, and verified for hardware safety and stability.
* **License**: Licensed under the **[GNU General Public License v3.0 (GPLv3)](LICENSE)**.
