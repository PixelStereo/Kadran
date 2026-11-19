# Kadran

Explore the OSCQuery devices of the local network and play them on a page of controls. A Pixel Stereo application, built on [pxs](https://github.com/PixelStereo/pxs).

- **Devices** (left): found by mDNS, or added by hand (`host:port`). Unfold one to read its namespace.
- **Page** (right): the same controls page as NyxMap's remote, served to the network with no code. Drag an address onto it, double-click it, or right-click for the widget (fader, encoder, number, switch, buttons…). Two numbers make an XY pad, or two controls.
- A control is a view of the remote parameter: it follows the device and writes back over OSC. The address is shown on its tile.
- **Edit mode:** Cmd/Ctrl+E. **File:** Open, Save, Save As (`.kadran`).

## Build

Qt 6.4 or later with Qt WebEngine, C++17.

```sh
git clone --recursive https://github.com/PixelStereo/Kadran.git
cd Kadran
cmake -S . -B build && cmake --build build
ctest --test-dir build
open build/Kadran.app
```

## Licence

GNU Affero General Public License v3.0 (see `LICENSE`).
