import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import { runBusy, displayRunState } from "./runStatus";
import { extractPublicBlock, type PublicInterface } from "../features/authoring/flowModel";
import {
  createWorkflow, deleteWorkflow, formatApiError, importTaskWorkflow, listWorkflows, readCatalog, readCurrentRun,
  readWorkflow, runWorkflow, saveWorkflow, stopRun,
} from "../api/client";
import type { Catalog, RunState, WorkflowDefinition } from "../api/types";

// 作者数据是纯 JSON。先序列化可避免把 Vue Proxy 传给 structuredClone。
const clone = <T>(value: T): T => JSON.parse(JSON.stringify(value)) as T;
const signature = (value: unknown) => JSON.stringify(value);
interface Snapshot { nodes: WorkflowDefinition["nodes"]; edges: WorkflowDefinition["edges"]; entry_node_id?: string; name: string; description?: string; time_limit_ms?: number; interface?: PublicInterface; resource_locale?: string }
interface DefinitionTrailEntry { flowId: string; nodeId: string }

function cleanWorkflow(workflow: WorkflowDefinition): WorkflowDefinition {
  return {
    ...workflow,
    nodes: workflow.nodes.map(({ id, type, position, data, repeat_limit }) => ({ id, type, position, data, ...(repeat_limit ? { repeat_limit } : {}) })),
    edges: workflow.edges.map(({ id, source, target, sourceHandle, targetHandle, label, data }) => ({ id, source, target, sourceHandle, targetHandle, label, data })),
  };
}

export function useWorkflowEditor() {
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
  const run = ref<RunState>();
  const starting = ref(false);
  const linkError = ref("");
  let pendingRequest: { id: string; fingerprint: string } | undefined;
  let polling = false;
  const loading = ref(false);
  const saving = ref(false);
  const error = ref("");
  const notice = ref("");
  let pollHandle: number | undefined;

  const dirty = computed(() => Boolean(current.value) && signature(cleanWorkflow(current.value!)) !== savedSignature.value);
  const selectedNode = computed(() => current.value?.nodes.find((node) => node.id === selectedNodeId.value));
  const selectedEdge = computed(() => current.value?.edges.find((edge) => edge.id === selectedEdgeId.value));
  const runActive = computed(() => starting.value || runBusy(run.value));
  const runLabel = computed(() => starting.value ? "提交启动请求" : displayRunState(run.value));
  const runError = computed(() => run.value?.submission?.error ?? linkError.value);
  const activeNodeId = computed(() => [...(run.value?.node_path??[])].reverse().find(p=>p.flow_id===current.value?.id)?.node_id
    ?? (run.value?.workflow_id===current.value?.id ? run.value?.current_node_id ?? run.value?.failed_node_id ?? "" : ""));
  const definitionCaller = computed(() => {
    const caller = definitionTrail.value.at(-1);
    if (!caller) return;
    return { ...caller, name: workflows.value.find((flow) => flow.id === caller.flowId)?.name ?? caller.flowId };
  });

  function snapshot(): Snapshot | undefined {
    if (!current.value) return;
    return clone({ nodes: current.value.nodes, edges: current.value.edges, entry_node_id: current.value.entry_node_id, name: current.value.name, description: current.value.description, time_limit_ms: current.value.time_limit_ms as number | undefined, interface:current.value.interface, resource_locale:current.value.resource_locale });
  }
  function checkpoint() {
    const value = snapshot();
    if (!value) return;
    history.value.push(value);
    if (history.value.length > 80) history.value.shift();
    future.value = [];
  }
  function applySnapshot(value: Snapshot) {
    if (!current.value) return;
    Object.assign(current.value, clone(value));
    selectedNodeId.value = "";
    selectedEdgeId.value = "";
  }
  function undo() {
    const value = history.value.pop();
    const now = snapshot();
    if (!value || !now) return;
    future.value.push(now);
    applySnapshot(value);
  }
  function redo() {
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
  }
  async function refreshList() {
    const response = await listWorkflows();
    workflows.value = Array.isArray(response) ? response : response.workflows;
  }
  async function load() {
    loading.value = true;
    error.value = "";
    try {
      const [catalogValue, runValue] = await Promise.all([readCatalog(), readCurrentRun()]);
      catalog.value = catalogValue;
      run.value = runValue;
      await refreshList();
      if (!current.value && workflows.value.length) await open(workflows.value[0].id);
    } catch (reason) { error.value = formatApiError(reason); }
    finally { loading.value = false; }
  }
  async function switchTo(id: string) {
    if (dirty.value && !window.confirm("放弃当前流程中未保存的更改？")) return;
    error.value = "";
    try { accept(await readWorkflow(id)); return true; }
    catch (reason) { error.value = formatApiError(reason); return false; }
  }
  async function open(id: string) {
    if (await switchTo(id)) definitionTrail.value = [];
  }
  function createBlank() {
    if (dirty.value && !window.confirm("放弃当前流程中未保存的更改？")) return;
    definitionTrail.value = [];
    accept({ id: `new-${crypto.randomUUID()}`, name: "未命名流程", description: "", nodes: [], edges: [] }, true);
    savedSignature.value = "";
  }
  function copyCurrent() {
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
    if (!taskId) return;
    if (dirty.value && !window.confirm("放弃当前流程中未保存的更改？")) return;
    error.value = "";
    try {
      const flowId = `task-${taskId}-${Date.now().toString(36)}`.replace(/[^A-Za-z0-9_-]/g, "-").slice(0, 64);
      definitionTrail.value = [];
      accept(await importTaskWorkflow(taskId, flowId));
      await refreshList();
      notice.value = "已复制现有任务为可编辑流程，原任务保持不变";
    } catch (reason) { error.value = formatApiError(reason); }
  }
  async function save() {
    if (!current.value) return;
    saving.value = true;
    error.value = "";
    notice.value = "";
    try {
      const payload = cleanWorkflow(current.value);
      const saved = isNew.value ? await createWorkflow(payload) : await saveWorkflow(payload);
      accept(saved);
      await refreshList();
      notice.value = "流程已由服务端校验并保存";
    } catch (reason) { error.value = formatApiError(reason); }
    finally { saving.value = false; }
  }
  async function reload() { if (current.value && !isNew.value) await open(current.value.id); }
  async function remove() {
    if (!current.value || isNew.value || !window.confirm(`删除流程“${current.value.name}”？`)) return;
    try {
      if (!current.value.revision) throw new Error("WORKFLOW_REVISION_REQUIRED");
      await deleteWorkflow(current.value.id, current.value.revision);
      current.value = undefined;
      definitionTrail.value = [];
      savedSignature.value = "";
      await refreshList();
      if (workflows.value.length) await open(workflows.value[0].id);
    } catch (reason) { error.value = formatApiError(reason); }
  }
  async function extractSelection(ids: string[]) {
    if (!current.value || !ids.length) return;
    const name = window.prompt("公共块名称");
    if (!name?.trim()) return;
    const before = signature(cleanWorkflow(current.value));
    try {
      const value = extractPublicBlock(current.value, ids, `block-${crypto.randomUUID()}`, name);
      // 先创建完整公共定义。失败时原草稿不变；成功后调用者仍是草稿，不暗中保存或覆盖 CAS。
      await createWorkflow(value.block as WorkflowDefinition);
      if (!current.value || signature(cleanWorkflow(current.value)) !== before) {
        await refreshList(); throw new Error("EXTRACT_CALLER_CHANGED: 公共块已创建，当前草稿未覆盖");
      }
      checkpoint(); current.value = value.caller;
      selectedNodeId.value = ""; selectedEdgeId.value = "";
      await refreshList(); notice.value = "公共块已创建；调用者替换已进入草稿，请保存。";
    } catch (reason) { error.value = formatApiError(reason); }
  }
  async function openDefinition(id:string,nodeId?:string) {
    if (!current.value) return;
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
    if (!current.value || runActive.value || starting.value) return;
    if (dirty.value || isNew.value) {
      error.value = "WORKFLOW_UNSAVED: 请先保存当前流程，运行只使用服务端保存版本";
      return;
    }
    const fingerprint = JSON.stringify([current.value.id, current.value.revision,
      selectedOnly, selectedOnly ? selectedNodeId.value : null, current.value.resource_locale]);
    if (!pendingRequest || pendingRequest.fingerprint !== fingerprint)
      pendingRequest = { id: crypto.randomUUID(), fingerprint };
    starting.value = true;
    error.value = "";
    try {
      await runWorkflow(current.value.id, {
        mode: selectedOnly ? "selected_node" : "workflow",
        ...(selectedOnly ? { node_id: selectedNodeId.value } : {}),
        revision: current.value.revision, request_id: pendingRequest.id, resource_locale:current.value.resource_locale??"",
      });
      run.value = await readCurrentRun();
      notice.value = "流程启动已接收，查看真实运行/准备状态";
    } catch (reason) { error.value = formatApiError(reason); }
    finally { starting.value = false; }
  }
  async function requestStop() {
    if (!run.value?.run_id && !pendingRequest && !run.value?.submission?.request_id) return;
    try { run.value = await stopRun(run.value?.run_id,
      run.value?.submission?.state === "preparing" ? run.value.submission.request_id :
        starting.value ? pendingRequest?.id : undefined); }
    catch (reason) { error.value = formatApiError(reason); }
  }
  async function pollRun() {
    if (polling) return;
    polling = true;
    try {
      run.value = await readCurrentRun();
      linkError.value = "";
      if (pendingRequest && run.value.submission?.request_id === pendingRequest.id &&
          ["submitted", "failed", "cancelled"].includes(run.value.submission?.state ?? "") && !runBusy(run.value)) pendingRequest = undefined;
    } catch (reason) { linkError.value = `状态连接中断，不能确认已停止：${formatApiError(reason)}`; }
    finally { polling = false; }
  }
  onMounted(() => { void load(); pollHandle = window.setInterval(pollRun, 1000); });
  onBeforeUnmount(() => window.clearInterval(pollHandle));

  return reactive({
    catalog, workflows, current, isNew, selectedNodeId, selectedEdgeId, selectedNode, selectedEdge,
    history, future, definitionCaller, run, loading, saving, error, notice, dirty, runActive, runLabel, runError, starting, activeNodeId,
    checkpoint, undo, redo, load, open, createBlank, copyCurrent, importTask, save, reload, remove, runSaved, requestStop, extractSelection, openDefinition, returnToCaller,
  });
}
