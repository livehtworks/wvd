<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { Handle, Position, VueFlow, type Connection, type EdgeMouseEvent, type NodeDragEvent, type NodeMouseEvent } from "@vue-flow/core";
import "@vue-flow/core/dist/style.css";
import "@vue-flow/core/dist/theme-default.css";
import {
  Beaker, CircleStop, Copy, GitBranch, Play, Plus, Redo2, RefreshCw, Save, Trash2, Undo2,
} from "@lucide/vue";
import { formatApiError, probeRecognition } from "../api/client";
import type { JsonObject, RecognitionProbeResult, WorkflowEdge, WorkflowNode } from "../api/types";
import { useWorkflowEditor } from "../stores/useWorkflowEditor";

const state = useWorkflowEditor();
const emit = defineEmits<{ dirty: [value: boolean] }>();
watch(() => state.dirty, (value) => emit("dirty", value), { immediate: true });
const probeImage = ref<File>();
const probeBusy = ref(false);
const probeResult = ref<RecognitionProbeResult>();
const probeSource = ref<"current" | "local">("current");
const editorError = ref("");
const importTaskId = ref("");

// 画布节点和参数来自 Vue 响应式状态，JSON 克隆同时剥离 Proxy。
const cloneJson = <T>(value: T): T => JSON.parse(JSON.stringify(value)) as T;

const selectedParameters = computed(() => state.selectedNode?.data.parameters ?? {});
const nodeTypes = computed(() => state.catalog.node_types ?? []);
const recognitionAssets = computed(() => [
  ...(state.catalog.templates ?? []),
  ...(state.catalog.recognizers ?? []),
]);
const flowNodes = computed(() => state.current?.nodes ?? []);
const flowEdges = computed(() => state.current?.edges ?? []);

function uid(prefix: string) { return `${prefix}-${crypto.randomUUID()}`; }
function addNode(type: string) {
  if (!state.current) return;
  const definition = nodeTypes.value.find((item) => item.type === type);
  if (!definition) return;
  state.checkpoint();
  const node: WorkflowNode = {
    id: uid("node"), type: "editor",
    position: { x: 90 + (state.current.nodes.length % 4) * 220, y: 80 + Math.floor(state.current.nodes.length / 4) * 140 },
    data: { label: definition.label, node_type: definition.type, parameters: cloneJson(definition.defaults ?? {}) },
  };
  state.current.nodes.push(node);
  state.selectedNodeId = node.id;
  state.selectedEdgeId = "";
  if (!state.current.entry_node_id) state.current.entry_node_id = node.id;
}
function duplicateNode() {
  if (!state.current || !state.selectedNode) return;
  state.checkpoint();
  const source = state.selectedNode;
  const node = cloneJson(source);
  node.id = uid("node");
  node.position = { x: source.position.x + 36, y: source.position.y + 36 };
  node.data.label = `${source.data.label} 副本`;
  state.current.nodes.push(node);
  state.selectedNodeId = node.id;
}
function deleteSelection() {
  if (!state.current) return;
  if (!state.selectedNodeId && !state.selectedEdgeId) return;
  state.checkpoint();
  if (state.selectedNodeId) {
    const id = state.selectedNodeId;
    state.current.nodes = state.current.nodes.filter((node) => node.id !== id);
    state.current.edges = state.current.edges.filter((edge) => edge.source !== id && edge.target !== id);
    if (state.current.entry_node_id === id) state.current.entry_node_id = undefined;
    state.selectedNodeId = "";
  } else {
    state.current.edges = state.current.edges.filter((edge) => edge.id !== state.selectedEdgeId);
    state.selectedEdgeId = "";
  }
}
function connect(connection: Connection) {
  if (!state.current || !connection.source || !connection.target || connection.source === connection.target) return;
  if (state.current.edges.some((edge) => edge.source === connection.source && edge.target === connection.target && edge.sourceHandle === connection.sourceHandle)) return;
  state.checkpoint();
  const outcome = connection.sourceHandle === "failure" ? "failure" : "success";
  const siblings = state.current.edges.filter((edge) => edge.source === connection.source &&
    (edge.data?.kind === "failure" ? "failure" : "success") === outcome);
  state.current.edges.push({
    id: uid("edge"), source: connection.source, target: connection.target,
    sourceHandle: connection.sourceHandle, targetHandle: connection.targetHandle,
    label: `${connection.sourceHandle === "failure" ? "failure" : "sequence"} · ${siblings.length}`,
    data: { kind: connection.sourceHandle === "failure" ? "failure" : "sequence", order: siblings.length },
  });
}
function selectNode(event: NodeMouseEvent) {
  state.selectedNodeId = event.node.id;
  state.selectedEdgeId = "";
}
function selectEdge(event: EdgeMouseEvent) {
  state.selectedEdgeId = event.edge.id;
  state.selectedNodeId = "";
}
function dragStart() { state.checkpoint(); }
function dragStop(event: NodeDragEvent) {
  const node = state.current?.nodes.find((item) => item.id === event.node.id);
  if (node) node.position = { x: event.node.position.x, y: event.node.position.y };
}
function setEntry() { if (state.current && state.selectedNodeId) { state.checkpoint(); state.current.entry_node_id = state.selectedNodeId; } }
function param(name: string) { return selectedParameters.value[name]; }
function textParam(name: string, event: Event) { setParam(name, (event.target as HTMLInputElement | HTMLSelectElement).value); }
function numberParam(name: string, event: Event) { setParam(name, Number((event.target as HTMLInputElement).value)); }
function boolParam(name: string, event: Event) { setParam(name, (event.target as HTMLInputElement).checked); }
function setParam(name: string, value: unknown) {
  if (!state.selectedNode) return;
  state.checkpoint();
  state.selectedNode.data.parameters[name] = value;
}
const nodeKind = computed(() => state.selectedNode?.data.node_type ?? "");
const selectedCondition = computed<JsonObject>(() => {
  const parameters = selectedParameters.value;
  return (parameters.condition as JsonObject | undefined) ?? {};
});
function setRecognitionMode(event: Event) {
  if (!state.selectedNode) return;
  state.checkpoint();
  const mode = (event.target as HTMLSelectElement).value;
  state.selectedNode.data.parameters.condition = mode === "template"
    ? { mode, image: String(state.catalog.templates?.[0]?.value ?? ""), threshold: 0.8, roi: [0, 0, 900, 1600] }
    : mode === "ocr" ? { mode, expected: ["Pause"], roi: [0, 0, 900, 1600] }
      : { mode };
}
function setConditionField(name: string, value: unknown) {
  if (!state.selectedNode) return;
  state.checkpoint();
  const condition = cloneJson(selectedCondition.value);
  condition[name] = value;
  state.selectedNode.data.parameters.condition = condition;
}
function setConditionText(name: string, event: Event) { setConditionField(name, (event.target as HTMLInputElement | HTMLSelectElement).value); }
function setConditionNumber(name: string, event: Event) { setConditionField(name, Number((event.target as HTMLInputElement).value)); }
function setOcrText(event: Event) {
  const values = (event.target as HTMLInputElement).value.split("|").map((item) => item.trim()).filter(Boolean);
  setConditionField("expected", values);
}
function setRoi(index: number, event: Event) {
  const roi = Array.isArray(selectedCondition.value.roi) ? [...selectedCondition.value.roi as number[]] : [0, 0, 900, 1600];
  roi[index] = Number((event.target as HTMLInputElement).value);
  setConditionField("roi", roi);
}
function changeAction(event: Event) {
  if (!state.selectedNode) return;
  state.checkpoint();
  const operation = (event.target as HTMLSelectElement).value;
  const scene = { mode: "combat_active" }, postcondition = { mode: "combat_active" };
  state.selectedNode.data.parameters = operation === "fixed_click"
    ? { operation, scene, postcondition, position: [450, 800] }
    : operation === "click" ? { operation, scene, target: { mode: "target_marker" }, postcondition, offset: [0, 0] }
      : operation === "swipe" ? { operation, scene, postcondition, coordinates: [450, 1000, 450, 500], duration_ms: 400 }
        : { operation: "back", scene, postcondition };
}
function setActionCondition(name: "scene" | "postcondition" | "target", event: Event) {
  setParam(name, { mode: (event.target as HTMLInputElement).value });
}
function setArrayValue(name: string, index: number, event: Event) {
  const current = Array.isArray(param(name)) ? [...param(name) as number[]] : [];
  current[index] = Number((event.target as HTMLInputElement).value);
  setParam(name, current);
}
function changeBusiness(event: Event) {
  if (!state.selectedNode) return;
  state.checkpoint();
  const binding = (event.target as HTMLSelectElement).value;
  state.selectedNode.data.parameters = binding === "chest"
    ? { binding, condition: { mode: "combat_active" }, preferred: 0, seed: 1 }
    : binding === "confirm"
      ? { binding, operation_id: "custom-confirm", event: "target_completed", condition: { mode: "target_marker" }, expected_step: 0 }
      : { binding: "combat", condition: { mode: "combat_active" }, arguments: { operation: "auto_confirmed", index: 0 } };
}
function setRepeat(event: Event) {
  if (!state.selectedNode) return;
  state.checkpoint();
  const value = Number((event.target as HTMLInputElement).value);
  state.selectedNode.repeat_limit = value > 1 ? value : undefined;
}
function edgeOrder(event: Event) {
  if (!state.selectedEdge) return;
  state.checkpoint();
  state.selectedEdge.data ??= {};
  state.selectedEdge.data.order = Number((event.target as HTMLInputElement).value);
  state.selectedEdge.label = edgeLabel(state.selectedEdge);
}
function edgeKind(event: Event) {
  if (!state.selectedEdge) return;
  state.checkpoint();
  state.selectedEdge.data ??= {};
  state.selectedEdge.data.kind = (event.target as HTMLSelectElement).value as "sequence" | "candidate" | "failure";
  state.selectedEdge.sourceHandle = state.selectedEdge.data.kind === "failure" ? "failure" : "success";
  state.selectedEdge.label = edgeLabel(state.selectedEdge);
}
function chooseProbeFile(event: Event) { probeImage.value = (event.target as HTMLInputElement).files?.[0]; }
async function runProbe() {
  if (!state.selectedNode) return;
  probeBusy.value = true;
  editorError.value = "";
  probeResult.value = undefined;
  try {
    const payload: JsonObject = {
      workflow_id: state.current?.id, node_id: state.selectedNode.id,
      source: probeSource.value, recognition: cloneJson(state.selectedNode.data.parameters),
    };
    probeResult.value = await probeRecognition(payload, probeSource.value === "local" ? probeImage.value : undefined);
  } catch (reason) { editorError.value = formatApiError(reason); }
  finally { probeBusy.value = false; }
}
function edgeLabel(edge: WorkflowEdge) { return `${edge.data?.kind ?? "sequence"} · ${edge.data?.order ?? 0}`; }
</script>

<template>
  <main class="workflow-page">
    <header class="editor-toolbar">
      <div class="workflow-picker">
        <select aria-label="流程" :value="state.current?.id ?? ''" @change="state.open(($event.target as HTMLSelectElement).value)">
          <option value="" disabled>选择流程</option><option v-for="item in state.workflows" :key="item.id" :value="item.id">{{ item.name }}</option>
        </select>
        <button class="icon-button" title="新建流程" aria-label="新建流程" @click="state.createBlank"><Plus :size="17" /></button>
        <button class="icon-button" title="复制流程" aria-label="复制流程" :disabled="!state.current" @click="state.copyCurrent"><Copy :size="16" /></button>
        <button class="icon-button danger" title="删除流程" aria-label="删除流程" :disabled="!state.current || state.isNew" @click="state.remove"><Trash2 :size="16" /></button>
        <select v-model="importTaskId" aria-label="复制现有任务"><option value="">复制现有任务…</option><option v-for="task in state.catalog.tasks?.filter((item) => item.type === 'dungeon') ?? []" :key="task.id" :value="task.id">{{ task.name }}</option></select>
        <button class="button secondary" :disabled="!importTaskId" @click="state.importTask(importTaskId)"><Copy :size="16" />生成编辑副本</button>
      </div>
      <div class="command-row">
        <span v-if="state.dirty" class="status-chip warning">未保存</span>
        <button class="icon-button" title="撤销" aria-label="撤销" :disabled="!state.history.length" @click="state.undo"><Undo2 :size="17" /></button>
        <button class="icon-button" title="重做" aria-label="重做" :disabled="!state.future.length" @click="state.redo"><Redo2 :size="17" /></button>
        <button class="button secondary" :disabled="!state.current || state.isNew" @click="state.reload"><RefreshCw :size="16" />重载</button>
        <button class="button primary" :disabled="!state.current || !state.dirty || state.saving" @click="state.save"><Save :size="16" />保存</button>
        <button class="button run" :disabled="!state.current || state.dirty || state.runActive" @click="state.runSaved(false)"><Play :size="16" />运行</button>
        <button class="button danger" :disabled="!state.runActive" @click="state.requestStop"><CircleStop :size="16" />停止</button>
      </div>
    </header>
    <div v-if="state.error || editorError" class="notice error editor-notice" role="alert">{{ state.error || editorError }}</div>
    <div v-if="state.notice" class="notice success editor-notice" role="status">{{ state.notice }}</div>

    <div class="editor-shell">
      <aside class="node-library">
        <header><h2>节点</h2><span>{{ nodeTypes.length }}</span></header>
        <div v-if="nodeTypes.length" class="node-type-list"><button v-for="item in nodeTypes" :key="item.type" @click="addNode(item.type)"><Plus :size="14" /><span><strong>{{ item.label }}</strong><small>{{ item.category ?? item.type }}</small></span></button></div>
        <p v-else class="empty-state">目录未返回可编排节点</p>
      </aside>

      <section class="flow-region" aria-label="流程画布">
        <div v-if="state.current" class="flow-title">
          <input v-model="state.current.name" aria-label="流程名称" @focus="state.checkpoint" /><input v-model="state.current.description" aria-label="流程说明" placeholder="流程说明" @focus="state.checkpoint" />
          <span class="mono">{{ state.current.revision ?? '尚未保存' }}</span>
        </div>
        <VueFlow v-if="state.current" :nodes="flowNodes" :edges="flowEdges" :delete-key-code="null" fit-view-on-init class="flow-canvas" @connect="connect" @node-click="selectNode" @edge-click="selectEdge" @node-drag-start="dragStart" @node-drag-stop="dragStop">
          <template #node-editor="slotProps">
            <div :class="['editor-node', { entry: state.current?.entry_node_id === slotProps.id, active: state.activeNodeId === slotProps.id, failed: state.run?.failed_node_id === slotProps.id }]">
              <Handle type="target" :position="Position.Left" /><span class="node-kind">{{ slotProps.data.node_type }}</span><strong>{{ slotProps.data.label }}</strong><span v-if="state.current?.entry_node_id === slotProps.id" class="node-mark">入口</span><Handle id="success" type="source" :position="Position.Right" /><Handle id="failure" type="source" :position="Position.Bottom" />
            </div>
          </template>
        </VueFlow>
        <div v-else class="canvas-empty"><GitBranch :size="38" /><p>新建或打开一个流程</p></div>
        <footer class="run-strip"><span :class="['connection-dot', { online: state.runActive }]" />{{ state.run?.state ?? 'Idle' }}<span>{{ state.run?.step_name ?? state.activeNodeId ?? '—' }}</span><span class="mono">{{ state.run?.run_id ?? '' }}</span></footer>
      </section>

      <aside class="property-panel">
        <template v-if="state.selectedNode">
          <header><div><span>节点参数</span><small class="mono">{{ state.selectedNode.id }}</small></div><div class="command-row"><button class="icon-button" title="复制节点" aria-label="复制节点" @click="duplicateNode"><Copy :size="15" /></button><button class="icon-button danger" title="删除节点" aria-label="删除节点" @click="deleteSelection"><Trash2 :size="15" /></button></div></header>
          <label class="field"><span>名称</span><input v-model="state.selectedNode.data.label" @focus="state.checkpoint" /></label>
          <label class="field"><span>节点类型</span><input :value="state.selectedNode.data.node_type" readonly /></label>
          <button class="button secondary full" :disabled="state.current?.entry_node_id === state.selectedNodeId" @click="setEntry">设为入口</button>
          <div v-if="nodeKind === 'recognition'" class="inspector-group"><h3>识别</h3>
            <label class="field"><span>识别方式</span><select :value="String(selectedCondition.mode ?? '')" @change="setRecognitionMode"><option value="template">模板</option><option value="ocr">OCR</option><option v-for="item in state.catalog.recognizers ?? []" :key="String(item.value)" :value="String(item.value)">WVD · {{ item.label }}</option></select></label>
            <label v-if="selectedCondition.mode === 'template'" class="field"><span>模板</span><input list="recognition-assets" :value="String(selectedCondition.image ?? '')" @input="setConditionText('image', $event)" /><datalist id="recognition-assets"><option v-for="item in state.catalog.templates ?? []" :key="String(item.value)" :value="String(item.value)">{{ item.label }}</option></datalist></label>
            <label v-if="selectedCondition.mode === 'template'" class="field"><span>阈值</span><input :value="Number(selectedCondition.threshold ?? 0.8)" type="number" min="0" max="1" step="0.01" @input="setConditionNumber('threshold', $event)" /></label>
            <label v-if="selectedCondition.mode === 'ocr'" class="field"><span>OCR 文本（多个用 | 分隔）</span><input :value="Array.isArray(selectedCondition.expected) ? selectedCondition.expected.join('|') : ''" @change="setOcrText" /></label>
            <div v-if="['template','ocr'].includes(String(selectedCondition.mode))" class="roi-grid"><label v-for="(label, index) in ['X','Y','宽','高']" :key="label" class="field"><span>{{ label }}</span><input :value="Number((selectedCondition.roi as number[] | undefined)?.[index] ?? [0,0,900,1600][index])" type="number" min="0" @change="setRoi(index, $event)" /></label></div>
          </div>
          <div v-else-if="nodeKind === 'action'" class="inspector-group"><h3>受控动作</h3>
            <label class="field"><span>动作</span><select :value="String(param('operation') ?? 'fixed_click')" @change="changeAction"><option value="fixed_click">固定坐标点击</option><option value="click">识别目标点击</option><option value="swipe">滑动</option><option value="back">返回键</option></select></label>
            <label class="field"><span>允许场景</span><input :value="String((param('scene') as JsonObject | undefined)?.mode ?? '')" @change="setActionCondition('scene', $event)" /></label>
            <label v-if="param('operation') === 'click'" class="field"><span>目标识别</span><input :value="String((param('target') as JsonObject | undefined)?.mode ?? '')" @change="setActionCondition('target', $event)" /></label>
            <label class="field"><span>后置条件</span><input :value="String((param('postcondition') as JsonObject | undefined)?.mode ?? '')" @change="setActionCondition('postcondition', $event)" /></label>
            <div v-if="param('operation') === 'fixed_click'" class="coordinate-grid"><label v-for="(label,index) in ['X','Y']" :key="label" class="field"><span>{{ label }}</span><input :value="Number((param('position') as number[])?.[index] ?? 0)" type="number" @change="setArrayValue('position', index, $event)" /></label></div>
            <div v-if="param('operation') === 'swipe'" class="coordinate-grid"><label v-for="(label,index) in ['X1','Y1','X2','Y2']" :key="label" class="field"><span>{{ label }}</span><input :value="Number((param('coordinates') as number[])?.[index] ?? 0)" type="number" @change="setArrayValue('coordinates', index, $event)" /></label></div>
            <label v-if="param('operation') === 'swipe'" class="field"><span>持续时间 (ms)</span><input :value="Number(param('duration_ms') ?? 400)" type="number" min="1" @change="numberParam('duration_ms', $event)" /></label>
          </div>
          <div v-else-if="nodeKind === 'wait'" class="inspector-group"><h3>等待</h3><label class="field"><span>时长 (ms)</span><input :value="Number(param('duration_ms') ?? 500)" type="number" min="1" max="10000" @change="numberParam('duration_ms', $event)" /></label></div>
          <div v-else-if="nodeKind === 'business'" class="inspector-group"><h3>业务子流程</h3><label class="field"><span>类型</span><select :value="String(param('binding') ?? 'combat')" :disabled="param('binding') === 'task_stage'" @change="changeBusiness"><option value="combat">战斗</option><option value="chest">开箱</option><option value="confirm">业务确认</option><option value="task_stage">现有任务阶段</option></select></label><template v-if="param('binding') === 'task_stage'"><label class="field"><span>任务</span><input :value="String(param('task_id') ?? '')" readonly /></label><label class="field"><span>阶段</span><select :value="String(param('stage') ?? '')" @change="textParam('stage', $event)"><option value="prepare">入本准备与补给</option><option value="enter">进入地下城</option><option value="traverse">路线、战斗与开箱</option></select></label></template><label v-if="param('binding') === 'chest'" class="field"><span>开箱角色</span><input :value="Number(param('preferred') ?? 0)" type="number" min="0" max="6" @change="numberParam('preferred', $event)" /></label><label v-if="param('binding') === 'confirm'" class="field"><span>确认事件</span><input :value="String(param('event') ?? '')" @change="textParam('event', $event)" /></label></div>
          <div v-else-if="nodeKind === 'end'" class="inspector-group"><h3>结束</h3><label class="field"><span>结果</span><select :value="String(param('outcome') ?? 'success')" @change="textParam('outcome', $event)"><option value="success">成功</option><option value="failure">失败</option></select></label><label v-if="param('outcome') === 'failure'" class="field"><span>原因</span><input :value="String(param('reason') ?? 'workflow_failed')" @change="textParam('reason', $event)" /></label></div>
          <div v-if="nodeKind !== 'end'" class="inspector-group"><h3>控制</h3><label class="field"><span>有限重复次数</span><input :value="state.selectedNode.repeat_limit ?? 1" type="number" min="1" max="256" @change="setRepeat" /></label></div>
          <div v-if="nodeKind === 'recognition'" class="inspector-group"><h3>试识别</h3>
            <span class="source-line">使用设备面板最近一次截图，不触发任何点击</span>
            <button class="button secondary full" :disabled="probeBusy" @click="runProbe"><Beaker :size="16" />试识别</button>
            <div v-if="probeResult" class="probe-result"><strong>{{ probeResult.outcome ?? '未知结果' }}</strong><span>置信度 {{ probeResult.score ?? '—' }}</span><span>ROI {{ probeResult.roi?.join(', ') ?? '—' }}</span><span v-if="probeResult.error_code">{{ probeResult.error_code }}: {{ probeResult.message }}</span><img v-if="probeResult.image_url" :src="probeResult.image_url" alt="试识别结果" /></div>
          </div>
          <button class="button run full" :disabled="state.dirty || state.runActive" @click="state.runSaved(true)"><Play :size="16" />调试选中节点</button>
        </template>
        <template v-else-if="state.selectedEdge">
          <header><div><span>连接</span><small class="mono">{{ state.selectedEdge.id }}</small></div><button class="icon-button danger" title="断开连接" aria-label="断开连接" @click="deleteSelection"><Trash2 :size="15" /></button></header>
          <label class="field"><span>连接语义</span><select :value="state.selectedEdge.data?.kind ?? 'sequence'" @change="edgeKind"><option value="sequence">顺序</option><option value="candidate">有序候选</option><option value="failure">失败出口</option></select></label>
          <label class="field"><span>顺序</span><input :value="state.selectedEdge.data?.order ?? 0" type="number" min="0" @input="edgeOrder" /></label>
          <p class="field-help">{{ state.selectedEdge.source }} → {{ state.selectedEdge.target }}</p>
        </template>
        <div v-else class="property-empty">选择节点或连接以编辑参数</div>
      </aside>
    </div>
  </main>
</template>
