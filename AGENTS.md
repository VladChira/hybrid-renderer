# AGENTS.md

## Project overview

This project is a renderer-first real-time hybrid rendering engine.

Its main goal is to combine traditional rasterization with selective ray tracing in order to produce physically based visuals that approach offline rendering quality on simple scenes, while still keeping an interactive frame rate.

The project is explicitly motivated by the fact that real-time pure ray tracing is too expensive on modest hardware, while rasterization remains efficient and broadly available. The hybrid approach exists to exploit the strengths of both.

## What the renderer is supposed to do

The first complete version of the project is expected to support:

- loading at least minimal glTF 2.0 scenes from disk
- storing a usable in-memory scene representation for both rasterization and ray tracing
- a deferred geometry pass producing a G-buffer
- a debug-friendly UI for inspecting scene data and render passes
- a modern physically based shading baseline
- direct lighting computed efficiently in rasterization
- ray-traced soft shadows at very low ray counts
- ray-traced reflections
- pass composition and post-processing
- qualitative comparison against offline references

Diffuse global illumination is part of the intended direction, but it is also one of the most uncertain and expensive pieces. Treat it as important, but not at the expense of the rest of the system.