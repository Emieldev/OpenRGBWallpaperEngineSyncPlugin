# Standalone Razer Chroma Runtime for Wallpaper Engine

This package provides the official 64-bit and 32-bit **Razer Chroma SDK Runtime** (`RzChromaSDK64.dll` and `RzChromaSDK.dll`) required for **Wallpaper Engine** to output live RGB lighting data to shared memory.

---

## Why Is This Needed?

Wallpaper Engine includes an **iCUE & Chroma SDK** plugin (`ledextensions32.dll` / `ledextensions64.dll`). When active, it samples live wallpaper scenes and outputs real-time LED matrices into Razer Chroma's shared memory ring buffer (`Global\{74164FAD-E73C-4FA1-A9AA-70813315ED9C}`).

Without the Chroma runtime DLLs present on your system:
* Wallpaper Engine cannot load its Chroma plugin.
* No shared memory stream is created.
* OpenRGB (or any other third-party RGB tool) cannot receive wallpaper colors.

**Important Note:**
* **No Razer hardware is required.**
* **No Razer Synapse software or Razer account is required.**
* These DLLs simply provide the standard Windows runtime libraries that Wallpaper Engine looks for.

---

## Quick Installation

1. Right-click **`install.bat`** and select **Run as administrator**.
2. The script will copy:
   * `x64\RzChromaSDK64.dll` $\to$ `C:\Windows\System32\`
   * `x86\RzChromaSDK.dll` $\to$ `C:\Windows\SysWOW64\`
   * (And optionally directly into your Wallpaper Engine folder if detected).
3. In Wallpaper Engine: Go to **Settings** $\to$ **Plugins**, and ensure **iCUE & Chroma SDK** is checked.
4. In OpenRGB: Go to the **Wallpaper Engine** tab, select your devices, and check **Enable Wallpaper Engine Lighting Sync**.

---

## Uninstallation

To remove the runtime DLLs at any time, right-click **`uninstall.bat`** and select **Run as administrator**.

---

## File Details

* **`x64/RzChromaSDK64.dll`**: Razer Chroma SDK (64-bit Windows PC runtime), Version 3.8.0.238 / Product Version 4.0.1.
* **`x86/RzChromaSDK.dll`**: Razer Chroma SDK (32-bit Windows PC runtime), Version 3.8.0.238 / Product Version 4.0.1.
* **Copyright**: © Razer Inc. All rights reserved.
