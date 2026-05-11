import { useState } from 'react';
import {
  Background,
  ReactFlow,
  useEdgesState,
  useNodesState,
} from '@xyflow/react';

import '@xyflow/react/dist/style.css';

import {
  edgeTypes,
  initialEdges,
  initialNodes,
  nodeTypes,
} from './pipelineData';

function AppCanvas() {
  const [nodes, , onNodesChange] = useNodesState(initialNodes);
  const [edges, , onEdgesChange] = useEdgesState(initialEdges);
  const [, setSelectedNodeId] = useState(null);

  return (
    <div className="flow-frame">
      <ReactFlow
        nodes={nodes}
        edges={edges}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onNodeClick={(_, node) => setSelectedNodeId(node.id)}
        onPaneClick={() => setSelectedNodeId(null)}
        nodeTypes={nodeTypes}
        edgeTypes={edgeTypes}
        minZoom={0.2}
        maxZoom={1.6}
        fitView
        fitViewOptions={{ padding: 0.18, maxZoom: 1 }}
        nodesDraggable
        nodesConnectable={false}
        elementsSelectable
        panOnScroll
        selectionOnDrag={false}
        proOptions={{ hideAttribution: true }}
      >
        <Background
          color="#ddd2c3"
          gap={28}
          size={1.2}
          variant="dots"
        />
      </ReactFlow>
    </div>
  );
}

export default function App() {
  return (
    <main className="graph-only-shell">
      <AppCanvas />
    </main>
  );
}
