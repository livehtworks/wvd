export type Scalar = boolean | number | string;
export interface ParameterBinding { node: string; path: string }
export interface PublicParameter {
  name: string; label?: string; type: 'boolean' | 'integer' | 'number' | 'string' | 'resource';
  default: Scalar; min?: number; max?: number; max_length?: number; choices?: Scalar[];
  bindings: ParameterBinding[];
}
export interface PublicInterface { kind?: 'step' | 'block' | 'task'; category?: string; parameters?: PublicParameter[] }
export interface FlowCall {
  flow_id: string; arguments?: Record<string, Scalar>;
  extensions?: Record<string, FlowCall[]>; expected_revision?: string;
}
export interface PublicFlowSummary { id: string; name: string; revision?: string; interface?: PublicInterface; slots?: string[] }
export interface GraphNode {
  id: string; type?: string; position: { x: number; y: number };
  data: { label: string; node_type: string; parameters: Record<string, unknown> };
  repeat_limit?: number;
}
export interface GraphEdge {
  id: string; source: string; target: string; sourceHandle?: string | null; targetHandle?: string | null;
  label?: string; data?: { order?: number; kind?: 'sequence' | 'candidate' | 'failure'; [key: string]: unknown };
}
export interface FlowGraph {
  id: string; name: string; description?: string; revision?: string; entry_node_id?: string;
  nodes: GraphNode[]; edges: GraphEdge[]; interface?: PublicInterface;
  time_limit_ms?: number; resource_locale?: string; [key: string]: unknown;
}
const clone = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;
export function effectiveArguments(fields: PublicParameter[], supplied: Record<string, Scalar> = {}) {
  const result: Record<string, Scalar> = {};
  const names = new Set(fields.map(field => field.name));
  if (names.size!==fields.length) throw new Error('FLOW_PARAMETER_DUPLICATE_OR_INVALID');
  for (const key of Object.keys(supplied)) if (!names.has(key)) throw new Error(`FLOW_ARGUMENT_UNKNOWN:${key}`);
  for (const field of fields) {
    const value = Object.hasOwn(supplied, field.name) ? supplied[field.name] : field.default;
    const valid = field.type === 'boolean' ? typeof value === 'boolean'
      : ['integer','number'].includes(field.type) ? typeof value === 'number' && Number.isFinite(value) &&
        (field.type !== 'integer' || Number.isSafeInteger(value)) : typeof value === 'string';
    if (!valid) throw new Error(`FLOW_ARGUMENT_TYPE:${field.name}`);
    if (typeof value === 'number' && ((field.min !== undefined && value < field.min) || (field.max !== undefined && value > field.max)))
      throw new Error(`FLOW_ARGUMENT_RANGE:${field.name}`);
    if (field.type==='resource' && !/^[A-Za-z0-9][A-Za-z0-9_.-]*$/.test(String(value))) throw new Error(`FLOW_RESOURCE_ID:${field.name}`);
    if (typeof value === 'string' && new TextEncoder().encode(value).length > (field.max_length ?? 512)) throw new Error(`FLOW_ARGUMENT_LENGTH:${field.name}`);
    if (field.choices && !field.choices.includes(value)) throw new Error(`FLOW_ARGUMENT_CHOICE:${field.name}`);
    result[field.name] = value;
  }
  return result;
}
export function groupedFlows(flows: PublicFlowSummary[]) {
  const groups = new Map<string, PublicFlowSummary[]>();
  for (const flow of flows) {
    const category = flow.interface?.category || '未分类';
    const bucket = groups.get(category) ?? [];
    bucket.push(flow); groups.set(category, bucket);
  }
  return [...groups].sort(([a],[b]) => a.localeCompare(b)).map(([category, items]) => ({ category, items }));
}
/** 提取是纯草稿变换，不写服务端。多入口/不兼容出口宁可明确拒绝，也不猜测连线语义。 */
export function extractPublicBlock<T extends FlowGraph>(source: T, selected: string[], id: string, name: string) {
  if (!/^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/.test(id) || !name.trim()) throw new Error('FLOW_ID_OR_NAME_INVALID');
  const chosen = new Set(selected);
  const members = source.nodes.filter(node => chosen.has(node.id));
  if (!members.length || members.length !== chosen.size) throw new Error('EXTRACT_SELECTION_INVALID');
  if (members.some(node => node.data.node_type === 'end')) throw new Error('EXTRACT_TERMINAL_SELECTED');
  if ((source.interface?.parameters ?? []).some(p => p.bindings.some(b => chosen.has(b.node))))
    throw new Error('EXTRACT_PUBLIC_BINDING_REQUIRES_REMAP');
  // 内联循环改为函数调用会重置子图命中预算；自动提取不能静默改变这个语义。
  const cycleFrom=(origin:string) => {
    const pending=source.edges.filter(e=>e.source===origin).map(e=>e.target), seen=new Set<string>();
    while(pending.length){const n=pending.pop()!;if(n===origin)return true;if(seen.has(n))continue;
      seen.add(n);pending.push(...source.edges.filter(e=>e.source===n).map(e=>e.target));}
    return false;
  };
  if([...chosen].some(cycleFrom)) throw new Error('EXTRACT_LOOP_BUDGET_REQUIRES_EXPLICIT_BLOCK');
  const incoming = source.edges.filter(edge => !chosen.has(edge.source) && chosen.has(edge.target));
  const outgoing = source.edges.filter(edge => chosen.has(edge.source) && !chosen.has(edge.target));
  const entries = new Set(incoming.map(edge => edge.target));
  if (source.entry_node_id && chosen.has(source.entry_node_id)) entries.add(source.entry_node_id);
  if (entries.size !== 1 || !outgoing.length) throw new Error('EXTRACT_SINGLE_ENTRY_EXIT_REQUIRED');
  if (outgoing.some(edge => edge.data?.kind === 'failure' || edge.sourceHandle === 'failure'))
    throw new Error('EXTRACT_FAILURE_BOUNDARY_REQUIRES_INCLUDE');
  const exits = [...new Set(outgoing.map(edge => edge.source))];
  const normalized = (from: string) => outgoing.filter(e => e.source === from)
    .sort((a,b) => (a.data?.order ?? 0) - (b.data?.order ?? 0));
  const signature = (from: string) => JSON.stringify(normalized(from).map(e => [e.target, e.targetHandle ?? null, e.data?.order ?? 0]));
  if (exits.some(from => signature(from) !== signature(exits[0]))) throw new Error('EXTRACT_DIFFERENT_EXITS');
  if (source.edges.some(e => exits.includes(e.source) && chosen.has(e.target) && e.data?.kind !== 'failure' && e.sourceHandle !== 'failure'))
    throw new Error('EXTRACT_MIXED_INTERNAL_EXIT');
  let end = 'block_complete'; while (chosen.has(end)) end += '_';
  let callId = `call-${id.slice(0,40)}`;
  while (source.nodes.some(n => n.id === callId)) callId += '_';
  if (callId.length > 64) throw new Error('EXTRACT_CALL_ID_CAPACITY');
  const block: FlowGraph = {
    id, name: name.trim(), description: '', entry_node_id: [...entries][0],
    interface: { kind: 'block', category: source.interface?.category ?? '', parameters: [] },
    time_limit_ms: source.time_limit_ms, resource_locale: source.resource_locale,
    nodes: [...clone(members), { id: end, type: 'editor', position: {x: 750,y: 100},
      data: {label:'块完成',node_type:'end',parameters:{outcome:'success'}} }],
    edges: [...clone(source.edges.filter(e => chosen.has(e.source) && chosen.has(e.target))),
      ...exits.map((from, i) => ({id:`block_exit_${i}`, source:from,target:end,sourceHandle:'success',data:{kind:'sequence' as const,order:0}}))],
  };
  if (new Set(block.edges.map(e=>e.id)).size !== block.edges.length) throw new Error('EXTRACT_EDGE_ID_COLLISION');
  const entryNode=members.find(n=>n.id===[...entries][0])!;
  const caller = clone(source);
  caller.nodes = [...caller.nodes.filter(n => !chosen.has(n.id)), {
    id:callId,type:'editor',position:clone(entryNode.position),
    ...(entryNode.repeat_limit?{repeat_limit:entryNode.repeat_limit}:{}),
    data:{label:name.trim(),node_type:'call',parameters:{flow_id:id,arguments:{},extensions:{}}},
  }];
  caller.edges = caller.edges.filter(e => !chosen.has(e.source) && !chosen.has(e.target));
  caller.edges.push(...incoming.map(e => ({...clone(e),target:callId})),
    ...normalized(exits[0]).map(e => ({...clone(e),source:callId})));
  if (chosen.has(caller.entry_node_id ?? '')) caller.entry_node_id = callId;
  return {block, caller};
}
