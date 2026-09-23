<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { Plus, Trash2 } from "@lucide/vue";
import type { EventRule, EventResume, JsonObject, WorkflowNode } from "../../api/types";
import type { FlowCall, PublicFlowSummary } from "./flowModel";
import ConditionEditor from "./ConditionEditor.vue";
import FlowCallInspector from "./FlowCallInspector.vue";

interface Option { value: string | number; label: string; role?: string; category?: string }
const props = defineProps<{
  modelValue?: Record<string, EventRule>;
  flows: PublicFlowSummary[];
  currentFlowId: string;
  nodes: WorkflowNode[];
  templates?: Option[];
  resources?: Option[];
  recognizers?: Option[];
}>();
const emit = defineEmits<{
  "update:modelValue": [Record<string, EventRule>];
  "open-definition": [string];
}>();
const selected = ref("");
const newId = ref("");
const ids = computed(() => Object.keys(props.modelValue ?? {}));
const current = computed(() => (props.modelValue ?? {})[selected.value]);
const availableFlows = computed(() => props.flows.filter(flow => flow.id !== props.currentFlowId));
const hasRecognition = computed(() => Boolean(props.resources?.length || props.templates?.length));
watch(ids, values => {
  if (!values.includes(selected.value)) selected.value = values[0] ?? "";
}, { immediate: true });

function copyRules(): Record<string, EventRule> {
  return JSON.parse(JSON.stringify(props.modelValue ?? {})) as Record<string, EventRule>;
}

function updateRule(id: string, patch: Partial<EventRule>) {
  const rules = copyRules();
  rules[id] = { ...rules[id], ...patch };
  emit("update:modelValue", rules);
}
function addRule() {
  const id = newId.value.trim();
  if (!/^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/.test(id) || ids.value.includes(id) ||
      ids.value.length >= 32 || !hasRecognition.value) return;
  const rules = copyRules();
  const detect = props.resources?.length
    ? { mode: "semantic", id: String(props.resources[0].value) }
    : { mode: "template", image: String(props.templates![0].value), threshold: 0.8,
        roi: [0, 0, 900, 1600] };
  const handled = availableFlows.value.length > 0;
  rules[id] = {
    enabled: false, class: "overlay", priority: 100,
    detect, allow_nested: [],
    ...(handled
      ? { handler: { flow_id: availableFlows.value[0].id, arguments: {}, extensions: {} },
          disposition: "handled" as const, resume: { mode: "reobserve" as const } }
      : { disposition: "external_blocked" as const, reason: "EXTERNAL_BLOCKED_MAINTENANCE" }),
  };
  emit("update:modelValue", rules);
  selected.value = id;
  newId.value = "";
}
function removeRule(id: string) {
  const rules = copyRules();
  delete rules[id];
  for (const rule of Object.values(rules))
    rule.allow_nested = (rule.allow_nested ?? []).filter(value => value !== id);
  emit("update:modelValue", rules);
}
function disposition(value: "handled" | "external_blocked") {
  if (!current.value) return;
  const rules = copyRules();
  const rule = rules[selected.value];
  rule.disposition = value;
  if (value === "external_blocked") {
    delete rule.handler;
    delete rule.resume;
    rule.reason = "EXTERNAL_BLOCKED_MAINTENANCE";
  } else {
    delete rule.reason;
    rule.handler = { flow_id: availableFlows.value[0]?.id ?? "", arguments: {}, extensions: {} };
    rule.resume = { mode: "reobserve" };
  }
  emit("update:modelValue", rules);
}
function resumeMode(value: EventResume["mode"]) {
  updateRule(selected.value, { resume: value === "replan"
    ? { mode: value, node_id: props.nodes.find(node => node.data.node_type !== "end")?.id ?? "",
        guard: { mode: "semantic", id: "" } }
    : { mode: "reobserve" } });
}
function toggleNested(id: string, enabled: boolean) {
  const next = new Set(current.value?.allow_nested ?? []);
  if (enabled) next.add(id); else next.delete(id);
  updateRule(selected.value, { allow_nested: [...next] });
}
</script>

<template>
  <section class="inspector-group" aria-label="事件规则">
    <h3>事件规则</h3>
    <div class="command-row">
      <input v-model.trim="newId" aria-label="新事件 ID" placeholder="事件 ID" maxlength="64" />
      <button class="icon-button" type="button" title="新增事件规则" aria-label="新增事件规则"
        :disabled="!hasRecognition || !/^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/.test(newId) || ids.includes(newId) || ids.length >= 32"
        @click="addRule"><Plus :size="16" /></button>
    </div>
    <div v-if="ids.length" class="command-row">
      <select v-model="selected" aria-label="当前事件"><option v-for="id in ids" :key="id" :value="id">{{ id }}</option></select>
      <button class="icon-button danger" type="button" title="删除当前事件" aria-label="删除当前事件"
        @click="removeRule(selected)"><Trash2 :size="16" /></button>
    </div>
    <template v-if="current">
      <label class="check-row"><input type="checkbox" :checked="current.enabled"
        @change="updateRule(selected,{enabled:($event.target as HTMLInputElement).checked})" /><span>启用</span></label>
      <label class="field"><span>类别</span><select :value="current.class"
        @change="updateRule(selected,{class:($event.target as HTMLSelectElement).value as EventRule['class']})">
        <option value="overlay">遮挡层</option><option value="encounter">额外流程</option>
      </select></label>
      <label class="field"><span>优先级</span><input type="number" min="0" max="1000" :value="current.priority"
        @change="updateRule(selected,{priority:Number(($event.target as HTMLInputElement).value)})" /></label>
      <label class="field"><span>处理结果</span><select :value="current.disposition ?? 'handled'"
        @change="disposition(($event.target as HTMLSelectElement).value as 'handled'|'external_blocked')">
        <option value="handled">处理后返回</option><option value="external_blocked">外部阻断</option>
      </select></label>
      <ConditionEditor :model-value="current.detect" :templates="templates" :resources="resources"
        :recognizers="recognizers" @update:model-value="updateRule(selected,{detect:$event})" />
      <template v-if="current.disposition === 'external_blocked'">
        <label class="field"><span>原因码</span><input :value="current.reason ?? ''"
          @change="updateRule(selected,{reason:($event.target as HTMLInputElement).value})" /></label>
      </template>
      <template v-else>
        <FlowCallInspector v-if="current.handler" :model-value="current.handler as FlowCall"
          :flows="flows" :current-flow-id="currentFlowId"
          @update:model-value="updateRule(selected,{handler:$event})"
          @open-definition="emit('open-definition',$event)" />
        <label class="field"><span>返回方式</span><select :value="current.resume?.mode ?? 'reobserve'"
          @change="resumeMode(($event.target as HTMLSelectElement).value as EventResume['mode'])">
          <option value="reobserve">重新观察原目标</option><option value="replan">转到指定步骤</option>
        </select></label>
        <template v-if="current.resume?.mode === 'replan'">
          <label class="field"><span>恢复步骤</span><select :value="current.resume.node_id ?? ''"
            @change="updateRule(selected,{resume:{...current.resume!,node_id:($event.target as HTMLSelectElement).value}})">
            <option v-for="node in nodes.filter(item => item.data.node_type !== 'end')" :key="node.id" :value="node.id">{{ node.data.label }}</option>
          </select></label>
          <ConditionEditor :model-value="(current.resume.guard ?? {}) as JsonObject"
            :templates="templates" :resources="resources" :recognizers="recognizers"
            @update:model-value="updateRule(selected,{resume:{...current.resume!,guard:$event}})" />
        </template>
        <div v-if="ids.length > 1" class="inspector-group"><h4>允许的嵌套事件</h4>
          <label v-for="id in ids.filter(value => value !== selected && modelValue?.[value]?.enabled)" :key="id" class="check-row">
            <input type="checkbox" :checked="(current.allow_nested ?? []).includes(id)"
              @change="toggleNested(id,($event.target as HTMLInputElement).checked)" /><span>{{ id }}</span>
          </label>
        </div>
      </template>
    </template>
  </section>
</template>
