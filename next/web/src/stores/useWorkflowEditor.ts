import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import { useRunSession } from "./useRunSession";
import { extractPublicBlock, type PublicInterface } from "../features/authoring/flowModel";
import {
  createWorkflow, deleteWorkflow, formatApiError, importTaskWorkflow, listWorkflows, readCatalog,
  readWorkflow, runWorkflow, saveWorkflow, inspectBuiltin, syncBuiltin,
} from "../api/client";
import type { BuiltinInspection, Catalog, WorkflowDefinition } from "../api/types";

// 作者数据是纯 JSON。先序列化可避免把 Vue Proxy 传给 structuredClone。
const clone = <T>(value: T): T => JSON.parse(JSON.stringify(value)) as T;
const signature = (value: unknown) => JSON.stringify(value);
interface Snapshot { nodes: WorkflowDefinition["nodes"]; edges: WorkflowDefinition["edges"]; entry_node_id?: string; name: string; description?: string; time_limit_ms?: number; interface?: PublicInterface; resource_locale?: WorkflowDefinition["resource_locale"]; events?: WorkflowDefinition["events"]; checks?: WorkflowDefinition["checks"] }
interface DefinitionTrailEntry { flowId: string; nodeId: string }

function cleanWorkflow(workflow: WorkflowDefinition): WorkflowDefinition {
  return {
    ...workflow,
    nodes: workflow.nodes.map(({ id, type, position, data, repeat_limit, event_overrides, resume }) => ({ id, type, position, data, ...(repeat_limit ? { repeat_limit } : {}), ...(event_overrides ? { event_overrides } : {}), ...(resume ? { resume } : {}) })),
    edges: workflow.edges.map(({ id, source, target, sourceHandle, targetHandle, label, data }) => ({ id, source, target, sourceHandle, targetHandle, label, data })),
  };
}

export function useWorkflowEditor() {
  const session = useRunSession();
  let alive = true;
  let operation = 0;
  let readSequence = 0;
  const catalog = ref<Catalog>({});
  const workflows = ref<WorkflowDefinition[]>([]);
  const current = ref<WorkflowDefinition>();
  const savedSignature = ref("");
  const isNew = ref(false);
  const selectedNodeId = ref("");
  const selectedEdgeId = ref("");
  const history = ref<Snapshot[]>([]);
  const future = ref<Snapshot[]>([]);
  const definitionTrail = ref<DefinitionTrailEntry[]>([]);
  const run = computed(() => session.run);
  const starting = computed(() => session.starting);
  const loading = ref(false);
  const saving = ref(false);
  const error = ref("");
  const notice = ref("");
  const builtinDetail = ref<BuiltinInspection>();
  const editLocked = computed(() => saving.value || loading.value);
  function beginWrite() {
    if (editLocked.value) return;
    saving.value = true; session.writing.workflow = true; ++readSequence;
    return ++operation;
  }
  function owns(id: number) { return alive && id === operation; }
  function endWrite(id: number) { if (owns(id)) { saving.value = false; session.writing.workflow = false; } }

  const dirty = computed(() => Boolean(current.value) && signature(cleanWorkflow(current.value!)) !== savedSignature.value);
  const selectedNode = computed(() => current.value?.nodes.find((node) => node.id === selectedNodeId.value));
  const selectedEdge = computed(() => current.value?.edges.find((edge) => edge.id === selectedEdgeId.value));
  const runActive = computed(() => !session.canStart);
  const runLabel = computed(() => session.label);
  const runError = computed(() => session.error);
  const activeNodeId = computed(() => [...(run.value?.node_path??[])].reverse().find(p=>p.flow_id===current.value?.id)?.node_id
    ?? (run.value?.workflow_id===current.value?.id ? run.value?.current_node_id ?? run.value?.failed_node_id ?? "" : ""));
  const definitionCaller = computed(() => {
    const caller = definitionTrail.value.at(-1);
    if (!caller) return;
    return { ...caller, name: workflows.value.find((flow) => flow.id === caller.flowId)?.name ?? caller.flowId };
  });

  function snapshot(): Snapshot | undefined {
    if (!current.value) return;
    return clone({ nodes: current.value.nodes, edges: current.value.edges, entry_node_id: current.value.entry_node_id, name: current.value.name, description: current.value.description, time_limit_ms: current.value.time_limit_ms as number | undefined, interface:current.value.interface, resource_locale:current.value.resource_locale, events:current.value.events, checks:current.value.checks });
  }
  function checkpoint() {
    if (editLocked.value) return;
    const value = snapshot();
    if (!value) return;
    history.value.push(value);
    if (history.value.length > 80) history.value.shift();
    future.value = [];
  }
  function applySnapshot(value: Snapshot) {
    if (!current.value) return;
    for (const key of ["entry_node_id", "description", "time_limit_ms", "interface", "resource_locale", "events", "checks"] as const) {
      if (!(key in value)) delete current.value[key];
    }
    Object.assign(current.value, clone(value));
    selectedNodeId.value = "";
    selectedEdgeId.value = "";
  }
  function undo() {
    if (editLocked.value) return;
    const value = history.value.pop();
    const now = snapshot();
    if (!value || !now) return;
    future.value.push(now);
    applySnapshot(value);
  }
  function redo() {
    if (editLocked.value) return;
    const value = future.value.pop();
    const now = snapshot();
    if (!value || !now) return;
    history.value.push(now);
    applySnapshot(value);
  }
  function accept(workflow: WorkflowDefinition, fresh = false) {
    current.value = clone(workflow);
    savedSignature.value = signature(cleanWorkflow(workflow));
    isNew.value = fresh;
    selectedNodeId.value = "";
    selectedEdgeId.value = "";
    history.value = [];
    future.value = [];
    builtinDetail.value = undefined;
  }
  async function refreshList() {
    const response = await listWorkflows();
    if (alive) workflows.value = Array.isArray(response) ? response : response.workflows;
  }
  async function load() {
    if (editLocked.value) return;
    loading.value = true;
    error.value = "";
    try {
      const catalogValue = await readCatalog();
      if (!alive) return;
      catalog.value = catalogValue;
      await refreshList();
      if (alive && !current.value && workflows.value.length) {
        const value = await readWorkflow(workflows.value[0].id);
        if (alive) accept(value);
      }
    } catch (reason) { if (alive) error.value = formatApiError(reason); }
    finally { if (alive) loading.value = false; }
  }
  async function switchTo(id: string) {
    if (editLocked.value) return false;
    if (dirty.value && !window.confirm("放弃当前流程中未保存的更改？")) return;
    error.value = "";
    const seq = ++readSequence;
    loading.value = true;
    try { const value = await readWorkflow(id); if (!alive || seq !== readSequence) return false; accept(value); return true; }
    catch (reason) { if (alive && seq === readSequence) error.value = formatApiError(reason); return false; }
    finally { if (alive && seq === readSequence) loading.value = false; }
  }
  async function open(id: string) {
    if (await switchTo(id)) definitionTrail.value = [];
  }
  function createBlank() {
    if (editLocked.value) return;
    if (dirty.value && !window.confirm("放弃当前流程中未保存的更改？")) return;
    definitionTrail.value = [];
    accept({ id: `new-${crypto.randomUUID()}`, name: "未命名流程", description: "", nodes: [], edges: [] }, true);
    savedSignature.value = "";
  }
  function copyCurrent() {
    if (editLocked.value) return;
    if (!current.value) return;
    definitionTrail.value = [];
    const copy = cleanWorkflow(clone(current.value));
    copy.id = `copy-${crypto.randomUUID()}`;
    copy.name = `${copy.name} 副本`;
    delete copy.revision;
    copy.created_from = current.value.id;
    accept(copy, true);
    savedSignature.value = "";
  }
  async function importTask(taskId: string) {
    if (!taskId || editLocked.value) return;
    if (dirty.value && !window.confirm("放弃当前流程中未保存的更改？")) return;
    error.value = "";
    const id = beginWrite(); if (id === undefined) return;
    try {
      const flowId = `task-${taskId}-${Date.now().toString(36)}`.replace(/[^A-Za-z0-9_-]/g, "-").slice(0, 64);
      definitionTrail.value = [];
      const value = await importTaskWorkflow(taskId, flowId);
      if (!owns(id)) return;
      accept(value);
      await refreshList();
      if (owns(id)) notice.value = "已复制现有任务为可编辑流程，原任务保持不变";
    } catch (reason) { if (owns(id)) error.value = formatApiError(reason); }
    finally { endWrite(id); }
  }
  async function save() {
    if (!current.value) return;
    const id = beginWrite(); if (id === undefined) return;
    const payload = cleanWorkflow(clone(current.value));
    const fresh = isNew.value;
    error.value = "";
    notice.value = "";
    try {
      const saved = fresh ? await createWorkflow(payload) : await saveWorkflow(payload);
      if (!owns(id)) return;
      accept(saved);
      await refreshList();
      if (owns(id)) notice.value = "流程已由服务端校验并保存";
    } catch (reason) { if (owns(id)) error.value = formatApiError(reason); }
    finally { endWrite(id); }
  }
  async function reload() { if (current.value && !isNew.value) await open(current.value.id); }
  async function viewBuiltin() {
    if (!current.value || editLocked.value) return;
    const seq = readSequence, flowId = current.value.id;
    try { const value = await inspectBuiltin(flowId); if (alive && seq === readSequence && current.value?.id === flowId) builtinDetail.value = value; }
    catch (reason) { if (alive && seq === readSequence && current.value?.id === flowId) error.value = formatApiError(reason); }
  }
  async function applyBuiltin() {
    const detail = builtinDetail.value;
    if (!detail || detail.status !== "update_available" || dirty.value || editLocked.value) return;
    if (!window.confirm(`用交付包内置定义更新“${current.value?.name}”？旧文档会备份，固定版本引用不会自动改写。`)) return;
    const id = beginWrite(); if (id === undefined) return;
    const frozen = clone(detail);
    try {
      const value = await syncBuiltin(frozen.flow_id, frozen.local_revision, frozen.builtin_revision);
      if (!owns(id)) return;
      accept(value);
      await refreshList();
      if (owns(id)) notice.value = "内置定义已同步，新运行将使用新版本";
    } catch (reason) { if (owns(id)) error.value = formatApiError(reason); }
    finally { endWrite(id); }
  }
  async function remove() {
    if (!current.value || isNew.value || editLocked.value || !window.confirm(`删除流程“${current.value.name}”？`)) return;
    const id = beginWrite(); if (id === undefined) return;
    const frozen = clone(current.value);
    try {
      if (!frozen.revision) throw new Error("WORKFLOW_REVISION_REQUIRED");
      await deleteWorkflow(frozen.id, frozen.revision);
      if (!owns(id)) return;
      current.value = undefined;
      definitionTrail.value = [];
      savedSignature.value = "";
      await refreshList();
      if (workflows.value.length) { const value = await readWorkflow(workflows.value[0].id); if (owns(id)) accept(value); }
    } catch (reason) { if (owns(id)) error.value = formatApiError(reason); }
    finally { endWrite(id); }
  }
  async function extractSelection(ids: string[]) {
    if (!current.value || !ids.length || editLocked.value) return;
    const name = window.prompt("公共块名称");
    if (!name?.trim()) return;
    const before = signature(cleanWorkflow(current.value));
    const id = beginWrite(); if (id === undefined) return;
    try {
      const value = extractPublicBlock(clone(current.value), [...ids], `block-${crypto.randomUUID()}`, name);
      // 先创建完整公共定义。失败时原草稿不变；成功后调用者仍是草稿，不暗中保存或覆盖 CAS。
      await createWorkflow(value.block as WorkflowDefinition);
      if (!owns(id)) return;
      if (!current.value || signature(cleanWorkflow(current.value)) !== before) {
        await refreshList(); throw new Error("EXTRACT_CALLER_CHANGED: 公共块已创建，当前草稿未覆盖");
      }
      const previous = snapshot(); if (previous) history.value.push(previous);
      future.value = []; current.value = value.caller;
      selectedNodeId.value = ""; selectedEdgeId.value = "";
      await refreshList(); if (owns(id)) notice.value = "公共块已创建；调用者替换已进入草稿，请保存。";
    } catch (reason) { if (owns(id)) error.value = formatApiError(reason); }
    finally { endWrite(id); }
  }
  async function openDefinition(id:string,nodeId?:string) {
    if (!current.value || editLocked.value) return;
    if (current.value.id === id) {
      if (nodeId) selectedNodeId.value = nodeId;
      return;
    }
    const caller = { flowId: current.value.id, nodeId: selectedNodeId.value };
    if (await switchTo(id)) {
      definitionTrail.value.push(caller);
      if (nodeId) selectedNodeId.value = nodeId;
    }
  }
  async function returnToCaller() {
    const caller = definitionTrail.value.at(-1);
    if (!caller || !await switchTo(caller.flowId)) return;
    definitionTrail.value.pop();
    selectedNodeId.value = caller.nodeId;
  }
  async function runSaved(selectedOnly = false) {
    if (!current.value || runActive.value || editLocked.value) return;
    if (dirty.value || isNew.value) {
      error.value = "WORKFLOW_UNSAVED: 请先保存当前流程，运行只使用服务端保存版本";
      return;
    }
    const fingerprint = JSON.stringify([current.value.id, current.value.revision,
      selectedOnly, selectedOnly ? selectedNodeId.value : null, current.value.resource_locale]);
    const flow = clone(current.value), nodeId = selectedNodeId.value;
    error.value = "";
    try {
      await session.submit(fingerprint, flow.name, (requestId) => runWorkflow(flow.id, {
        mode: selectedOnly ? "selected_node" : "workflow",
        ...(selectedOnly ? { node_id: nodeId } : {}),
        revision: flow.revision, request_id: requestId, resource_locale:flow.resource_locale??"",
      }));
      if (alive) notice.value = "流程启动已接收，查看真实运行/准备状态";
    } catch (reason) { if (alive) error.value = formatApiError(reason); }
  }
  onMounted(() => { void load(); });
  onBeforeUnmount(() => { alive = false; ++operation; ++readSequence; session.writing.workflow = false; });

  return reactive({
    catalog, workflows, current, isNew, selectedNodeId, selectedEdgeId, selectedNode, selectedEdge, builtinDetail,
    history, future, definitionCaller, run, loading, saving, error, notice, dirty, runActive, runLabel, runError, starting, activeNodeId,
    checkpoint, undo, redo, load, open, createBlank, copyCurrent, importTask, save, reload, remove, runSaved, requestStop: session.requestStop, extractSelection, openDefinition, returnToCaller, viewBuiltin, applyBuiltin, editLocked,
  });
}
