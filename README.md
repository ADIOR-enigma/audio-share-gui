# Audio Share GUI
[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or--later-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.html)
[![GitHub stars](https://img.shields.io/github/stars/ADIOR-enigma/audio-share-gui)](https://github.com/ADIOR-enigma/audio-share-gui/stargazers)
[![Release](https://img.shields.io/github/v/release/ADIOR-enigma/audio-share-gui)](https://github.com/ADIOR-enigma/audio-share-gui/releases)


**Audio Share GUI** is a portable desktop application for Linux that lets you share your system’s audio over the network with a clean GTK interface and system tray controls.

This project builds on the work originally done by **mkckr0** in the [audio-share](https://github.com/mkckr0/audio-share) repository and extends it with a full GUI frontend, AppImage packaging, and usability improvements.

<img width="383" height="486" alt="image" src="https://github.com/user-attachments/assets/7ad167a7-9965-4689-9451-4fb4eda0a567" />
---

## 📦 Features

- 🎛️ **GTK3-based graphical interface**
- 🔊 **Tray icon support** for quick access
- 🖧 **Network audio sharing** using the underlying audio-share core
- 🧠 **Dynamic listing of interfaces, encodings, and endpoints**
- 📦 Distributed as a **standalone AppImage**

---

## 🧠 Core Project (by mkckr0)

This repository depends on the excellent work in the [audio-share](https://github.com/mkckr0/audio-share) project by **mkckr0**. That project provides the core audio serving CLI tool (`as-cmd`) and the underlying networking and audio logic.

please check out:
👉 https://github.com/mkckr0/audio-share

Huge thanks to **mkckr0** for building the foundation of this tool.

---

## 🚀 AppImage Releases

Prebuilt AppImages are available in the releases section:

🔗 https://github.com/ADIOR-enigma/audio-share-gui/releases

---

To run the AppImage:
---

## ▶️ GearLever (Recommended)

The easiest and cleanest way to run **Audio Share GUI** is by using a modern AppImage manager for Linux.

🔗 https://github.com/mijorus/gearlever

### Why Gear Lever?
- Automatically makes AppImages executable
- Integrates AppImages into your application menu
- Handles updates and metadata cleanly
- No terminal commands required

### Steps
1. Install **Gear Lever** (Flathub recommended)
2. Download the latest `AudioShareGUI-*.AppImage` from the Releases page
3. Open Gear Lever
4. Drag and drop the AppImage into the Gear Lever window
5. Launch **Audio Share GUI** like a normal desktop application

---

## ▶️ Manual Method (Alternative)

If you prefer using the terminal:

```bash
chmod +x AudioShareGUI-*.AppImage
./AudioShareGUI-*.AppImage
```

---

## ℹ️ Notes

The AppImage is fully self-contained

Required tray libraries (Ayatana / dbusmenu) are bundled

Works even on systems where those dependencies are not installed

Tested on Linux Mint / Ubuntu-based distributions

---

## 📜 License

This project is licensed under GPL-3.0-or-later.

The underlying core project (audio-share) by mkckr0 is licensed separately.
Please refer to its repository for details.

---

## ❤️ Acknowledgements

mkckr0 — original author of the core audio-share project
https://github.com/mkckr0/audio-share

Mijorus — creator of Gear Lever
https://github.com/mijorus/gearlever

The Linux desktop and AppImage community

---
