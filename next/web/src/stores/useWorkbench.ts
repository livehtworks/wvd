import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import { runBusy, displayRunState } from "./runStatus";
import {
  captureDevice, connectDevice, disconnectDevice, formatApiError, readCatalog, readCurrentRun,
  readDevice, readProfile, readTaskProfile, saveProfile, stopRun,
  startTask, selectEmulator,
} from "../api/client";
import type { Catalog, DeviceState, ProfileEnvelope, RunState, StrategyGroup, WvdProfile } from "../api/types";

// 配置全部是 JSON 数据。JSON 往返可安全解开 Vue 的响应式 Proxy；
// structuredClone 直接接收 Proxy 会在真实浏览器中抛 DataCloneError。
const clone = <T>(value: T): T => JSON.parse(JSON.stringify(value)) as T;
const signature = (value: unknown) => JSON.stringify(value);

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

export function useWorkbench() {
  const resourceLocale = ref<"en" | "zh-Hant">(
    typeof window !== "undefined" && window.localStorage.getItem("wvd.gameResourceLocale") === "en" ? "en" : "zh-Hant",
  );
  watch(resourceLocale, (value) => window.localStorage.setItem("wvd.gameResourceLocale", value));
  const envelope = ref<ProfileEnvelope>();
  const draft = ref<WvdProfile>();
  const savedSignature = ref("");
  const catalog = ref<Catalog>({});
  const device = ref<DeviceState>();
  const run = ref<RunState>();
  const starting = ref(false);
  const linkError = ref("");
  let pendingRequest: { id: string; fingerprint: string } | undefined;
  let polling = false;
  const loading = ref(false);
  const saving = ref(false);
  const deviceBusy = ref(false);
  const error = ref("");
  const notice = ref("");
  const strategies = ref<StrategyGroup[]>([]);
  const strategyRenames = ref<Record<string, string>>({});
  function noteRename(oldName: string, newName: string) {
    const original = Object.keys(strategyRenames.value).find((key) => strategyRenames.value[key] === oldName);
    if (original) strategyRenames.value[original] = newName;
    else if (envelope.value && readStrategies(envelope.value.profile).some((group) => group.group_name === oldName))
      strategyRenames.value[oldName] = newName;
  }
  let pollHandle: number | undefined;

  const dirty = computed(() => Boolean(draft.value) && signature(draft.value) !== savedSignature.value);
  const runActive = computed(() => starting.value || runBusy(run.value));
  const runLabel = computed(() => starting.value ? "提交启动请求" : displayRunState(run.value));
  const runError = computed(() => run.value?.submission?.error ?? linkError.value);
  const selectedTask = computed(() => catalog.value.tasks?.find((task) => task.id === draft.value?.FARM_TARGET));

  async function load() {
    loading.value = true;
    error.value = "";
    try {
      const [profileValue, catalogValue, deviceValue, runValue] = await Promise.all([
        readProfile(), readCatalog(), readDevice(), readCurrentRun(),
      ]);
      envelope.value = normalizeEnvelope(profileValue);
      draft.value = clone(envelope.value.profile);
      draft.value.TASK_POINT_STRATEGY ??= { overall_strategy: "", task_point: {} };
      strategies.value = readStrategies(draft.value);
      strategyRenames.value = {};
      savedSignature.value = signature(draft.value);
      catalog.value = catalogValue;
      device.value = deviceValue;
      run.value = runValue;
    } catch (reason) {
      error.value = formatApiError(reason);
    } finally {
      loading.value = false;
    }
  }

  async function save() {
    if (!draft.value || !envelope.value) return;
    saving.value = true;
    error.value = "";
    notice.value = "";
    try {
      writeStrategies(draft.value, clone(strategies.value));
      const saved = normalizeEnvelope(await saveProfile({
        ...envelope.value,
        profile: draft.value,
        strategy_renames: strategyRenames.value,
      }));
      envelope.value = saved;
      draft.value = clone(saved.profile);
      draft.value.TASK_POINT_STRATEGY ??= { overall_strategy: "", task_point: {} };
      strategies.value = readStrategies(draft.value);
      strategyRenames.value = {};
      savedSignature.value = signature(draft.value);
      strategyRenames.value = {};
      notice.value = "配置已由服务端保存";
    } catch (reason) {
      error.value = formatApiError(reason);
    } finally {
      saving.value = false;
    }
  }

  async function clearTaskOverride() {
    if (!draft.value || !envelope.value || !draft.value.FARM_TARGET) return;
    if (dirty.value) {
      error.value = "PROFILE_UNSAVED: 请先保存或重载当前更改，再清除任务覆盖";
      return;
    }
    saving.value = true;
    error.value = "";
    try {
      const saved = normalizeEnvelope(await saveProfile({
        ...envelope.value,
        profile: draft.value,
        operation: "clear_task_override",
        task_id: draft.value.FARM_TARGET,
      }));
      envelope.value = saved;
      draft.value = clone(saved.profile);
      draft.value.TASK_POINT_STRATEGY ??= { overall_strategy: "", task_point: {} };
      strategies.value = readStrategies(draft.value);
      strategyRenames.value = {};
      savedSignature.value = signature(draft.value);
      notice.value = "当前任务覆盖已清除";
    } catch (reason) { error.value = formatApiError(reason); }
    finally { saving.value = false; }
  }

  function revert() {
    if (!envelope.value) return;
    draft.value = clone(envelope.value.profile);
    draft.value.TASK_POINT_STRATEGY ??= { overall_strategy: "", task_point: {} };
    strategies.value = readStrategies(draft.value);
    savedSignature.value = signature(draft.value);
    strategyRenames.value = {};
    notice.value = "已恢复为服务端保存版本";
  }

  async function selectTask(taskId: string) {
    if (!draft.value) return;
    error.value = "";
    try {
      const selected = normalizeEnvelope(await readTaskProfile(taskId));
      draft.value = clone(selected.profile);
      draft.value.TASK_POINT_STRATEGY ??= { overall_strategy: "", task_point: {} };
      strategies.value = readStrategies(draft.value);
      envelope.value = selected;
      savedSignature.value = signature(draft.value);
      notice.value = selected.task_override_active ? "已载入该任务的专用配置" : "已载入默认配置；保存后切换任务";
    } catch (reason) { error.value = formatApiError(reason); }
  }

  async function deviceAction(action: "connect" | "disconnect" | "capture") {
    if (!draft.value) return;
    deviceBusy.value = true;
    error.value = "";
    try {
      device.value = action === "connect"
        ? await connectDevice({
          emulator_path: draft.value.EMU_PATH,
          adb_address: draft.value.ADB_ADRESS,
          emulator_index: draft.value.EMU_INDEX,
          auto_start_clash: draft.value.AUTO_START_CLASH,
        })
        : action === "disconnect" ? await disconnectDevice() : await captureDevice();
    } catch (reason) {
      error.value = formatApiError(reason);
    } finally {
      deviceBusy.value = false;
    }
  }

  async function chooseEmulator() {
    if (!draft.value) return;
    error.value = "";
    try {
      const selected = await selectEmulator();
      if (!selected.cancelled && selected.path) draft.value.EMU_PATH = selected.path;
    } catch (reason) { error.value = formatApiError(reason); }
  }

  async function startSelectedTask() {
    if (runActive.value || starting.value) return;
    if (!draft.value?.FARM_TARGET || dirty.value) {
      error.value = dirty.value ? "PROFILE_UNSAVED: 请先保存配置" : "TASK_NOT_SELECTED: 请选择任务";
      return;
    }
    const fingerprint = JSON.stringify([draft.value.FARM_TARGET, envelope.value?.revision, resourceLocale.value]);
    if (!pendingRequest || pendingRequest.fingerprint !== fingerprint)
      pendingRequest = { id: crypto.randomUUID(), fingerprint };
    starting.value = true;
    error.value = "";
    try {
      await startTask(draft.value.FARM_TARGET, pendingRequest.id, envelope.value?.revision, resourceLocale.value);
      run.value = await readCurrentRun();
      notice.value = "启动请求已接收；正在执行正式装配和启动检查";
    } catch (reason) { error.value = formatApiError(reason); }
    finally { starting.value = false; }
  }

  async function requestStop() {
    if (!run.value?.run_id && !pendingRequest && !run.value?.submission?.request_id) return;
    error.value = "";
    try { run.value = await stopRun(run.value?.run_id,
      run.value?.submission?.state === "preparing" ? run.value.submission.request_id :
        starting.value ? pendingRequest?.id : undefined); }
    catch (reason) { error.value = formatApiError(reason); }
  }

  async function pollRun() {
    if (polling) return;
    polling = true;
    try {
      const [runValue, deviceValue] = await Promise.all([readCurrentRun(), readDevice()]);
      run.value = runValue;
      device.value = deviceValue;
      deviceBusy.value = Boolean(deviceValue.busy);
      linkError.value = "";
      if (pendingRequest && runValue.submission?.request_id === pendingRequest.id &&
          ["submitted", "failed", "cancelled"].includes(runValue.submission?.state ?? "") && !runBusy(runValue))
        pendingRequest = undefined;
      if (deviceValue.operation?.state === "failed")
        error.value = `${String(deviceValue.error_code ?? "DEVICE_OPERATION_FAILED")}: ${String(deviceValue.message ?? deviceValue.operation.error ?? "设备操作失败")}`;
    } catch (reason) { linkError.value = `状态连接中断，最后状态不可作为已停止证明：${formatApiError(reason)}`; }
    finally { polling = false; }
  }

  watch(strategies, () => {
    if (draft.value) writeStrategies(draft.value, clone(strategies.value));
  }, { deep: true });
  onMounted(() => {
    void load();
    pollHandle = window.setInterval(pollRun, 1500);
  });
  onBeforeUnmount(() => window.clearInterval(pollHandle));

  return reactive({
    envelope, draft, catalog, device, run, loading, saving, deviceBusy, error, notice, resourceLocale,
    strategies, dirty, runActive, runLabel, runError, starting, selectedTask, load, save, clearTaskOverride, revert, selectTask,
    deviceAction, chooseEmulator, startSelectedTask, requestStop, noteRename,
  });
}
