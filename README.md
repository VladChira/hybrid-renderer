# Hybrid Rendering Engine

## Introduction

## Getting started

Clone this repo:
- `git clone --recurse-submodules https://github.com/VladChira/hybrid-renderer.git`

### Build (Linux)

Install GCC, plus Ninja and vcpkg, then:
- `export VCPKG_ROOT=/path/to/vcpkg`
- `cmake --preset linux-ninja`
- `cmake --build --preset linux-ninja`

### Build (Windows)

Install:
- Visual Studio 2022 (or Build Tools) with “Desktop development with C++” (MSVC + Windows SDK)
- Ninja
- vcpkg + `VCPKG_ROOT` set to your vcpkg ports repo

Use the “x64 Native Tools Command Prompt for VS 2022” (or otherwise ensure the x64 MSVC environment is active), then:
- `set VCPKG_ROOT=C:\\path\\to\\vcpkg`
- `cmake --preset win-ninja`
- `cmake --build --preset win-ninja`

## Progress images

<p align="center"><img src="images/snapshot-7.png" width="700"/></p>
<p align="center">Diffuse HDRI</p>

<p align="center"><img src="images/snapshot-6.png" width="700"/></p>
<p align="center">Metallic, roughness and normal mapping</p>

<p align="center"><img src="images/snapshot-5.png" width="700"/></p>
<p align="center">Deferred shading, UI updates</p>

<p align="center"><img src="images/snapshot-4.png" width="700"/></p>
<p align="center">G-Buffer Pass (albedo on screen), material updating</p>

<p align="center"><img src="images/snapshot-3.png" width="700"/></p>
<p align="center">Simple visualisation, materials panel, cameras</p>

<p align="center"><img src="images/snapshot-2.png" width="700"/></p>
<p align="center">Scene loading and hierarchy</p>

<p align="center"><img src="images/snapshot-1.png" width="700"/></p>
<p align="center">UI Scaffold</p>