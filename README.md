# Kadran

**Explore the OSCQuery devices of your network and play them on a page of controls.** A Pixel Stereo application, built on [pxs](https://github.com/PixelStereo/pxs).

<!-- release-downloads:start -->
**LATEST VERSION · [v0.3.0](https://github.com/PixelStereo/Kadran/releases/tag/v0.3.0)**

[![macOS Apple Silicon](https://img.shields.io/badge/macOS-Apple_Silicon-6b7787?style=for-the-badge&logo=apple&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/Kadran/releases/download/v0.3.0/Kadran-macos-arm64-v0.3.0.zip)&nbsp;&nbsp;[![macOS Intel](https://img.shields.io/badge/macOS-Intel-515b69?style=for-the-badge&logo=apple&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/Kadran/releases/download/v0.3.0/Kadran-macos-x64-v0.3.0.zip)&nbsp;&nbsp;[![Windows Installer](https://img.shields.io/badge/Windows-Installer-2b6fb6?style=for-the-badge&logo=windows&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/Kadran/releases/download/v0.3.0/Kadran-windows-x64-v0.3.0-Setup.exe)&nbsp;&nbsp;[![Linux AppImage](https://img.shields.io/badge/Linux-AppImage-b8821c?style=for-the-badge&logo=linux&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/Kadran/releases/download/v0.3.0/Kadran-linux-x64-v0.3.0.AppImage)&nbsp;&nbsp;[![Raspberry Pi AppImage](https://img.shields.io/badge/Raspberry_Pi-AppImage-a22846?style=for-the-badge&logo=raspberrypi&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/Kadran/releases/download/v0.3.0/Kadran-linux-arm64-v0.3.0.AppImage)
<!-- release-downloads:end -->

On a **Mac**, unzip, move Kadran to Applications and Control-click › Open the first time (it is signed ad hoc, not notarized). On **Windows**, run the installer. On **Linux**, make the AppImage executable and run it (or unpack the tar.gz).

Open Kadran and it opens in your browser (and on any phone, tablet or computer of the network, at the address it shows):

- **Devices** (left): found by mDNS, or added by hand (`+ Device`, `host:port`). Unfold one to read its namespace. Filter the addresses with the search field.
- **Page** (right): drag an address onto it, double-click it, or right-click for the widget (fader, encoder, number, switch, buttons…). Two numbers make an XY pad, or two controls. Select several with Cmd/Ctrl-click or Shift-click.
- A control is a view of the remote parameter: it follows the device and writes back over OSC. Its address is shown on its tile.
- **Edit mode:** Cmd/Ctrl+E (or the Edit button): move, resize, restyle, add pages.
- **Keeping it:** what is on the page is saved as you work and comes back at the next launch. **Save** and **Open** keep named interfaces (`.kadran` files).

The page is served without a code, to the whole network: use it on a network you trust.

## Build

Qt 6.4 or later, C++17.

```sh
git clone --recursive https://github.com/PixelStereo/Kadran.git
cd Kadran
./build.sh            # macOS, Linux (apt) and Windows (Git Bash; or build.cmd): installs what is missing, builds, opens Kadran
```

Or by hand:

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build
open build/Kadran*.app        # macOS; elsewhere run the executable
```

`build/kadran_fake_device` is a small OSCQuery device to try it on.

## Licence

GNU Affero General Public License v3.0 (see `LICENSE`).
