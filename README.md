# SolEngine

> *"The Solitaire Engine. Bake your new Sol into existence with the power of Python-based scripting in Eatscript."*

[![GitHub Pages](https://img.shields.io/badge/GitHub%20Pages-Live%20Demo-brightgreen?style=flat-square&logo=github)](https://maddestlabs.github.io/solengine/)
[![Repository](https://img.shields.io/badge/Repository-maddestlabs%2Fsolengine-blue?style=flat-square&logo=github)](https://github.com/maddestlabs/solengine)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=flat-square)](file:///c:/git/solengine/LICENSE.md)
[![Standard: C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B)](file:///c:/git/solengine/CMakeLists.txt)
[![Graphics: WebGPU](https://img.shields.io/badge/Graphics-WebGPU%20%2F%20WGSL-red?style=flat-square)](file:///c:/git/solengine/src/gfx)
[![Audio: miniaudio](https://img.shields.io/badge/Audio-miniaudio-orange?style=flat-square)](file:///c:/git/solengine/src/audio)

---

## The Crucible

**SolEngine** is an ode to solitaire classics like *Hardwood Solitaire III* and the boundless monolith of *PySolFC*, resurrected through modern **WebGPU** compute and fragment sorcery.

Every shuffle is deterministic, powered by an editable seed. Easily replay games via seeds. Built-in solver can ensure solvable games (depending on variant) and allows walkthrough of gameplay to facilitate learning new dynamics.

Play instantly in your browser:  
👉 **[Launch SolEngine](https://maddestlabs.github.io/solengine/)**

---

## The Core

### The Scripted Pantheon
SolEngine is built around **Eatscript**: a declarative, Pythonic scripting dialect where game logic, layout topology, and card movement are define:
- **Classic Solitaries:** *Klondike (Draw 1 & 3)*, *FreeCell*, *Spider (1, 2, & 4 Suits)*, *Yukon*.
- **Custom Variations:** Craft your own game rules via `.eats` scripts. Define tableaux, cell constraints, foundation ranks, reserve cascades, and victory conditions.

### Procedural API
Table backgrounds in SolEngine are forged in WebGPU fragment shaders while card faces and card backs can be designed with bitmaps, vector API, rendered via shader code or any combination of these.

### Classic Hardwood Ergonomics
- **One-Tap / Right-Click Smart Moves:** Hover or select any card or substack and single-click to immediately route it along its mathematically optimal trajectory to Foundations or Tableau columns.
- **Unreal World Physics:** Dragged and airborne cards fly with physical weight, damping, and spring restitution.
- **Elevation Shadow Casters:** Cards in flight elevate dynamically above the board, casting soft, spring-damped drop shadows that react to card velocity and height.

### Procedural Foley Audio
Embued with the power of `miniaudio`, sound is synthesized dynamically and can likewise be designed via Eatscript.

---

## Building

### 1. Desktop (Windows Native)

Compile and launch the native desktop application using PowerShell:

```powershell
# Build and immediately launch interactive desktop client
.\build.ps1

# Compile only without launching (safe for CI / agent environments)
.\build.ps1 -NoRun -NoLaunch

# Generate a headless snapshot preview of the table
.\build.ps1 -RenderPreview
```

### 2. Web (WebAssembly & WebGPU)

Compile into WebAssembly using Emscripten and host locally:

```powershell
# Compile WASM bundle and start local development server with COOP/COEP headers
.\build-web.ps1

# Compile only without launching the local web server
.\build-web.ps1 -NoServe

# Serve on a custom port
.\build-web.ps1 -Port 9000
```

---

## Table Controls & Keybindings

| Action | Control |
| :--- | :--- |
| **Pick & Drag** | **Left Click + Drag** card or face-up substack |
| **Smart Move** | **Right Click**, **Single Tap**, or **`Space`** to auto-route card |
| **Deal Stock** | **Left Click** on the stock deck |
| **Undo Move** | **`Z`** or **`Ctrl + Z`** (unlimited zero-allocation history) |
| **Redo Move** | **`Y`** or **`Ctrl + Y`** |
| **Nether Sight (Hint)** | **`H`** (illuminates optimal move) |
| **Auto-Foundation** | **`A`** (send all safe candidates home) |
| **New Seeded Deal** | **`N`** (shuffles a fresh deterministic layout) |

---

## License

SolEngine is published under a **Split-License Model**:

* **Engine & Source Code:** Free Software released under the [GNU General Public License v3.0 (GPLv3)](file:///c:/git/solengine/LICENSE.md).
* **Art, Vector Decks, Foley Synthesis & Lore:** **All Rights Reserved** (or authorized non-commercial use only) to prevent unauthorized commercial asset-flipping and commercial cloning.
* **Trademarks:** *SolEngine*, is a proprietary trademarks of Maddest Labs.
