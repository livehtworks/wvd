<script setup lang="ts">

import { computed, nextTick, ref, watch } from "vue";
import {
  ArrowDown, ArrowUp, Bug, Camera, Check, ChevronDown, Copy, FolderOpen, GripVertical, Link, Link2Off, Pencil, Play, Plus, RefreshCw, ScanFace, Square, Trash2, Undo2, X,
} from "@lucide/vue";
import { useWorkbench } from "../stores/useWorkbench";
import ProfileSaveBar from "../components/ProfileSaveBar.vue";
import EnemyRulesEditor from "../components/EnemyRulesEditor.vue";
import { useSortableList } from "../composables/useSortableList";
import { resourceLocaleOptions } from "../api/types";
import type { CatalogOption, SkillSetting, StrategyGroup } from "../api/types";

const state = useWorkbench();
const emit = defineEmits<{ dirty: [value: boolean] }>();
const tab = ref<"common" | "advanced">("common");
const combatSection = ref<HTMLDetailsElement>();
const previewDialog = ref<HTMLDialogElement>();
const previewButton = ref<HTMLButtonElement>();
function closePreview() { previewDialog.value?.close(); previewButton.value?.focus(); }
const taskCategory = ref("");
const taskSelectVersion = ref(0);
const strategyQuery = ref("");
const selectedStrategyIndex = ref(0);
const renamingStrategy = ref(false);
const strategyNameDraft = ref("");
const strategyNameError = ref("");
const strategyNameInput = ref<HTMLInputElement>();
const renameButton = ref<HTMLButtonElement>();
const schemeList = ref<HTMLElement>();
const actionList = ref<HTMLElement>();
const monsterSection = ref<HTMLElement>();
const objectIds = new WeakMap<object, number>();
let nextObjectId = 0;
function rowKey(value: object) { if (!objectIds.has(value)) objectIds.set(value, ++nextObjectId); return objectIds.get(value)!; }

const tasks = computed(() => (state.catalog.tasks ?? []).filter((task) => !taskCategory.value || task.category === taskCategory.value));
const strategyNames = computed(() => state.strategies.map((item) => item.group_name));
const selectedStrategy = computed(() => state.strategies[selectedStrategyIndex.value]);
const pendingStrategyName = computed(() => renamingStrategy.value && strategyNameDraft.value.trim() !== selectedStrategy.value?.group_name);
watch(() => state.dirty || pendingStrategyName.value, (value) => emit("dirty", value), { immediate: true });
const filteredStrategies = computed(() => {
  const query = strategyQuery.value.trim().toLocaleLowerCase();
  return state.strategies
    .map((group, index) => ({ group, index }))
    .filter(({ group }) => !query || group.group_name.toLocaleLowerCase().includes(query));
});
const taskPoints = computed(() => state.selectedTask?.task_points ?? []);
useSortableList(schemeList, () => state.editLocked || renamingStrategy.value, '.strategy-item', (from, to) => {
  const source = filteredStrategies.value[from]?.group, target = filteredStrategies.value[to]?.group;
  const selected = selectedStrategy.value;
  if (!source || !target) return;
  const targetIndex = state.strategies.indexOf(target);
  state.strategies.splice(state.strategies.indexOf(source), 1);
  state.strategies.splice(targetIndex, 0, source);
  selectedStrategyIndex.value = selected ? state.strategies.indexOf(selected) : 0;
});
useSortableList(actionList, () => state.editLocked || renamingStrategy.value, '.skill-row', (from, to) => {
  const rows = selectedStrategy.value?.skill_settings;
  if (!rows) return;
  const [row] = rows.splice(from, 1); rows.splice(to, 0, row);
});
async function showMonsters() {
  tab.value = 'common';
  await nextTick();
  if (combatSection.value) combatSection.value.open = true;
  monsterSection.value?.scrollIntoView({ block: 'center', behavior: 'smooth' });
}
watch(selectedStrategy, () => {
  renamingStrategy.value = false;
  strategyNameError.value = "";
});

watch(() => state.strategies.length, (length) => {
  if (!length) selectedStrategyIndex.value = 0;
  else if (selectedStrategyIndex.value >= length) selectedStrategyIndex.value = length - 1;
});

function optionValue(option: CatalogOption) { return String(option.value); }
function optionsWithCurrent(options: CatalogOption[] | undefined, current: unknown): CatalogOption[] {
  const result = options ?? [];
  if (current === undefined || current === null || current === "" || result.some((item) => String(item.value) === String(current))) return result;
  return [{ value: String(current), label: `现有值：${String(current)}` }, ...result];
}
function asNumber(event: Event) { return Number((event.target as HTMLInputElement).value); }
function today() { return new Date().toLocaleDateString("sv-SE"); }
function markWebsiteVisit() { if (!state.editLocked && state.draft) state.draft.WEBSITE_ORG_TIME = today(); }
function switchTempleTarget() {
  if (state.editLocked) return;
  if (!state.draft) return;
  const task = state.catalog.tasks?.find((item) => item.name.includes("炉壶灵庙"));
  if (!task) { window.alert("任务目录中没有找到炉壶灵庙目标"); return; }
  void state.selectTask(task.id, { AM_REFRESH_TIME: today(), FARM_TARGET_TEXT: task.name });
}
async function reloadProfile() {
  if (state.editLocked || (state.dirty && !window.confirm("放弃当前配置中未保存的更改，并重新读取服务端？"))) return;
  await state.load();
}
async function updateTask(event: Event) {
  if (!state.draft) return;
  const value = (event.target as HTMLSelectElement).value;
  await state.selectTask(value);
  taskSelectVersion.value++;
}
function setRecovery(field: "SKIP_COMBAT_RECOVER" | "SKIP_CHEST_RECOVER", enabled: boolean) {
  if (state.editLocked) return;
  if (state.draft) state.draft[field] = !enabled;
}
function pointBindings() {
  if (!state.draft) return {};
  const current = state.draft.TASK_POINT_STRATEGY ?? {};
  if (Array.isArray(current.task_point)) {
    current.task_point = Object.fromEntries(current.task_point.map((item) => [item.point, item.strategy]));
  }
  current.task_point ??= {};
  state.draft.TASK_POINT_STRATEGY = current;
  return current.task_point as Record<string, string>;
}
function addStrategy() {
  if (state.editLocked) return;
  const base = "新方案";
  let index = 1;
  while (strategyNames.value.includes(`${base}${index}`)) index++;
  state.strategies.push({ group_name: `${base}${index}`, skill_settings: [], complete_one_as_all: false });
  selectedStrategyIndex.value = state.strategies.length - 1;
  strategyQuery.value = "";
}
function copyStrategy() {
  const group = selectedStrategy.value;
  if (state.editLocked || !group) return;
  const base = `${group.group_name} 副本`;
  let name = base, suffix = 2;
  while (strategyNames.value.includes(name)) name = `${base} ${suffix++}`;
  // 配置是 JSON 数据；深复制避免新方案与原方案共用动作行，不改原方案的名称引用。
  const copy: StrategyGroup = JSON.parse(JSON.stringify(group));
  copy.group_name = name;
  const index = selectedStrategyIndex.value + 1;
  state.strategies.splice(index, 0, copy);
  selectedStrategyIndex.value = index;
  strategyQuery.value = "";
}
function moveStrategy(offset: -1 | 1) {
  const index = selectedStrategyIndex.value, target = index + offset;
  if (state.editLocked || !selectedStrategy.value || target < 0 || target >= state.strategies.length) return;
  const [group] = state.strategies.splice(index, 1);
  state.strategies.splice(target, 0, group);
  selectedStrategyIndex.value = target;
  strategyQuery.value = "";
}
function renameStrategy(group: StrategyGroup, value: string) {
  if (state.editLocked) return;
  const old = group.group_name;
  const name = value.trim();
  if (!name || (name !== old && strategyNames.value.includes(name))) return;
  state.noteRename(old, name);
  group.group_name = name;
  if (!state.draft) return;
  if (state.draft.DEFAULT_OVERALL_STRATEGY === old) state.draft.DEFAULT_OVERALL_STRATEGY = name;
  if (state.draft.TASK_POINT_STRATEGY?.special_combat?.normal_strategy === old) state.draft.TASK_POINT_STRATEGY.special_combat.normal_strategy = name;
  if (state.draft.TASK_POINT_STRATEGY?.special_combat?.special_strategy === old) state.draft.TASK_POINT_STRATEGY.special_combat.special_strategy = name;
  for (const rule of state.draft.TASK_POINT_STRATEGY?.special_combat?.rules ?? []) if (rule.strategy === old) rule.strategy = name;
  if (state.draft.TASK_POINT_STRATEGY?.overall_strategy === old) state.draft.TASK_POINT_STRATEGY.overall_strategy = name;
  const bindings = pointBindings();
  for (const point of Object.keys(bindings)) if (bindings[point] === old) bindings[point] = name;
}
async function beginStrategyRename() {
  if (state.editLocked || !selectedStrategy.value) return;
  strategyNameDraft.value = selectedStrategy.value.group_name;
  strategyNameError.value = "";
  renamingStrategy.value = true;
  await nextTick();
  strategyNameInput.value?.focus();
  strategyNameInput.value?.select();
}
async function cancelStrategyRename() {
  renamingStrategy.value = false;
  strategyNameError.value = "";
  await nextTick();
  renameButton.value?.focus();
}
function confirmStrategyRename() {
  const group = selectedStrategy.value, name = strategyNameDraft.value.trim();
  if (state.editLocked || !group) return;
  if (!name) { strategyNameError.value = "请输入方案名称"; return; }
  if (name !== group.group_name && strategyNames.value.includes(name)) {
    strategyNameError.value = "已有同名方案，请使用其它名称";
    return;
  }
  // 仅确认后更新名称和全部引用，输入中或取消时保留原配置。
  if (name !== group.group_name) renameStrategy(group, name);
  strategyQuery.value = "";
  void cancelStrategyRename();
}
function strategyReferences(name: string) {
  if (!state.draft) return [];
  const result: string[] = [];
  if (state.draft.DEFAULT_OVERALL_STRATEGY === name) result.push("全程策略");
  if (state.draft.TASK_POINT_STRATEGY?.special_combat?.normal_strategy === name) result.push("普通敌人方案");
  if (state.draft.TASK_POINT_STRATEGY?.special_combat?.special_strategy === name) result.push("特殊敌人方案");
  for (const rule of state.draft.TASK_POINT_STRATEGY?.special_combat?.rules ?? []) if (rule.strategy === name) result.push(`怪物：${rule.name}`);
  if (state.draft.TASK_POINT_STRATEGY?.overall_strategy === name) result.push("任务覆盖策略");
  const bindings = pointBindings();
  for (const [point, strategy] of Object.entries(bindings)) if (strategy === name) result.push(`任务点：${point}`);
  return result;
}
function removeStrategy(index: number) {
  if (state.editLocked) return;
  const group = state.strategies[index];
  const references = strategyReferences(group.group_name);
  if (references.length) {
    window.alert(`该方案仍被引用：${references.join("、")}。请先修改引用。`);
    return;
  }
  if (window.confirm(`删除战斗方案“${group.group_name}”？`)) {
    state.strategies.splice(index, 1);
    if (index < selectedStrategyIndex.value) selectedStrategyIndex.value--;
    else if (index === selectedStrategyIndex.value)
      selectedStrategyIndex.value = Math.min(index, state.strategies.length - 1);
    selectedStrategyIndex.value = Math.max(0, selectedStrategyIndex.value);
    strategyQuery.value = "";
  }
}
function addSkill(group: StrategyGroup) {
  if (state.editLocked) return;
  group.skill_settings.push({ role_var: "", skill_var: "", target_var: "左上角色", skill_lvl: 1, freq_var: "用完后移除" });
}
function copySkill(group: StrategyGroup, index: number) {
  if (state.editLocked || !group.skill_settings[index]) return;
  group.skill_settings.splice(index + 1, 0, JSON.parse(JSON.stringify(group.skill_settings[index])));
}
function moveSkill(group: StrategyGroup, index: number, offset: -1 | 1) {
  const target = index + offset;
  if (state.editLocked || !group.skill_settings[index] || target < 0 || target >= group.skill_settings.length) return;
  const [skill] = group.skill_settings.splice(index, 1);
  group.skill_settings.splice(target, 0, skill);
}
function removeSkill(group: StrategyGroup, index: number) {
  if (state.editLocked) return; group.skill_settings.splice(index, 1); }
function updateSkill(skill: SkillSetting, key: keyof SkillSetting, event: Event) {
  if (state.editLocked) return;
  const input = event.target as HTMLInputElement | HTMLSelectElement;
  skill[key] = key === "skill_lvl" ? Number(input.value) : input.value;
}
</script>

<template>
  <main class="app-main workbench-page">
    <header class="page-heading">
      <div><h1>工作台</h1></div>
      <button class="button secondary" :disabled="state.editLocked" @click="reloadProfile"><Undo2 :size="16" />重新读取配置</button>
    </header>
    <div v-if="state.error && !state.feedbackScope" class="notice error" role="alert">{{ state.error }}<button v-if="/PROFILE_.*CONFLICT/.test(state.error)" class="button secondary" :disabled="state.editLocked" @click="reloadProfile">重新读取配置</button></div>
    <div v-if="state.notice && !state.feedbackScope" class="notice success" role="status">{{ state.notice }}</div>
    <div v-if="state.loading" class="loading-line"><RefreshCw :size="16" class="spinning" />正在读取服务端配置</div>

    <template v-if="state.draft">
      <section class="workbench-actions">
        <div class="section-body form-grid">
          <label class="field"><span>任务类别</span><select v-model="taskCategory" :disabled="state.saving"><option value="">全部类别</option><option v-for="item in state.catalog.task_categories ?? []" :key="optionValue(item)" :value="optionValue(item)">{{ item.label }}</option></select></label>
          <label class="field"><span>任务目标</span><select :key="taskSelectVersion" :value="state.selectionId" :disabled="state.saving" @change="updateTask"><option value="" disabled>请选择</option><option v-if="state.draft.FARM_TARGET && !tasks.some((item) => item.id === state.draft!.FARM_TARGET)" :value="state.draft.FARM_TARGET">现有值：{{ state.draft.FARM_TARGET_TEXT ?? state.draft.FARM_TARGET }}</option><option v-for="task in tasks" :key="task.id" :value="task.id">{{ task.name }}</option></select></label>

          <label class="check-field"><input v-model="state.draft.TASK_SPECIFIC_CONFIG" :disabled="state.editLocked" type="checkbox" />使用任务专用配置</label>
          <button class="button danger-inline" type="button" :disabled="!state.draft.TASK_SPECIFIC_CONFIG || state.editLocked" @click="state.clearTaskOverride"><Trash2 :size="15" />清除当前任务覆盖</button>
        <div v-if="state.draft?.FARM_TARGET === 'Scorpionesses' || state.draft?.FARM_TARGET === 'GiantBounty'" class="command-row">
          <label class="field"><span>循环模式</span><select v-model="state.repeatMode" :disabled="state.editLocked || state.runActive"><option value="forever">一直循环</option><option value="count">指定次数</option></select></label>
          <label v-if="state.repeatMode === 'count'" class="field"><span>循环次数</span><input v-model.number="state.repeatCount" type="number" min="1" max="1000000" :disabled="state.editLocked || state.runActive" /></label>
        </div>
<label class="field run-locale"><span>游戏识别语言</span><select v-model="state.resourceLocale" :disabled="state.editLocked || state.runActive"><option v-for="option in resourceLocaleOptions" :key="option.value" :value="option.value">{{ option.label }}</option></select></label><button class="button run" :disabled="state.editLocked || state.runActive || state.deviceBusy || state.dirty || !state.draft?.FARM_TARGET" @click="state.startSelectedTask"><Play :size="16" />开始任务</button>
          <span class="source-line span-2">当前来源：{{ state.envelope?.effective_source ?? (state.draft.TASK_SPECIFIC_CONFIG ? '任务覆盖' : '默认配置') }}</span>
          <details v-if="state.selectedTask?.description" class="task-description"><summary>任务说明</summary><p>{{ state.selectedTask.description }}</p></details>
        </div>
        <ProfileSaveBar label="任务设置" :dirty="state.scopeDirty.task" :saving="state.saveScope === 'task'" :locked="state.editLocked" :error="state.feedbackScope === 'task' ? state.error : ''" :notice="state.feedbackScope === 'task' ? state.notice : ''" @save="state.save('task')" @reload="reloadProfile" />
      </section>
      <nav class="config-tabs" aria-label="工作台设置">
        <button v-for="item in [{id:'common',name:'常用参数'},{id:'advanced',name:'设备与高级'}] as const" :key="item.id" :aria-label="item.name" :aria-pressed="tab === item.id" @click="tab = item.id">{{ item.name }}<span v-if="state.scopeDirty[item.id] || (item.id === 'common' && state.scopeDirty.combat)" class="dirty-dot" title="有未保存更改" aria-hidden="true" /></button>
      </nav>
      <fieldset class="editor-fields" :disabled="state.editLocked" :inert="state.editLocked">
      <details v-show="tab === 'advanced'" class="config-section" open>
        <summary><span>模拟器</span><ChevronDown :size="17" /></summary>
        <div class="section-body device-layout">
          <div class="form-grid">
            <label class="field span-2"><span>模拟器路径</span><span class="path-picker"><input v-model="state.draft.EMU_PATH" autocomplete="off" /><button class="icon-button" type="button" title="选择 MuMu 启动程序" aria-label="选择 MuMu 启动程序" @click="state.chooseEmulator"><FolderOpen :size="16" /></button></span></label>
            <label class="field"><span>ADB 地址</span><input v-model="state.draft.ADB_ADRESS" autocomplete="off" /></label>
            <label class="field"><span>实例编号</span><input :value="state.draft.EMU_INDEX" type="number" min="0" @input="state.draft.EMU_INDEX = asNumber($event)" /></label>
            <label class="check-field span-2"><input v-model="state.draft.AUTO_START_CLASH" type="checkbox" />重启后自动启动 Clash 并打开 VPN</label>
            <div class="command-row span-2">
              <button class="button primary" :disabled="state.deviceBusy || state.device?.connected" @click="state.deviceAction('connect')"><Link :size="16" />连接</button>
              <button class="button secondary" :disabled="state.deviceBusy || !state.device?.connected" @click="state.deviceAction('disconnect')"><Link2Off :size="16" />断开</button>
              <button class="button secondary" :disabled="state.deviceBusy || !state.device?.connected" @click="state.deviceAction('capture')"><Camera :size="16" />截图</button>
            </div>
          </div>
          <aside class="device-preview">
            <div class="preview-header"><span :class="['connection-dot', { online: state.device?.connected }]" />{{ state.device?.display_name ?? state.device?.state ?? '未连接' }}<small>{{ state.device?.captured_at ?? '' }}</small></div>
            <button v-if="state.device?.screenshot_url" ref="previewButton" class="screenshot-thumb" title="查看已有截图" aria-label="查看已有截图" @click="previewDialog?.show()"><img :src="state.device.screenshot_url" alt="最近一次截图" /></button>
            <dialog ref="previewDialog" class="screenshot-dialog" @cancel.prevent="closePreview" @keydown.esc.prevent="closePreview"><button class="button secondary" @click="closePreview">关闭</button><img v-if="state.device?.screenshot_url" :src="state.device.screenshot_url" alt="已有截图大图" /></dialog>
            <div v-if="!state.device?.screenshot_url" class="preview-empty">暂无已保存截图</div>
          </aside>
        </div>
      </details>



      <details v-show="tab === 'advanced'" class="config-section" open>
        <summary><span>日志与诊断</span><ChevronDown :size="17" /></summary>
        <div class="section-body form-grid">
          <label class="field"><span>日志级别</span><select v-model="state.logging.level">
            <option value="trace">追踪</option><option value="debug">调试</option>
            <option value="info">信息</option><option value="warn">警告</option><option value="error">错误</option>
            <option value="off">关闭明细</option>
          </select></label>
          <label class="field"><span>内存细采样间隔（调试级，毫秒）</span><input v-model.number="state.logging.memory_interval_ms" type="number" min="1000" max="60000" step="1000" :disabled="!state.logging.memory || !['debug', 'trace'].includes(state.logging.level)" /></label>
          <label class="check-field"><input v-model="state.logging.performance" type="checkbox" />动作耗时明细</label>
          <label class="check-field"><input v-model="state.logging.memory" type="checkbox" />内存诊断</label>
          <label class="check-field"><input v-model="state.logging.recognition" type="checkbox" />识别统计</label>
          <span class="source-line span-2">输入回执与异常证据始终保存</span>
        </div>
      </details>

      <details v-show="tab === 'common'" class="config-section exploration-section" open>
        <summary><span>探索</span><ChevronDown :size="17" /></summary>
        <div class="section-body form-grid">
          <label class="field"><span>开箱人选</span><select v-model="state.draft.WHO_WILL_OPEN_IT"><option v-for="item in optionsWithCurrent(state.catalog.chest_openers, state.draft.WHO_WILL_OPEN_IT)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select></label>
          <label class="check-field"><input v-model="state.draft.QUICK_DISARM_CHEST" type="checkbox" />快速开箱</label>
          <label class="check-field"><input :checked="!state.draft.SKIP_COMBAT_RECOVER" type="checkbox" @change="setRecovery('SKIP_COMBAT_RECOVER', ($event.target as HTMLInputElement).checked)" />战后恢复</label>
          <label class="check-field"><input :checked="!state.draft.SKIP_CHEST_RECOVER" type="checkbox" @change="setRecovery('SKIP_CHEST_RECOVER', ($event.target as HTMLInputElement).checked)" />开箱后恢复</label>
          <label class="check-field"><input v-model="state.draft.RECOVER_WHEN_BEGINNING" type="checkbox" />刚入地下城恢复</label>
          <label class="check-field"><input v-model="state.draft.ACTIVE_REST" type="checkbox" />主动旅店休息</label>
          <label class="field"><span>旅店间隔</span><input :value="state.draft.REST_INTERVEL" type="number" min="0" @input="state.draft.REST_INTERVEL = asNumber($event)" /></label>
          <label class="field"><span>善恶方向 / 剩余量</span><select v-model="state.draft.KARMA_ADJUST"><option v-for="item in optionsWithCurrent(state.catalog.karma_directions, state.draft.KARMA_ADJUST)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select></label>
          <label class="check-field"><input v-model="state.draft.RE_ASSEMBLE_PARTY" type="checkbox" />每六小时重组队伍</label>
        </div>
      </details>

      <details ref="combatSection" v-show="tab === 'common'" class="config-section battle-section" open>
        <summary><span>战斗</span><ChevronDown :size="17" /></summary>
        <div class="section-body form-grid">
          <label class="field"><span>全程策略</span><select v-model="state.draft.DEFAULT_OVERALL_STRATEGY"><option v-if="!strategyNames.includes('全自动战斗')" value="全自动战斗">全自动战斗</option><option v-for="name in strategyNames" :key="name" :value="name">{{ name }}</option></select></label>
          <label class="field"><span>任务专用模式</span><select v-model="state.draft.TASK_POINT_STRATEGY!.overall_strategy"><option value="">继承全局</option><option value="自定义任务点策略">按任务点分别设置</option><option v-for="name in strategyNames" :key="name" :value="name">{{ name }}</option></select></label>
          <div v-if="taskPoints.length && state.draft.TASK_POINT_STRATEGY!.overall_strategy === '自定义任务点策略'" class="binding-table span-2"><div class="binding-head"><span>任务点</span><span>策略</span></div><label v-for="point in taskPoints" :key="optionValue(point)" class="binding-row"><span>{{ point.label }}</span><select v-model="pointBindings()[String(point.value)]"><option value="">继承全程</option><option v-for="name in strategyNames" :key="name" :value="name">{{ name }}</option></select></label></div>
          <label class="check-field"><input v-model="state.draft.TASK_POINT_STRATEGY!.special_combat!.skull" type="checkbox" />红骷髅识别特殊敌人</label>
          <label class="check-field"><input v-model="state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait" type="checkbox" />行动栏头像识别特殊敌人</label>
          <label v-if="state.draft.TASK_POINT_STRATEGY!.special_combat!.skull || state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait" class="field"><span>普通敌人方案</span><select v-model="state.draft.TASK_POINT_STRATEGY!.special_combat!.normal_strategy"><option value="">请选择</option><option v-for="name in strategyNames" :key="name" :value="name">{{ name }}</option></select></label>
          <label v-if="state.draft.TASK_POINT_STRATEGY!.special_combat!.skull || (state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait && !state.draft.TASK_POINT_STRATEGY!.special_combat!.rules?.length)" class="field"><span>特殊敌人方案</span><select v-model="state.draft.TASK_POINT_STRATEGY!.special_combat!.special_strategy"><option value="">请选择</option><option v-for="name in strategyNames" :key="name" :value="name">{{ name }}</option></select></label>
          <label v-if="state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait && !state.draft.TASK_POINT_STRATEGY!.special_combat!.rules?.length" class="field"><span>头像模板</span><select v-model="state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait_image"><option value="combat_scorpion_portrait">蝎女头像</option><option v-if="state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait_image && state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait_image !== 'combat_scorpion_portrait'" :value="state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait_image">{{ state.draft.TASK_POINT_STRATEGY!.special_combat!.portrait_image }}</option></select></label>
          <div ref="monsterSection" class="span-2"><EnemyRulesEditor :model="state.draft.TASK_POINT_STRATEGY!.special_combat!" :strategies="strategyNames" :locked="state.editLocked" :capture="state.captureForMonster" :capture-disabled="state.runActive || !state.device?.connected" :initial-strategy="selectedStrategy?.group_name || ''" :normal-strategy="String(state.draft.DEFAULT_OVERALL_STRATEGY || '')" /></div>
        </div>
        <ProfileSaveBar class="common-preferences-savebar" label="常用参数" :dirty="state.scopeDirty.common" :saving="state.saveScope === 'common'" :locked="state.editLocked" :error="state.feedbackScope === 'common' ? state.error : ''" :notice="state.feedbackScope === 'common' ? state.notice : ''" @save="state.save('common')" @reload="reloadProfile" />

      <section class="combat-section" aria-label="战斗方案编辑">
        <div class="section-body strategy-editor">
          <div class="section-commands strategy-commands"><h3>战斗方案</h3><button class="button secondary" @click="showMonsters"><ScanFace :size="16" />怪物配置</button><label class="field compact"><span>额外重置时机</span><select v-model="state.draft.RELOAD_STRATEGY_WHEN"><option v-for="item in optionsWithCurrent(state.catalog.strategy_reload_timings, state.draft.RELOAD_STRATEGY_WHEN)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select></label></div>
          <div class="strategy-workspace">
            <aside class="strategy-sidebar" aria-label="战斗方案列表">
              <button class="button secondary strategy-create" @click="addStrategy"><Plus :size="16" />新建方案</button>
              <label class="field strategy-search"><span>查找方案</span><input v-model="strategyQuery" type="search" placeholder="输入方案名称" /></label>
              <div ref="schemeList" class="strategy-list" role="listbox" aria-label="选择战斗方案">
                <div v-for="item in filteredStrategies" :key="rowKey(item.group)" class="strategy-item" role="option" tabindex="0" :aria-label="`${item.group.group_name} ${item.group.skill_settings.length} 项`" :aria-selected="item.index === selectedStrategyIndex" @click="selectedStrategyIndex = item.index" @keydown.enter.prevent="selectedStrategyIndex = item.index" @keydown.space.prevent="selectedStrategyIndex = item.index">
                  <button class="icon-button drag-handle" :disabled="state.editLocked || renamingStrategy" :aria-label="`拖动方案 ${item.group.group_name}`" title="拖动排序" @click.stop><GripVertical :size="15" /></button><span class="row-number" aria-hidden="true">{{ item.index + 1 }}</span>
                  <span>{{ item.group.group_name }}</span><small>{{ item.group.skill_settings.length }} 项</small>
                </div>
                <p v-if="!filteredStrategies.length" class="strategy-list-empty">{{ state.strategies.length ? '没有匹配方案' : '尚无方案' }}</p>
              </div>
            </aside>
            <article v-if="selectedStrategy" class="strategy-group">
              <header>
                <form v-if="renamingStrategy" class="strategy-rename" @submit.prevent="confirmStrategyRename" @keydown.esc.prevent="cancelStrategyRename">
                  <input ref="strategyNameInput" v-model="strategyNameDraft" class="strategy-name" aria-label="方案名称" :aria-invalid="!!strategyNameError" :aria-describedby="strategyNameError ? 'strategy-name-error' : undefined" @input="strategyNameError = ''" />
                  <button class="icon-button" type="submit" title="确认重命名" aria-label="确认重命名"><Check :size="16" /></button>
                  <button class="icon-button" type="button" title="取消重命名" aria-label="取消重命名" @click="cancelStrategyRename"><X :size="16" /></button>
                  <p v-if="strategyNameError" id="strategy-name-error" class="strategy-name-error" role="alert">{{ strategyNameError }}</p>
                </form>
                <div v-else class="strategy-title-row">
                  <span class="strategy-title">{{ selectedStrategy.group_name }}</span>
                  <button ref="renameButton" class="button secondary" type="button" @click="beginStrategyRename"><Pencil :size="15" />重命名</button>
                </div>
                <div class="strategy-actions" role="group" aria-label="方案操作">
                  <button v-if="!state.debugActive" class="button secondary" :disabled="state.editLocked || state.runActive || renamingStrategy" @click="state.debugStrategy(selectedStrategy.group_name)"><Bug :size="16" />开始调试</button>
                  <button v-else class="button danger-inline" @click="state.requestStop"><Square :size="15" />停止调试</button>
                  <button class="icon-button" title="复制方案" aria-label="复制方案" @click="copyStrategy"><Copy :size="16" /></button>
                  <button class="icon-button" title="上移方案" aria-label="上移方案" :disabled="selectedStrategyIndex === 0" @click="moveStrategy(-1)"><ArrowUp :size="16" /></button>
                  <button class="icon-button" title="下移方案" aria-label="下移方案" :disabled="selectedStrategyIndex === state.strategies.length - 1" @click="moveStrategy(1)"><ArrowDown :size="16" /></button>
                  <button class="icon-button danger" title="删除方案" :aria-label="`删除方案 ${selectedStrategy.group_name}`" @click="removeStrategy(selectedStrategyIndex)"><Trash2 :size="16" /></button>
                </div>
                <label class="check-field strategy-completion" title="完成任一动作后清空本轮方案，优先于单行的重复设置"><input v-model="selectedStrategy.complete_one_as_all" type="checkbox" />任一完成即结束方案（优先于重复）</label>
              </header>
              <div class="skill-table"><div class="skill-head"><span>序号</span><span>角色</span><span>技能</span><span>等级</span><span>目标</span><span>频次</span><span>操作</span></div><div ref="actionList" class="skill-rows"><div v-for="(skill, skillIndex) in selectedStrategy.skill_settings" :key="rowKey(skill)" class="skill-row">
                <div class="row-order"><button class="icon-button drag-handle" :disabled="state.editLocked || renamingStrategy" :aria-label="`拖动第${skillIndex + 1}行`" title="拖动排序"><GripVertical :size="15" /></button><span class="row-number">{{ skillIndex + 1 }}</span></div>
                <select :value="skill.role_var" aria-label="角色" @change="updateSkill(skill, 'role_var', $event)"><option value="">默认行为</option><option v-for="item in optionsWithCurrent(state.catalog.roles, skill.role_var)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <select :value="skill.skill_var" aria-label="技能" @change="updateSkill(skill, 'skill_var', $event)"><option value="">自动战斗</option><option v-for="item in optionsWithCurrent(state.catalog.skills, skill.skill_var)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <select :value="skill.skill_lvl" aria-label="技能等级" @change="updateSkill(skill, 'skill_lvl', $event)"><option v-for="item in optionsWithCurrent(state.catalog.skill_levels, skill.skill_lvl)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <select :value="skill.target_var ?? '左上角色'" aria-label="技能目标" title="友方技能的队伍位置；不改变敌方选敌" @change="updateSkill(skill, 'target_var', $event)"><option v-for="item in state.catalog.skill_targets" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <select :value="skill.freq_var ?? '用完后移除'" aria-label="技能频次" title="重复行在角色下次行动时继续使用；一次性技能应排在重复行前" @change="updateSkill(skill, 'freq_var', $event)"><option v-for="item in state.catalog.skill_frequencies" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <div class="skill-actions" role="group" :aria-label="`第${skillIndex + 1}行操作`">
                  <button class="icon-button" title="复制角色配置" aria-label="复制角色配置" @click="copySkill(selectedStrategy, skillIndex)"><Copy :size="15" /></button>
                  <button class="icon-button" title="上移角色配置" aria-label="上移角色配置" :disabled="skillIndex === 0" @click="moveSkill(selectedStrategy, skillIndex, -1)"><ArrowUp :size="15" /></button>
                  <button class="icon-button" title="下移角色配置" aria-label="下移角色配置" :disabled="skillIndex === selectedStrategy.skill_settings.length - 1" @click="moveSkill(selectedStrategy, skillIndex, 1)"><ArrowDown :size="15" /></button>
                  <button class="icon-button danger" title="删除技能行" aria-label="删除技能行" @click="removeSkill(selectedStrategy, skillIndex)"><Trash2 :size="15" /></button>
                </div>
              </div></div></div>
              <button class="text-command" @click="addSkill(selectedStrategy)"><Plus :size="15" />新增角色技能</button>
            </article>
            <div v-else class="strategy-empty"><p>尚无战斗方案</p><button class="button secondary" @click="addStrategy"><Plus :size="16" />新建方案</button></div>
          </div>
          <ProfileSaveBar label="战斗方案" :dirty="state.scopeDirty.combat" :saving="state.saveScope === 'combat'" :locked="state.editLocked || renamingStrategy" :pending-name="pendingStrategyName" :error="state.feedbackScope === 'combat' ? state.error : ''" :notice="state.feedbackScope === 'combat' ? state.notice : ''" @save="state.save('combat')" @reload="reloadProfile" />
        </div>
      </section>
      </details>

      <details v-show="tab === 'advanced'" class="config-section" open>
        <summary><span>日常 / 周常</span><ChevronDown :size="17" /></summary>
        <div class="section-body daily-layout">
          <div class="daily-actions"><a class="button secondary" href="https://store.wizardry.info/" target="_blank" rel="noreferrer" @click="markWebsiteVisit">领取 50 钻（旧）</a><a class="button secondary" href="https://webstore.wizardry.info/" target="_blank" rel="noreferrer" @click="markWebsiteVisit">领取 50 钻（新）</a><button class="button secondary" @click="switchTempleTarget">灵庙已刷新，切换目标</button></div>
          <div class="form-grid"><label class="field"><span>官网领取记录</span><input v-model="state.draft.WEBSITE_ORG_TIME" type="date" /></label><label class="field"><span>灵庙切换记录</span><input v-model="state.draft.AM_REFRESH_TIME" type="date" /></label><label class="field"><span>界面语言</span><select v-model="state.draft.LANGUAGE"><option value="zh_CN">简体中文</option><option value="en_US">English</option><option v-if="state.draft.LANGUAGE && !['zh_CN','en_US'].includes(state.draft.LANGUAGE)" :value="state.draft.LANGUAGE">现有值：{{ state.draft.LANGUAGE }}</option></select></label></div>
        </div>
      </details>

      <details v-show="tab === 'advanced'" class="config-section" open>
        <summary><span>高级</span><ChevronDown :size="17" /></summary>
        <div class="section-body form-grid">
          <label class="check-field"><input v-model="state.draft.ACTIVE_BEG_MONEY" type="checkbox" />自动要钱</label>
          <label class="check-field"><input v-model="state.draft.ACTIVE_ROYALSUITE_REST" type="checkbox" />豪华房</label>
          <label class="check-field"><input v-model="state.draft.ACTIVE_TRIUMPH" type="checkbox" />凯旋</label>
          <label class="check-field"><input v-model="state.draft.ACTIVE_BEAUTIFUL_ORE" type="checkbox" />美丽矿石的真相</label>
          <label class="check-field"><input v-model="state.draft.ACTIVE_CSC" type="checkbox" />因果调整</label>
          <label class="check-field"><input v-model="state.draft.BYPASS_THE_WALL" type="checkbox" />空气墙绕行</label>
          <label class="field"><span>定位失败阈值</span><input :value="state.draft.MAX_TRY_LIMIT" type="number" min="25" @input="state.draft.MAX_TRY_LIMIT = asNumber($event)" /></label>
          <label class="field"><span>重启阈值</span><input :value="state.draft.MAX_CRASH_LIMIT" type="number" min="10" @input="state.draft.MAX_CRASH_LIMIT = asNumber($event)" /></label>
        </div>
      </details>


      </fieldset>
      <ProfileSaveBar v-if="tab === 'advanced'" label="设备与高级" :dirty="state.scopeDirty.advanced" :saving="state.saveScope === 'advanced'" :locked="state.editLocked" :error="state.feedbackScope === 'advanced' ? state.error : ''" :notice="state.feedbackScope === 'advanced' ? state.notice : ''" @save="state.save('advanced')" @reload="reloadProfile" />
    </template>
  </main>
</template>
