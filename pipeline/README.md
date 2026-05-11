# Pipeline Presentation App

Standalone Vite + React Flow app for the rendering-architecture presentation.

## Goal

This is intentionally presentation-first rather than code-accurate. The graph is designed to:

- read well on bright projectors
- support zoomed screenshots for PowerPoint slides
- tell a clear hybrid-rendering story
- keep texture outputs visually separate, even when the engine packs them internally

## Run

```bash
npm install
npm run dev
```

## Files to edit

- `src/pipelineData.jsx`: nodes, edges, labels, and focus groups
- `src/index.css`: visual theme and node styling
- `src/App.jsx`: sidebar copy and view controls

## Swapping placeholder textures

Each texture card in `src/pipelineData.jsx` accepts:

```js
{
  label: 'Albedo',
  shortLabel: 'Alb',
  style: 'albedo',
  image: '/your-image.png'
}
```

If `image` is omitted, a styled placeholder swatch is shown instead.
