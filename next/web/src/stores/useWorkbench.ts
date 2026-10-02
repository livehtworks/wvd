import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import { useRunSession } from "./useRunSession";
import {
  captureDevice, connectDevice, disconnectDevice, formatApiError, readCatalog,
  readProfile, readTaskProfile, saveProfile,
  startTask, selectEmulator,
} from "../api/client";
import type { Catalog, LoggingSettings, ProfileEnvelope, ResourceLocale, StrategyGroup, WvdProfile } from "../api/types";

// 配置全部是 JSON 数据。JSON 往返可安全解开 Vue 的响应式 Proxy；
// structuredClone 直接接收 Proxy 会在真实浏览器中抛 DataCloneError。
const clone = <T>(value: T): T => JSON.parse(JSON.stringify(value)) as T;
const signature = (value: unknown) => JSON.stringify(value);
const defaultLogging = (): LoggingSettings => ({
  schema: 1, level: "info", performance: true, memory: true,
  recognition: false, memory_interval_ms: 1000,
});

function normalizeEnvelope(value: ProfileEnvelope | WvdProfile): ProfileEnvelope {
  if ("profile" in value && typeof value.profile === "object") return value as ProfileEnvelope;
  return { profile: value as WvdProfile };
}

export function readStrategies(profile: WvdProfile): StrategyGroup[] {
  if (Array.isArray(profile.STRATEGY)) return profile.STRATEGY as StrategyGroup[];
  return Object.entries(profile.STRATEGY ?? {}).map(([group_name, value]) => ({
    group_name,
    skill_settings: Array.isArray(value.skill_settings) ? value.skill_settings : [],
    ...value,
  }));
}

export function writeStrategies(profile: WvdProfile, groups: StrategyGroup[]) {
  profile.STRATEGY = groups;
}

function ensureCombatSettings(profile: WvdProfile) {
  profile.TASK_POINT_STRATEGY ??= { overall_strategy: "", task_point: {} };
  const points = profile.TASK_POINT_STRATEGY;
  if (Array.isArray(points.task_point)) points.task_point = Object.fromEntries(points.task_point.map(item => [item.point, item.strategy]));
  points.task_point ??= {};
  profile.TASK_POINT_STRATEGY.special_combat ??= {
    skull: false, portrait: false, portrait_image: "combat_scorpion_portrait",
    normal_strategy: "", special_strategy: "",
  };
}

export function useWorkbench() {
  const session = useRunSession();
  let alive = true;
  let writeGeneration = 0;
  let selectionSequence = 0;
  const taskLoading = ref(false);
  const selectionId = ref("");
  const persisted = ref<ProfileEnvelope>();
  const savedLocale = typeof window !== "undefined" ? window.localStorage.getItem("wvd.gameResourceLocale") : null;
  const resourceLocale = ref<ResourceLocale>(
    savedLocale === "" || savedLocale === "en" || savedLocale === "zh-Hant" ||
    savedLocale === "zh-Hans" || savedLocale === "ja" ? savedLocale : "zh-Hant",
  );
  watch(resourceLocale, (value) => window.localStorage.setItem("wvd.gameResourceLocale", value));
  const envelope = ref<ProfileEnvelope>();
  const draft = ref<WvdProfile>();
  const logging = ref<LoggingSettings>(defaultLogging());
  const savedSignature = ref("");
  const catalog = ref<Catalog>({});
  const device = computed(() => session.device);
  const run = computed(() => session.run);
  const starting = computed(() => session.starting);
  const repeatMode = ref<'forever' | 'count'>('count');
  const repeatCount = ref(1);
  const loading = ref(false);
  const saving = ref(false);
  const deviceBusy = ref(false);
  const error = ref("");
  const notice = ref("");
  const strategies = ref<StrategyGroup[]>([]);
  const strategyRenames = ref<Record<string, string>>({});
  const editLocked = computed(() => saving.value || loading.value || taskLoading.value);
  function normalized(profile: WvdProfile): WvdProfile {
    const value = clone(profile);
    ensureCombatSettings(value);
    writeStrategies(value, readStrategies(value));
    return value;
  }
  function setDraft(profile: WvdProfile) {
    draft.value = normalized(profile);
    strategies.value = readStrategies(draft.value);
    selectionId.value = draft.value.FARM_TARGET ?? "";
    strategyRenames.value = {};
  }
  function acceptSaved(value: ProfileEnvelope) {
    persisted.value = clone(value); envelope.value = clone(value);
    setDraft(value.profile);
    logging.value = clone(value.logging ?? defaultLogging());
    savedSignature.value = signature({ profile: normalized(value.profile), logging: logging.value });
  }
  function beginWrite() {
    if (editLocked.value) return;
    saving.value = true; session.writing.workbench = true;
    return ++writeGeneration;
  }
  function owns(id: number) { return alive && writeGeneration === id; }
  function endWrite(id: number) {
    if (owns(id)) { saving.value = false; session.writing.workbench = false; }
  }
  function noteRename(oldName: string, newName: string) {
    if (editLocked.value) return;
    const original = Object.keys(strategyRenames.value).find((key) => strategyRenames.value[key] === oldName);
    if (original) strategyRenames.value[original] = newName;
    else if (envelope.value && readStrategies(envelope.value.profile).some((group) => group.group_name === oldName))
      strategyRenames.value[oldName] = newName;
  }

  const dirty = computed(() => Boolean(draft.value) &&
    signature({ profile: draft.value, logging: logging.value }) !== savedSignature.value);
  const runActive = computed(() => !session.canStart);
  const runLabel = computed(() => session.label);
  const runError = computed(() => session.error);
  const selectedTask = computed(() => catalog.value.tasks?.find((task) => task.id === draft.value?.FARM_TARGET));

  async function load() {
    if (editLocked.value) return;
    loading.value = true;
    error.value = "";
    try {
      const [profileValue, catalogValue] = await Promise.all([
        readProfile(), readCatalog(),
      ]);
      if (!alive) return;
      acceptSaved(normalizeEnvelope(profileValue));
      catalog.value = catalogValue;
    } catch (reason) {
      if (alive) error.value = formatApiError(reason);
    } finally {
      if (alive) loading.value = false;
    }
  }

  async function save() {
    if (!draft.value || !persisted.value) return;
    const id = beginWrite(); if (id === undefined) return;
    const payload = clone({ ...persisted.value, profile: draft.value, logging: logging.value,
      strategy_renames: strategyRenames.value });
    error.value = "";
    notice.value = "";
    try {
      const special = draft.value.TASK_POINT_STRATEGY?.special_combat;
      if (special?.skull || special?.portrait) {
        const names = new Set(strategies.value.map((group) => group.group_name));
        if (!names.has(special.normal_strategy) || !names.has(special.special_strategy))
          throw new Error("请先选择普通敌人和特殊敌人的战斗方案");
      }
      const saved = normalizeEnvelope(await saveProfile(payload));
      if (!owns(id)) return;
      acceptSaved(saved);
      notice.value = "配置已由服务端保存";
    } catch (reason) {
      if (owns(id)) error.value = formatApiError(reason);
    } finally {
      endWrite(id);
    }
  }

  async function clearTaskOverride() {
    if (!draft.value || !persisted.value || !draft.value.FARM_TARGET || editLocked.value) return;
    if (dirty.value) {
      error.value = "PROFILE_UNSAVED: 请先保存或重载当前更改，再清除任务覆盖";
      return;
    }
    const id = beginWrite(); if (id === undefined) return;
    const payload = clone({ ...persisted.value, profile: draft.value,
      operation: "clear_task_override", task_id: draft.value.FARM_TARGET });
    error.value = "";
    try {
      const saved = normalizeEnvelope(await saveProfile(payload));
      if (!owns(id)) return;
      acceptSaved(saved);
      notice.value = "当前任务覆盖已清除";
    } catch (reason) { if (owns(id)) error.value = formatApiError(reason); }
    finally { endWrite(id); }
  }

  function revert() {
    if (!persisted.value || editLocked.value) return;
    acceptSaved(persisted.value);
    notice.value = "已恢复为服务端保存版本";
  }

  async function selectTask(taskId: string, patch: Partial<WvdProfile> = {}) {
    if (!draft.value || saving.value || loading.value) return;
    if (!taskLoading.value && dirty.value && !window.confirm("放弃当前配置中未保存的更改？")) return;
    const seq = ++selectionSequence, generation = writeGeneration;
    const valid = () => alive && seq === selectionSequence && generation === writeGeneration;
    selectionId.value = taskId; taskLoading.value = true;
    error.value = "";
    try {
      const selected = normalizeEnvelope(await readTaskProfile(taskId));
      if (!valid()) return;
      if (selected.revision !== persisted.value?.revision) throw new Error("PROFILE_REVISION_CONFLICT: 服务端配置已变化，请重载后再选择任务");
      setDraft({ ...selected.profile, ...patch, FARM_TARGET: taskId });
      logging.value = clone(selected.logging ?? defaultLogging());
      envelope.value = selected;
      notice.value = selected.task_override_active ? "已载入该任务的专用配置" : "已载入默认配置；保存后切换任务";
    } catch (reason) {
      if (valid()) { error.value = formatApiError(reason); selectionId.value = draft.value?.FARM_TARGET ?? ""; }
    } finally { if (valid()) taskLoading.value = false; }
  }

  async function deviceAction(action: "connect" | "disconnect" | "capture") {
    if (!draft.value || editLocked.value || deviceBusy.value) return;
    const profile = clone(draft.value);
    deviceBusy.value = true;
    error.value = "";
    try {
      await session.deviceCommand(() => action === "connect"
        ? connectDevice({
          emulator_path: profile.EMU_PATH,
          adb_address: profile.ADB_ADRESS,
          emulator_index: profile.EMU_INDEX,
          auto_start_clash: profile.AUTO_START_CLASH,
        })
        : action === "disconnect" ? disconnectDevice() : captureDevice());
    } catch (reason) {
      if (alive) error.value = formatApiError(reason);
    } finally {
      if (alive) deviceBusy.value = false;
    }
  }

  async function chooseEmulator() {
    if (!draft.value) return;
    const id = beginWrite(); if (id === undefined) return;
    error.value = "";
    try {
      const selected = await selectEmulator();
      if (owns(id) && !selected.cancelled && selected.path) draft.value.EMU_PATH = selected.path;
    } catch (reason) { if (owns(id)) error.value = formatApiError(reason); }
    finally { endWrite(id); }
  }

  async function startSelectedTask() {
    if (runActive.value || editLocked.value) return;
    if (!draft.value?.FARM_TARGET || dirty.value) {
      error.value = dirty.value ? "PROFILE_UNSAVED: 请先保存配置" : "TASK_NOT_SELECTED: 请选择任务";
      return;
    }
    const repeat = draft.value.FARM_TARGET === "Scorpionesses";
    const count = repeat && repeatMode.value === 'count' ? repeatCount.value : undefined;
    if (count !== undefined && (!Number.isInteger(count) || count < 1 || count > 1000000)) {
      error.value = '循环次数必须是1到1000000之间的整数';
      return;
    }
    const fingerprint = JSON.stringify([draft.value.FARM_TARGET, envelope.value?.revision, resourceLocale.value, repeat, count]);
    const taskId = draft.value.FARM_TARGET, revision = persisted.value?.revision, locale = resourceLocale.value;
    error.value = "";
    try {
      await session.submit(fingerprint, taskId, (id) => startTask(taskId, id, revision, locale, repeat, count));
      if (alive) notice.value = "启动请求已接收；正在执行正式装配和启动检查";
    } catch (reason) { if (alive) error.value = formatApiError(reason); }
  }

  watch(strategies, () => {
    if (draft.value) writeStrategies(draft.value, strategies.value);
  }, { deep: true, flush: "sync" });
  onMounted(() => { void load(); });
  onBeforeUnmount(() => { alive = false; ++selectionSequence; ++writeGeneration; session.writing.workbench = false; });

  return reactive({
    envelope, draft, logging, catalog, device, run, loading, saving, deviceBusy, error, notice, resourceLocale, repeatMode, repeatCount,
    strategies, dirty, runActive, runLabel, runError, starting, selectedTask, load, save, clearTaskOverride, revert, selectTask,
    deviceAction, chooseEmulator, startSelectedTask, requestStop: session.requestStop, noteRename,
    editLocked, taskLoading, selectionId,
  });
}
