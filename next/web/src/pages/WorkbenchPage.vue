<script setup lang="ts">
import { computed, ref, watch } from "vue";
import {
  Camera, ChevronDown, CircleStop, FolderOpen, Link, Link2Off, Play, Plus, RefreshCw, Save, Trash2, Undo2,
} from "@lucide/vue";
import { useWorkbench } from "../stores/useWorkbench";
import type { CatalogOption, SkillSetting, StrategyGroup } from "../api/types";

const state = useWorkbench();
const emit = defineEmits<{ dirty: [value: boolean] }>();
watch(() => state.dirty, (value) => emit("dirty", value), { immediate: true });
const taskCategory = ref("");
const taskSelectVersion = ref(0);
const strategyQuery = ref("");
const selectedStrategyIndex = ref(0);

const tasks = computed(() => (state.catalog.tasks ?? []).filter((task) => !taskCategory.value || task.category === taskCategory.value));
const strategyNames = computed(() => state.strategies.map((item) => item.group_name));
const selectedStrategy = computed(() => state.strategies[selectedStrategyIndex.value]);
const filteredStrategies = computed(() => {
  const query = strategyQuery.value.trim().toLocaleLowerCase();
  return state.strategies
    .map((group, index) => ({ group, index }))
    .filter(({ group }) => !query || group.group_name.toLocaleLowerCase().includes(query));
});
const taskPoints = computed(() => state.selectedTask?.task_points ?? []);

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
function markWebsiteVisit() { if (state.draft) state.draft.WEBSITE_ORG_TIME = today(); }
function switchTempleTarget() {
  if (!state.draft) return;
  const task = state.catalog.tasks?.find((item) => item.name.includes("炉壶灵庙"));
  if (!task) { window.alert("任务目录中没有找到炉壶灵庙目标"); return; }
  state.draft.FARM_TARGET = task.id;
  state.draft.FARM_TARGET_TEXT = task.name;
  state.draft.AM_REFRESH_TIME = today();
}
async function updateTask(event: Event) {
  if (!state.draft) return;
  const value = (event.target as HTMLSelectElement).value;
  if (state.dirty && value !== state.draft.FARM_TARGET) {
    const discard = window.confirm("当前配置有未保存更改。放弃这些更改后切换任务？");
    if (!discard) { taskSelectVersion.value++; return; }
    state.revert();
  }
  if (!state.draft) return;
  await state.selectTask(value);
}
function setRecovery(field: "SKIP_COMBAT_RECOVER" | "SKIP_CHEST_RECOVER", enabled: boolean) {
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
  const base = "新方案";
  let index = 1;
  while (strategyNames.value.includes(`${base}${index}`)) index++;
  state.strategies.push({ group_name: `${base}${index}`, skill_settings: [], complete_one_as_all: false });
  selectedStrategyIndex.value = state.strategies.length - 1;
  strategyQuery.value = "";
}
function renameStrategy(group: StrategyGroup, value: string) {
  const old = group.group_name;
  const name = value.trim();
  if (!name || (name !== old && strategyNames.value.includes(name))) return;
  group.group_name = name;
  if (!state.draft) return;
  if (state.draft.DEFAULT_OVERALL_STRATEGY === old) state.draft.DEFAULT_OVERALL_STRATEGY = name;
  if (state.draft.TASK_POINT_STRATEGY?.overall_strategy === old) state.draft.TASK_POINT_STRATEGY.overall_strategy = name;
  const bindings = pointBindings();
  for (const point of Object.keys(bindings)) if (bindings[point] === old) bindings[point] = name;
}
function strategyReferences(name: string) {
  if (!state.draft) return [];
  const result: string[] = [];
  if (state.draft.DEFAULT_OVERALL_STRATEGY === name) result.push("全程策略");
  if (state.draft.TASK_POINT_STRATEGY?.overall_strategy === name) result.push("任务覆盖策略");
  const bindings = pointBindings();
  for (const [point, strategy] of Object.entries(bindings)) if (strategy === name) result.push(`任务点：${point}`);
  return result;
}
function removeStrategy(index: number) {
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
  group.skill_settings.push({ role_var: "", skill_var: "", target_var: "", skill_lvl: 1, freq_var: "" });
}
function removeSkill(group: StrategyGroup, index: number) { group.skill_settings.splice(index, 1); }
function updateSkill(skill: SkillSetting, key: keyof SkillSetting, event: Event) {
  const input = event.target as HTMLInputElement | HTMLSelectElement;
  skill[key] = key === "skill_lvl" ? Number(input.value) : input.value;
}
</script>

<template>
  <main class="app-main workbench-page">
    <header class="page-heading">
      <div><h1>工作台</h1><p>配置、设备和运行状态均来自本地服务</p></div>
      <div class="command-row">
        <span v-if="state.dirty" class="status-chip warning">有未保存更改</span>
        <button class="button secondary" :disabled="!state.dirty || state.saving" @click="state.revert"><Undo2 :size="16" />重载</button>
        <button class="button primary" :disabled="!state.dirty || state.saving" @click="state.save"><Save :size="16" />保存配置</button>
      </div>
    </header>
    <div v-if="state.error" class="notice error" role="alert">{{ state.error }}</div>
    <div v-if="state.notice" class="notice success" role="status">{{ state.notice }}</div>
    <div v-if="state.loading" class="loading-line"><RefreshCw :size="16" class="spinning" />正在读取服务端配置</div>

    <template v-if="state.draft">
      <details class="config-section" open>
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
            <img v-if="state.device?.screenshot_url" :src="state.device.screenshot_url" alt="当前模拟器截图" />
            <div v-else class="preview-empty">连接后可获取当前画面</div>
          </aside>
        </div>
      </details>

      <details class="config-section" open>
        <summary><span>目标</span><ChevronDown :size="17" /></summary>
        <div class="section-body form-grid">
          <label class="field"><span>任务类别</span><select v-model="taskCategory"><option value="">全部类别</option><option v-for="item in state.catalog.task_categories ?? []" :key="optionValue(item)" :value="optionValue(item)">{{ item.label }}</option></select></label>
          <label class="field"><span>任务目标</span><select :key="taskSelectVersion" :value="state.draft.FARM_TARGET" @change="updateTask"><option value="" disabled>请选择</option><option v-if="state.draft.FARM_TARGET && !tasks.some((item) => item.id === state.draft!.FARM_TARGET)" :value="state.draft.FARM_TARGET">现有值：{{ state.draft.FARM_TARGET_TEXT ?? state.draft.FARM_TARGET }}</option><option v-for="task in tasks" :key="task.id" :value="task.id">{{ task.name }}</option></select></label>
          <p v-if="state.selectedTask?.description" class="field-help span-2">{{ state.selectedTask.description }}</p>
          <label class="check-field"><input v-model="state.draft.TASK_SPECIFIC_CONFIG" type="checkbox" />使用任务专用配置</label>
          <button class="button danger-inline" type="button" :disabled="!state.draft.TASK_SPECIFIC_CONFIG || state.saving" @click="state.clearTaskOverride"><Trash2 :size="15" />清除当前任务覆盖</button>
          <span class="source-line span-2">当前来源：{{ state.envelope?.effective_source ?? (state.draft.TASK_SPECIFIC_CONFIG ? '任务覆盖' : '默认配置') }}</span>
        </div>
      </details>

      <details class="config-section" open>
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

      <details class="config-section" open>
        <summary><span>战斗</span><ChevronDown :size="17" /></summary>
        <div class="section-body form-grid">
          <label class="field"><span>全程策略</span><select v-model="state.draft.DEFAULT_OVERALL_STRATEGY"><option v-if="!strategyNames.includes('全自动战斗')" value="全自动战斗">全自动战斗</option><option v-for="name in strategyNames" :key="name" :value="name">{{ name }}</option></select></label>
          <label class="field"><span>任务专用模式</span><select v-model="state.draft.TASK_POINT_STRATEGY!.overall_strategy"><option value="">继承全局</option><option value="自定义任务点策略">按任务点分别设置</option><option v-for="name in strategyNames" :key="name" :value="name">{{ name }}</option></select></label>
          <div v-if="taskPoints.length && state.draft.TASK_POINT_STRATEGY!.overall_strategy === '自定义任务点策略'" class="binding-table span-2"><div class="binding-head"><span>任务点</span><span>策略</span></div><label v-for="point in taskPoints" :key="optionValue(point)" class="binding-row"><span>{{ point.label }}</span><select v-model="pointBindings()[String(point.value)]"><option value="">继承全程</option><option v-for="name in strategyNames" :key="name" :value="name">{{ name }}</option></select></label></div>
        </div>
      </details>

      <details class="config-section" open>
        <summary><span>战斗方案</span><ChevronDown :size="17" /></summary>
        <div class="section-body strategy-editor">
          <div class="section-commands strategy-commands"><label class="field compact"><span>额外重置时机</span><select v-model="state.draft.RELOAD_STRATEGY_WHEN"><option v-for="item in optionsWithCurrent(state.catalog.strategy_reload_timings, state.draft.RELOAD_STRATEGY_WHEN)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select></label></div>
          <div class="strategy-workspace">
            <aside class="strategy-sidebar" aria-label="战斗方案列表">
              <button class="button secondary strategy-create" @click="addStrategy"><Plus :size="16" />新建方案</button>
              <label class="field strategy-search"><span>查找方案</span><input v-model="strategyQuery" type="search" placeholder="输入方案名称" /></label>
              <div class="strategy-list" role="listbox" aria-label="选择战斗方案">
                <button v-for="item in filteredStrategies" :key="item.index" type="button" role="option" :aria-selected="item.index === selectedStrategyIndex" @click="selectedStrategyIndex = item.index">
                  <span>{{ item.group.group_name }}</span><small>{{ item.group.skill_settings.length }} 项</small>
                </button>
                <p v-if="!filteredStrategies.length" class="strategy-list-empty">{{ state.strategies.length ? '没有匹配方案' : '尚无方案' }}</p>
              </div>
            </aside>
            <article v-if="selectedStrategy" class="strategy-group">
              <header><input class="strategy-name" :value="selectedStrategy.group_name" aria-label="方案名称" @input="renameStrategy(selectedStrategy, ($event.target as HTMLInputElement).value)" /><label class="check-field"><input v-model="selectedStrategy.complete_one_as_all" type="checkbox" />释放任一即视为完成</label><button class="icon-button danger" title="删除方案" :aria-label="`删除方案 ${selectedStrategy.group_name}`" @click="removeStrategy(selectedStrategyIndex)"><Trash2 :size="16" /></button></header>
              <div class="skill-table"><div class="skill-head"><span>角色</span><span>技能</span><span>等级</span><span>目标</span><span>频次</span><span></span></div><div v-for="(skill, skillIndex) in selectedStrategy.skill_settings" :key="skillIndex" class="skill-row">
                <select :value="skill.role_var" aria-label="角色" @change="updateSkill(skill, 'role_var', $event)"><option value="">默认行为</option><option v-for="item in optionsWithCurrent(state.catalog.roles, skill.role_var)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <select :value="skill.skill_var" aria-label="技能" @change="updateSkill(skill, 'skill_var', $event)"><option value="">自动战斗</option><option v-for="item in optionsWithCurrent(state.catalog.skills, skill.skill_var)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <select :value="skill.skill_lvl" aria-label="技能等级" @change="updateSkill(skill, 'skill_lvl', $event)"><option v-for="item in optionsWithCurrent(state.catalog.skill_levels, skill.skill_lvl)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <select :value="skill.target_var" aria-label="技能目标" @change="updateSkill(skill, 'target_var', $event)"><option value="">默认</option><option v-for="item in optionsWithCurrent(state.catalog.skill_targets, skill.target_var)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <select :value="skill.freq_var" aria-label="技能频次" @change="updateSkill(skill, 'freq_var', $event)"><option value="">默认</option><option v-for="item in optionsWithCurrent(state.catalog.skill_frequencies, skill.freq_var)" :key="optionValue(item)" :value="item.value">{{ item.label }}</option></select>
                <button class="icon-button danger" title="删除技能行" aria-label="删除技能行" @click="removeSkill(selectedStrategy, skillIndex)"><Trash2 :size="15" /></button>
              </div></div>
              <button class="text-command" @click="addSkill(selectedStrategy)"><Plus :size="15" />新增角色技能</button>
            </article>
            <div v-else class="strategy-empty"><p>尚无战斗方案</p><button class="button secondary" @click="addStrategy"><Plus :size="16" />新建方案</button></div>
          </div>
        </div>
      </details>

      <details class="config-section">
        <summary><span>日常 / 周常</span><ChevronDown :size="17" /></summary>
        <div class="section-body daily-layout">
          <div class="daily-actions"><a class="button secondary" href="https://store.wizardry.info/" target="_blank" rel="noreferrer" @click="markWebsiteVisit">领取 50 钻（旧）</a><a class="button secondary" href="https://webstore.wizardry.info/" target="_blank" rel="noreferrer" @click="markWebsiteVisit">领取 50 钻（新）</a><button class="button secondary" @click="switchTempleTarget">灵庙已刷新，切换目标</button></div>
          <div class="form-grid"><label class="field"><span>官网领取记录</span><input v-model="state.draft.WEBSITE_ORG_TIME" type="date" /></label><label class="field"><span>灵庙切换记录</span><input v-model="state.draft.AM_REFRESH_TIME" type="date" /></label><label class="field"><span>界面语言</span><select v-model="state.draft.LANGUAGE"><option value="zh_CN">简体中文</option><option value="en_US">English</option><option v-if="state.draft.LANGUAGE && !['zh_CN','en_US'].includes(state.draft.LANGUAGE)" :value="state.draft.LANGUAGE">现有值：{{ state.draft.LANGUAGE }}</option></select></label></div>
        </div>
      </details>

      <details class="config-section">
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

      <section class="run-section" aria-label="运行结果">
        <header><div><h2>运行结果</h2><span>{{ state.run?.run_id ?? '当前没有运行' }}</span></div><div class="command-row"><button class="button run" :disabled="state.runActive || state.dirty || !state.device?.connected || !state.draft?.FARM_TARGET" @click="state.startSelectedTask"><Play :size="16" />开始任务</button><button class="button danger" :disabled="!state.runActive" @click="state.requestStop"><CircleStop :size="16" />停止</button></div></header>
        <div v-if="state.error" class="notice error" role="alert">{{ state.error }}</div>
        <div class="run-grid"><div><span>状态</span><strong>{{ state.run?.state ?? 'Idle' }}</strong></div><div><span>任务 / 步骤</span><strong>{{ state.run?.task_name ?? '—' }} / {{ state.run?.step_name ?? '—' }}</strong></div><div><span>耗时</span><strong>{{ state.run?.elapsed_seconds ?? 0 }} 秒</strong></div><div><span>结果</span><strong>{{ state.run?.result ?? '—' }}</strong></div></div>
        <div v-if="state.run?.error_code || state.run?.message" class="notice error">{{ state.run.error_code }}: {{ state.run.message }}</div>
        <div v-if="state.run?.statistics" class="statistics"><span v-for="(value, key) in state.run.statistics" :key="key"><small>{{ key }}</small><strong>{{ value }}</strong></span></div>
        <div v-if="state.run?.diagnostics?.length" class="diagnostics"><figure v-for="item in state.run.diagnostics" :key="item.id ?? item.image_url ?? item.label"><img v-if="item.image_url" :src="item.image_url" :alt="item.label ?? '诊断截图'" /><div v-else class="diagnostic-placeholder">{{ item.status ?? '未保存图片' }}</div><figcaption><strong>{{ item.label ?? item.id }}</strong><span v-if="item.stage || item.node_id">{{ item.stage ?? '阶段未知' }} · {{ item.node_id ?? '节点未知' }}</span><span v-if="item.frame_age_ms !== undefined">帧龄 {{ item.frame_age_ms }} ms</span><span v-if="item.error">{{ item.error }}</span></figcaption></figure></div>
      </section>
    </template>
  </main>
</template>
