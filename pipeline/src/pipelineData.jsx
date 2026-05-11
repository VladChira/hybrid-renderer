import { BaseEdge, Handle, MarkerType, Position, getBezierPath } from '@xyflow/react';

function PassNode({ data, selected }) {
  const outputs = data.outputHandles ?? [];
  const outputCount = Math.max(outputs.length, 1);

  return (
    <div className={`pass-node kind-${data.kind}${selected ? ' is-selected' : ''}`}>
      <Handle className="pass-handle" type="target" position={Position.Left} />
      {outputs.map((handleId, index) => (
        <Handle
          key={handleId}
          id={handleId}
          className="pass-handle"
          type="source"
          position={Position.Right}
          style={{ top: `${((index + 1) / (outputCount + 1)) * 100}%` }}
        />
      ))}
      {outputs.length === 0 ? (
        <Handle className="pass-handle" type="source" position={Position.Right} />
      ) : null}
      <div className="pass-node-topbar" />
      <div className="pass-node-body">
        <h3>{data.title}</h3>
        <p>{data.description}</p>
        <ul>
          {data.bullets.map((bullet) => (
            <li key={bullet}>{bullet}</li>
          ))}
        </ul>
      </div>
    </div>
  );
}

function StubNode({ data }) {
  return (
    <div className="stub-node">
      <Handle className="stub-handle" type="source" position={Position.Right} />
      <div className="stub-label">{data.label}</div>
    </div>
  );
}

function BundleNode({ data, selected }) {
  const handles = data.inputHandles ?? [];
  const count = Math.max(handles.length, 1);

  return (
    <div className={`bundle-node${selected ? ' is-selected' : ''}`}>
      {handles.map((handleId, index) => (
        <Handle
          key={handleId}
          id={handleId}
          className="bundle-handle"
          type="target"
          position={Position.Left}
          style={{ top: `${((index + 1) / (count + 1)) * 100}%` }}
        />
      ))}
      <div className="bundle-node-topbar" />
      <div className="bundle-node-body">
        <div className="bundle-title">{data.title}</div>
        <div className="bundle-subtitle">{data.subtitle}</div>
      </div>
      <Handle className="bundle-handle" type="source" position={Position.Right} />
    </div>
  );
}

function PreviewNode({ data, selected }) {
  return (
    <div className={`preview-node${selected ? ' is-selected' : ''}`}>
      <Handle className="preview-handle" type="target" position={Position.Left} />
      <Handle className="preview-handle" type="source" position={Position.Right} />
      <div className={`preview-visual style-${data.style}`}>
        {data.image ? <img alt={data.label} src={data.image} /> : <span>{data.shortLabel}</span>}
      </div>
      <div className="preview-label">{data.label}</div>
    </div>
  );
}

function StoryEdge(props) {
  const [edgePath] = getBezierPath({
    sourceX: props.sourceX,
    sourceY: props.sourceY,
    sourcePosition: props.sourcePosition,
    targetX: props.targetX,
    targetY: props.targetY,
    targetPosition: props.targetPosition,
    curvature: props.data?.curvature ?? 0.26,
  });

  const color = props.data?.color ?? '#9aa7b8';

  return (
    <>
      <BaseEdge
        path={edgePath}
        markerEnd={props.data?.roundTip ? undefined : props.markerEnd}
        style={{
          stroke: color,
          strokeWidth: props.data?.width ?? 3,
          opacity: props.selected ? 1 : 0.92,
        }}
      />
      {props.data?.roundTip ? (
        <circle
          cx={props.targetX}
          cy={props.targetY}
          r={5.5}
          fill={color}
          stroke="rgba(255,255,255,0.95)"
          strokeWidth={2}
        />
      ) : null}
    </>
  );
}

export const nodeTypes = {
  pass: PassNode,
  stub: StubNode,
  bundle: BundleNode,
  preview: PreviewNode,
};

export const edgeTypes = {
  story: StoryEdge,
};

const warm = '#d97706';
const blue = '#2563eb';
const teal = '#0f766e';
const slate = '#94a3b8';
const violet = '#7c3aed';

const marker = (color) => ({
  type: MarkerType.ArrowClosed,
  color,
  width: 22,
  height: 22,
});

export const initialNodes = [
  {
    id: 'stub-scene',
    type: 'stub',
    position: { x: 88, y: 188 },
    data: { label: 'Scene' },
  },
  {
    id: 'stub-materials',
    type: 'stub',
    position: { x: 88, y: 280 },
    data: { label: 'Geometry + materials' },
  },
  {
    id: 'gbuffer-pass',
    type: 'pass',
    position: { x: 255, y: 152 },
    data: {
      kind: 'raster',
      title: 'G-Buffer',
      description:
        'Rasterization captures the visible surfaces of the scene and the material attributes needed later for lighting.',
      bullets: ['primary visibility', 'material texture fetch', 'surface attributes'],
      outputHandles: ['out-albedo', 'out-metalness', 'out-normal', 'out-roughness', 'out-depth'],
    },
  },
  {
    id: 'preview-albedo',
    type: 'preview',
    position: { x: 560, y: 118 },
    data: { label: 'Albedo', shortLabel: 'Alb', style: 'albedo' },
  },
  {
    id: 'preview-metalness',
    type: 'preview',
    position: { x: 580, y: 196 },
    data: { label: 'Metalness', shortLabel: 'Met', style: 'metallic' },
  },
  {
    id: 'preview-normal',
    type: 'preview',
    position: { x: 600, y: 276 },
    data: { label: 'Normal', shortLabel: 'Nrm', style: 'normal' },
  },
  {
    id: 'preview-roughness',
    type: 'preview',
    position: { x: 580, y: 356 },
    data: { label: 'Roughness', shortLabel: 'Rgh', style: 'roughness' },
  },
  {
    id: 'preview-depth',
    type: 'preview',
    position: { x: 552, y: 434 },
    data: { label: 'Depth', shortLabel: 'Z', style: 'depth' },
  },
  {
    id: 'gbuffer-bundle',
    type: 'bundle',
    position: { x: 840, y: 242 },
    data: {
      title: 'G-buffer',
      subtitle: 'Packed surface data',
      inputHandles: ['albedo', 'metalness', 'normal', 'roughness', 'depth'],
    },
  },
  {
    id: 'stub-hdri',
    type: 'stub',
    position: { x: 88, y: 652 },
    data: { label: 'HDR image' },
  },
  {
    id: 'hdri-pass',
    type: 'pass',
    position: { x: 255, y: 588 },
    data: {
      kind: 'raster',
      title: 'HDRI Precompute',
      description:
        'The environment map is converted into lighting data that can be sampled efficiently during shading.',
      bullets: ['cubemap conversion', 'irradiance', 'specular prefilter + BRDF LUT'],
      outputHandles: ['out-sky', 'out-irradiance', 'out-prefilter', 'out-lut'],
    },
  },
  {
    id: 'preview-sky',
    type: 'preview',
    position: { x: 560, y: 572 },
    data: { label: 'Skybox Cubemap', shortLabel: 'Sky', style: 'final' },
  },
  {
    id: 'preview-irradiance',
    type: 'preview',
    position: { x: 586, y: 650 },
    data: { label: 'Irradiance', shortLabel: 'Irr', style: 'heat' },
  },
  {
    id: 'preview-prefilter',
    type: 'preview',
    position: { x: 570, y: 728 },
    data: { label: 'Prefilter', shortLabel: 'Pre', style: 'metallic' },
  },
  {
    id: 'preview-lut',
    type: 'preview',
    position: { x: 548, y: 806 },
    data: { label: 'BRDF LUT', shortLabel: 'LUT', style: 'roughness' },
  },
  {
    id: 'hdri-bundle',
    type: 'bundle',
    position: { x: 840, y: 682 },
    data: {
      title: 'IBL data',
      subtitle: 'Precomputed environment maps',
      inputHandles: ['sky', 'irradiance', 'prefilter', 'lut'],
    },
  },
  {
    id: 'stub-bvh-shadow',
    type: 'stub',
    position: { x: 1030, y: 152 },
    data: { label: 'BVH' },
  },
  {
    id: 'stub-lights-shadow',
    type: 'stub',
    position: { x: 998, y: 250 },
    data: { label: 'Lights' },
  },
  {
    id: 'shadow-pass',
    type: 'pass',
    position: { x: 1180, y: 132 },
    data: {
      kind: 'compute',
      title: 'Shadow Rays',
      description:
        'Low-sample visibility rays are traced through the BVH to estimate soft shadows at interactive cost.',
      bullets: ['screen-space dispatch', 'stochastic visibility', 'BVH traversal'],
      outputHandles: ['out-area', 'out-directional', 'out-env'],
    },
  },
  {
    id: 'preview-area-shadow',
    type: 'preview',
    position: { x: 1508, y: 120 },
    data: { label: 'Area Light Mask', shortLabel: 'A1', style: 'shadow' },
  },
  {
    id: 'preview-directional-shadow',
    type: 'preview',
    position: { x: 1534, y: 204 },
    data: { label: 'Directional Mask', shortLabel: 'D1', style: 'shadow' },
  },
  {
    id: 'preview-env-shadow',
    type: 'preview',
    position: { x: 1512, y: 286 },
    data: { label: 'HDRI Visibility', shortLabel: 'Env', style: 'heat' },
  },
  {
    id: 'shadow-bundle',
    type: 'bundle',
    position: { x: 1808, y: 196 },
    data: {
      title: 'Shadow buffers',
      subtitle: 'Noisy raw visibility',
      inputHandles: ['area-mask', 'directional-mask', 'env-mask'],
    },
  },
  {
    id: 'stub-history',
    type: 'stub',
    position: { x: 1008, y: 518 },
    data: { label: 'Previous frame' },
  },
  {
    id: 'denoise-pass',
    type: 'pass',
    position: { x: 1180, y: 458 },
    data: {
      kind: 'compute',
      title: 'Shadow Denoise',
      description:
        'Temporal accumulation reuses stable history while A-Trous filtering removes the remaining Monte Carlo noise.',
      bullets: ['history reprojection', 'disocclusion rejection', 'edge-aware filtering'],
      outputHandles: ['out-soft-shadow', 'out-history'],
    },
  },
  {
    id: 'preview-soft-shadow',
    type: 'preview',
    position: { x: 1516, y: 472 },
    data: { label: 'Soft Shadow Mask', shortLabel: 'S', style: 'denoised' },
  },
  {
    id: 'preview-history',
    type: 'preview',
    position: { x: 1526, y: 560 },
    data: { label: 'Temporal History', shortLabel: 'H', style: 'history' },
  },
  {
    id: 'denoise-bundle',
    type: 'bundle',
    position: { x: 1808, y: 524 },
    data: {
      title: 'Resolved shadows',
      subtitle: 'Stable visibility for shading',
      inputHandles: ['soft-shadow', 'history-out'],
    },
  },
  {
    id: 'stub-bvh-reflections',
    type: 'stub',
    position: { x: 1030, y: 906 },
    data: { label: 'BVH' },
  },
  {
    id: 'reflection-pass',
    type: 'pass',
    position: { x: 1180, y: 844 },
    data: {
      kind: 'compute',
      title: 'Reflection Rays',
      description:
        'Secondary rays add local specular reflections on top of rasterized primary shading for a more physically based result.',
      bullets: ['secondary ray hits', 'roughness-aware sampling', 'hybrid local reflections'],
      outputHandles: ['out-reflection-color', 'out-hit-distance'],
    },
  },
  {
    id: 'preview-reflection-color',
    type: 'preview',
    position: { x: 1520, y: 856 },
    data: { label: 'Reflection Color', shortLabel: 'Refl', style: 'final' },
  },
  {
    id: 'preview-hit-distance',
    type: 'preview',
    position: { x: 1526, y: 944 },
    data: { label: 'Hit Distance', shortLabel: 'Hit', style: 'depth' },
  },
  {
    id: 'reflection-bundle',
    type: 'bundle',
    position: { x: 1808, y: 906 },
    data: {
      title: 'Reflection buffers',
      subtitle: 'Specular contribution',
      inputHandles: ['reflection-color', 'hit-distance'],
    },
  },
  {
    id: 'deferred-pass',
    type: 'pass',
    position: { x: 2140, y: 466 },
    data: {
      kind: 'raster',
      title: 'Final Lighting + Composition',
      description:
        'The fullscreen shading pass combines the G-buffer, shadows, reflections, and image-based lighting into the main frame.',
      bullets: ['PBR lighting', 'IBL + direct lights', 'final composition'],
      outputHandles: ['out-final'],
    },
  },
  {
    id: 'preview-final-frame',
    type: 'preview',
    position: { x: 2478, y: 504 },
    data: { label: 'Lit Scene', shortLabel: 'RGB', style: 'final' },
  },
  {
    id: 'final-bundle',
    type: 'bundle',
    position: { x: 2786, y: 526 },
    data: {
      title: 'Final frame',
      subtitle: 'Viewport output',
      inputHandles: ['lit-frame'],
    },
  },
];

export const initialEdges = [
  {
    id: 'stub-scene-to-gbuffer',
    source: 'stub-scene',
    target: 'gbuffer-pass',
    type: 'story',
    data: { color: slate, width: 2.4, roundTip: true, curvature: 0.18 },
  },
  {
    id: 'stub-materials-to-gbuffer',
    source: 'stub-materials',
    target: 'gbuffer-pass',
    type: 'story',
    data: { color: slate, width: 2.4, roundTip: true, curvature: 0.12 },
  },
  {
    id: 'gbuffer-pass-to-albedo',
    source: 'gbuffer-pass',
    sourceHandle: 'out-albedo',
    target: 'preview-albedo',
    type: 'story',
    data: { color: warm, width: 3.2, curvature: 0.14 },
    markerEnd: marker(warm),
  },
  {
    id: 'gbuffer-pass-to-metalness',
    source: 'gbuffer-pass',
    sourceHandle: 'out-metalness',
    target: 'preview-metalness',
    type: 'story',
    data: { color: warm, width: 3.0, curvature: 0.12 },
    markerEnd: marker(warm),
  },
  {
    id: 'gbuffer-pass-to-normal',
    source: 'gbuffer-pass',
    sourceHandle: 'out-normal',
    target: 'preview-normal',
    type: 'story',
    data: { color: warm, width: 3.0, curvature: 0.1 },
    markerEnd: marker(warm),
  },
  {
    id: 'gbuffer-pass-to-roughness',
    source: 'gbuffer-pass',
    sourceHandle: 'out-roughness',
    target: 'preview-roughness',
    type: 'story',
    data: { color: warm, width: 3.0, curvature: 0.08 },
    markerEnd: marker(warm),
  },
  {
    id: 'gbuffer-pass-to-depth',
    source: 'gbuffer-pass',
    sourceHandle: 'out-depth',
    target: 'preview-depth',
    type: 'story',
    data: { color: warm, width: 3.0, curvature: 0.06 },
    markerEnd: marker(warm),
  },
  {
    id: 'albedo-to-bundle',
    source: 'preview-albedo',
    target: 'gbuffer-bundle',
    targetHandle: 'albedo',
    type: 'story',
    data: { color: warm, width: 2.8, curvature: 0.14 },
  },
  {
    id: 'metalness-to-bundle',
    source: 'preview-metalness',
    target: 'gbuffer-bundle',
    targetHandle: 'metalness',
    type: 'story',
    data: { color: warm, width: 2.8, curvature: 0.1 },
  },
  {
    id: 'normal-to-bundle',
    source: 'preview-normal',
    target: 'gbuffer-bundle',
    targetHandle: 'normal',
    type: 'story',
    data: { color: warm, width: 2.8, curvature: 0.08 },
  },
  {
    id: 'roughness-to-bundle',
    source: 'preview-roughness',
    target: 'gbuffer-bundle',
    targetHandle: 'roughness',
    type: 'story',
    data: { color: warm, width: 2.8, curvature: 0.06 },
  },
  {
    id: 'depth-to-bundle',
    source: 'preview-depth',
    target: 'gbuffer-bundle',
    targetHandle: 'depth',
    type: 'story',
    data: { color: warm, width: 2.8, curvature: 0.04 },
  },
  {
    id: 'stub-hdri-to-pass',
    source: 'stub-hdri',
    target: 'hdri-pass',
    type: 'story',
    data: { color: slate, width: 2.4, roundTip: true, curvature: 0.18 },
  },
  {
    id: 'hdri-pass-to-sky',
    source: 'hdri-pass',
    sourceHandle: 'out-sky',
    target: 'preview-sky',
    type: 'story',
    data: { color: blue, width: 3.0, curvature: 0.14 },
    markerEnd: marker(blue),
  },
  {
    id: 'hdri-pass-to-irradiance',
    source: 'hdri-pass',
    sourceHandle: 'out-irradiance',
    target: 'preview-irradiance',
    type: 'story',
    data: { color: blue, width: 3.0, curvature: 0.1 },
    markerEnd: marker(blue),
  },
  {
    id: 'hdri-pass-to-prefilter',
    source: 'hdri-pass',
    sourceHandle: 'out-prefilter',
    target: 'preview-prefilter',
    type: 'story',
    data: { color: blue, width: 3.0, curvature: 0.08 },
    markerEnd: marker(blue),
  },
  {
    id: 'hdri-pass-to-lut',
    source: 'hdri-pass',
    sourceHandle: 'out-lut',
    target: 'preview-lut',
    type: 'story',
    data: { color: blue, width: 3.0, curvature: 0.06 },
    markerEnd: marker(blue),
  },
  {
    id: 'sky-to-bundle',
    source: 'preview-sky',
    target: 'hdri-bundle',
    targetHandle: 'sky',
    type: 'story',
    data: { color: blue, width: 2.8, curvature: 0.14 },
  },
  {
    id: 'irradiance-to-bundle',
    source: 'preview-irradiance',
    target: 'hdri-bundle',
    targetHandle: 'irradiance',
    type: 'story',
    data: { color: blue, width: 2.8, curvature: 0.1 },
  },
  {
    id: 'prefilter-to-bundle',
    source: 'preview-prefilter',
    target: 'hdri-bundle',
    targetHandle: 'prefilter',
    type: 'story',
    data: { color: blue, width: 2.8, curvature: 0.08 },
  },
  {
    id: 'lut-to-bundle',
    source: 'preview-lut',
    target: 'hdri-bundle',
    targetHandle: 'lut',
    type: 'story',
    data: { color: blue, width: 2.8, curvature: 0.06 },
  },
  {
    id: 'stub-bvh-to-shadow',
    source: 'stub-bvh-shadow',
    target: 'shadow-pass',
    type: 'story',
    data: { color: slate, width: 2.4, roundTip: true, curvature: 0.14 },
  },
  {
    id: 'stub-lights-to-shadow',
    source: 'stub-lights-shadow',
    target: 'shadow-pass',
    type: 'story',
    data: { color: slate, width: 2.4, roundTip: true, curvature: 0.1 },
  },
  {
    id: 'gbuffer-to-shadow',
    source: 'gbuffer-bundle',
    target: 'shadow-pass',
    type: 'story',
    data: { color: warm, width: 3.2, curvature: 0.12 },
    markerEnd: marker(warm),
  },
  {
    id: 'shadow-pass-to-area',
    source: 'shadow-pass',
    sourceHandle: 'out-area',
    target: 'preview-area-shadow',
    type: 'story',
    data: { color: teal, width: 3.0, curvature: 0.12 },
    markerEnd: marker(teal),
  },
  {
    id: 'shadow-pass-to-directional',
    source: 'shadow-pass',
    sourceHandle: 'out-directional',
    target: 'preview-directional-shadow',
    type: 'story',
    data: { color: teal, width: 3.0, curvature: 0.1 },
    markerEnd: marker(teal),
  },
  {
    id: 'shadow-pass-to-env',
    source: 'shadow-pass',
    sourceHandle: 'out-env',
    target: 'preview-env-shadow',
    type: 'story',
    data: { color: teal, width: 3.0, curvature: 0.08 },
    markerEnd: marker(teal),
  },
  {
    id: 'area-to-bundle',
    source: 'preview-area-shadow',
    target: 'shadow-bundle',
    targetHandle: 'area-mask',
    type: 'story',
    data: { color: teal, width: 2.8, curvature: 0.12 },
  },
  {
    id: 'directional-to-bundle',
    source: 'preview-directional-shadow',
    target: 'shadow-bundle',
    targetHandle: 'directional-mask',
    type: 'story',
    data: { color: teal, width: 2.8, curvature: 0.1 },
  },
  {
    id: 'env-to-bundle',
    source: 'preview-env-shadow',
    target: 'shadow-bundle',
    targetHandle: 'env-mask',
    type: 'story',
    data: { color: teal, width: 2.8, curvature: 0.08 },
  },
  {
    id: 'shadow-to-denoise',
    source: 'shadow-bundle',
    target: 'denoise-pass',
    type: 'story',
    data: { color: violet, width: 3.5, curvature: 0.16 },
    markerEnd: marker(violet),
  },
  {
    id: 'gbuffer-to-denoise',
    source: 'gbuffer-bundle',
    target: 'denoise-pass',
    type: 'story',
    data: { color: warm, width: 3.0, curvature: 0.08 },
    markerEnd: marker(warm),
  },
  {
    id: 'stub-history-to-denoise',
    source: 'stub-history',
    target: 'denoise-pass',
    type: 'story',
    data: { color: slate, width: 2.4, roundTip: true, curvature: 0.16 },
  },
  {
    id: 'denoise-pass-to-soft',
    source: 'denoise-pass',
    sourceHandle: 'out-soft-shadow',
    target: 'preview-soft-shadow',
    type: 'story',
    data: { color: violet, width: 3.0, curvature: 0.12 },
    markerEnd: marker(violet),
  },
  {
    id: 'denoise-pass-to-history',
    source: 'denoise-pass',
    sourceHandle: 'out-history',
    target: 'preview-history',
    type: 'story',
    data: { color: violet, width: 3.0, curvature: 0.1 },
    markerEnd: marker(violet),
  },
  {
    id: 'soft-to-bundle',
    source: 'preview-soft-shadow',
    target: 'denoise-bundle',
    targetHandle: 'soft-shadow',
    type: 'story',
    data: { color: violet, width: 2.8, curvature: 0.12 },
  },
  {
    id: 'history-to-bundle',
    source: 'preview-history',
    target: 'denoise-bundle',
    targetHandle: 'history-out',
    type: 'story',
    data: { color: violet, width: 2.8, curvature: 0.1 },
  },
  {
    id: 'stub-bvh-to-reflections',
    source: 'stub-bvh-reflections',
    target: 'reflection-pass',
    type: 'story',
    data: { color: slate, width: 2.4, roundTip: true, curvature: 0.16 },
  },
  {
    id: 'gbuffer-to-reflections',
    source: 'gbuffer-bundle',
    target: 'reflection-pass',
    type: 'story',
    data: { color: warm, width: 3.0, curvature: 0.04 },
    markerEnd: marker(warm),
  },
  {
    id: 'hdri-to-reflections',
    source: 'hdri-bundle',
    target: 'reflection-pass',
    type: 'story',
    data: { color: blue, width: 3.0, curvature: -0.02 },
    markerEnd: marker(blue),
  },
  {
    id: 'reflection-pass-to-color',
    source: 'reflection-pass',
    sourceHandle: 'out-reflection-color',
    target: 'preview-reflection-color',
    type: 'story',
    data: { color: teal, width: 3.0, curvature: 0.12 },
    markerEnd: marker(teal),
  },
  {
    id: 'reflection-pass-to-hit',
    source: 'reflection-pass',
    sourceHandle: 'out-hit-distance',
    target: 'preview-hit-distance',
    type: 'story',
    data: { color: teal, width: 3.0, curvature: 0.1 },
    markerEnd: marker(teal),
  },
  {
    id: 'reflection-color-to-bundle',
    source: 'preview-reflection-color',
    target: 'reflection-bundle',
    targetHandle: 'reflection-color',
    type: 'story',
    data: { color: teal, width: 2.8, curvature: 0.12 },
  },
  {
    id: 'reflection-hit-to-bundle',
    source: 'preview-hit-distance',
    target: 'reflection-bundle',
    targetHandle: 'hit-distance',
    type: 'story',
    data: { color: teal, width: 2.8, curvature: 0.1 },
  },
  {
    id: 'gbuffer-to-deferred',
    source: 'gbuffer-bundle',
    target: 'deferred-pass',
    type: 'story',
    data: { color: warm, width: 3.2, curvature: 0.14 },
    markerEnd: marker(warm),
  },
  {
    id: 'hdri-to-deferred',
    source: 'hdri-bundle',
    target: 'deferred-pass',
    type: 'story',
    data: { color: blue, width: 3.2, curvature: 0.06 },
    markerEnd: marker(blue),
  },
  {
    id: 'denoise-to-deferred',
    source: 'denoise-bundle',
    target: 'deferred-pass',
    type: 'story',
    data: { color: violet, width: 3.4, curvature: 0.12 },
    markerEnd: marker(violet),
  },
  {
    id: 'reflection-to-deferred',
    source: 'reflection-bundle',
    target: 'deferred-pass',
    type: 'story',
    data: { color: teal, width: 3.2, curvature: -0.04 },
    markerEnd: marker(teal),
  },
  {
    id: 'deferred-pass-to-final',
    source: 'deferred-pass',
    sourceHandle: 'out-final',
    target: 'preview-final-frame',
    type: 'story',
    data: { color: warm, width: 4.0, curvature: 0.14 },
    markerEnd: marker(warm),
  },
  {
    id: 'final-to-bundle',
    source: 'preview-final-frame',
    target: 'final-bundle',
    targetHandle: 'lit-frame',
    type: 'story',
    data: { color: warm, width: 2.9, curvature: 0.12 },
  },
];
