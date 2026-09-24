import type { WorkflowEdge, WorkflowNode } from "../../api/types";

type Group = "success" | "failure";
type EdgeKind = "sequence" | "candidate" | "failure";

function group(edge: WorkflowEdge): Group {
  return edge.data?.kind === "failure" || edge.sourceHandle === "failure" ? "failure" : "success";
}

function ordered(edges: WorkflowEdge[], source: string, outcome: Group): WorkflowEdge[] {
  return edges.filter((edge) => edge.source === source && group(edge) === outcome)
    .map((edge, index) => ({ edge, index }))
    .sort((a, b) => (a.edge.data?.order ?? a.index) - (b.edge.data?.order ?? b.index) || a.index - b.index)
    .map(({ edge }) => edge);
}

function renumber(edges: WorkflowEdge[], source: string, outcome: Group, preferred?: WorkflowEdge[]): void {
  const siblings = preferred ?? ordered(edges, source, outcome);
  siblings.forEach((edge, order) => {
    const kind = edge.data?.kind ?? (outcome === "failure" ? "failure" : "sequence");
    edge.data = { ...edge.data, kind, order };
    edge.sourceHandle = outcome;
    edge.label = `${kind} · ${order}`;
  });
}

function copy(edges: WorkflowEdge[]): WorkflowEdge[] {
  return edges.map((edge) => ({ ...edge, data: edge.data ? { ...edge.data } : undefined }));
}

export function removeSelection(nodes: WorkflowNode[], edges: WorkflowEdge[], entry: string | undefined,
  nodeId: string, edgeId: string): { nodes: WorkflowNode[]; edges: WorkflowEdge[]; entry?: string } {
  const removed = edges.filter((edge) => edge.id === edgeId || edge.source === nodeId || edge.target === nodeId);
  const next = copy(edges.filter((edge) => !removed.includes(edge)));
  const affected = new Set(removed.map((edge) => `${edge.source}\0${group(edge)}`));
  for (const key of affected) {
    const [source, outcome] = key.split("\0") as [string, Group];
    renumber(next, source, outcome);
  }
  return { nodes: nodeId ? nodes.filter((node) => node.id !== nodeId) : nodes,
    edges: next, entry: nodeId && entry === nodeId ? undefined : entry };
}

export function appendEdge(edges: WorkflowEdge[], edge: WorkflowEdge): WorkflowEdge[] {
  const next = copy(edges);
  const outcome = group(edge);
  next.push({ ...edge, data: { ...edge.data, kind: outcome === "failure" ? "failure" : edge.data?.kind ?? "sequence" } });
  renumber(next, edge.source, outcome, [...ordered(next.filter((item) => item.id !== edge.id), edge.source, outcome), next.at(-1)!]);
  return next;
}

export function changeEdgeKind(edges: WorkflowEdge[], id: string, kind: EdgeKind): WorkflowEdge[] {
  const next = copy(edges);
  const edge = next.find((item) => item.id === id);
  if (!edge) return edges;
  const previous = group(edge);
  edge.data = { ...edge.data, kind };
  edge.sourceHandle = kind === "failure" ? "failure" : "success";
  const outcome = group(edge);
  if (previous !== outcome) {
    renumber(next, edge.source, previous);
    renumber(next, edge.source, outcome,
      [...ordered(next.filter((item) => item.id !== id), edge.source, outcome), edge]);
  } else renumber(next, edge.source, outcome);
  return next;
}

export function moveEdge(edges: WorkflowEdge[], id: string, requested: number): WorkflowEdge[] {
  if (!Number.isFinite(requested)) return edges;
  const next = copy(edges);
  const edge = next.find((item) => item.id === id);
  if (!edge) return edges;
  const outcome = group(edge);
  const siblings = ordered(next, edge.source, outcome).filter((item) => item.id !== id);
  siblings.splice(Math.max(0, Math.min(Math.trunc(requested), siblings.length)), 0, edge);
  renumber(next, edge.source, outcome, siblings);
  return next;
}
