import { computed, inject, onBeforeUnmount, onMounted, provide, reactive, ref, type InjectionKey } from "vue";
import { ApiError, formatApiError, readCurrentRun, readDevice, stopRun } from "../api/client";
import type { DeviceState, RunState, SubmissionReceipt } from "../api/types";
import { displayRunState, runBusy } from "./runStatus";

function createSession() {
  const run = ref<RunState>();
  const device = ref<DeviceState>();
  const starting = ref(false);
  const stopping = ref(false);
  const serviceClosing = ref(false);
  const lastSuccess = ref(0);
  const now = ref(Date.now());
  const linkError = ref("");
  const deviceError = ref("");
  const commandError = ref("");
  const writing = reactive({ workbench: false, workflow: false });
  const intent = ref<{ id: string; fingerprint: string; target: string; uncertain: boolean }>();
  let alive = true;
  let generation = 0;
  let deviceGeneration = 0;
  let runFlight: Promise<void> | undefined;
  let deviceFlight: Promise<void> | undefined;
  let timer: number | undefined;
  const fresh = computed(() => !serviceClosing.value && !linkError.value && lastSuccess.value > 0 && now.value - lastSuccess.value <= 5000);
  const busy = computed(() => starting.value || runBusy(run.value));
  const canStart = computed(() => fresh.value && !busy.value && !stopping.value);
  const label = computed(() => !fresh.value ? "状态未知" : stopping.value ? "停止结果待确认" : starting.value ? "提交启动请求" : displayRunState(run.value));
  const error = computed(() => commandError.value || linkError.value ||
    (run.value?.repeat?.state === "failed" ? run.value.repeat.reason : run.value?.submission?.error) ||
    run.value?.error_code || run.value?.storage_error || run.value?.secondary_errors?.join("; ") || "");

  // 全 App 每种 GET 最多一个在途请求；命令前的旧读取不允许覆盖命令后的状态。
  function refreshRun(): Promise<void> {
    if (serviceClosing.value) return Promise.resolve();
    if (runFlight) return runFlight;
    const epoch = generation;
    runFlight = (async () => {
      try {
        const value = await readCurrentRun();
        if (!alive || epoch !== generation) return;
        run.value = value; lastSuccess.value = Date.now(); now.value = Date.now(); linkError.value = "";
        if (intent.value && value.submission?.request_id === intent.value.id &&
            ["submitted", "failed", "cancelled"].includes(value.submission.state ?? "")) intent.value.uncertain = false;
      } catch (reason) {
        if (alive && epoch === generation) linkError.value = `状态连接中断，不能确认已停止：${formatApiError(reason)}`;
      }
    })().finally(() => { runFlight = undefined; });
    return runFlight;
  }
  function refreshDevice(): Promise<void> {
    if (serviceClosing.value) return Promise.resolve();
    if (deviceFlight) return deviceFlight;
    const epoch = deviceGeneration;
    deviceFlight = (async () => {
      try {
        const value = await readDevice();
        if (alive && epoch === deviceGeneration) { device.value = value; deviceError.value = ""; }
      } catch (reason) { if (alive && epoch === deviceGeneration) deviceError.value = formatApiError(reason); }
    })().finally(() => { deviceFlight = undefined; });
    return deviceFlight;
  }
  async function refresh() { await Promise.all([refreshRun(), refreshDevice()]); }
  async function afterCommand() {
    // 等旧 GET 释放槽位，再读取；不为刷新另开并行 GET。
    await runFlight;
    if (alive) await refreshRun();
  }
  async function submit(fingerprint: string, target: string, send: (id: string) => Promise<SubmissionReceipt>) {
    if (!canStart.value) throw new Error("RUN_STATE_UNAVAILABLE: 请先确认运行状态");
    if (intent.value?.uncertain && intent.value.fingerprint !== fingerprint)
      throw new Error("START_UNCONFIRMED: 上次启动结果尚未确认，请先停止或确认原请求");
    if (!intent.value?.uncertain) intent.value = { id: crypto.randomUUID(), fingerprint, target, uncertain: true };
    const owned = intent.value!;
    starting.value = true; commandError.value = ""; ++generation;
    try {
      await send(owned.id);
      if (alive && intent.value === owned) owned.uncertain = false;
    } catch (reason) {
      if (alive) {
        commandError.value = formatApiError(reason);
        if (reason instanceof ApiError && reason.status >= 400 && reason.status < 500) owned.uncertain = false;
      }
      throw reason;
    } finally {
      if (alive) { await afterCommand(); if (alive) starting.value = false; }
    }
  }
  async function requestStop() {
    if (stopping.value) return;
    stopping.value = true; commandError.value = ""; ++generation;
    const requestId = run.value?.submission?.state === "preparing" ? run.value.submission.request_id
      : starting.value || intent.value?.uncertain ? intent.value?.id : undefined;
    try {
      await stopRun(undefined, requestId);
      // 停止响应仅是收件回执，不能把它当作 quiescent 证明。
    } catch (reason) { if (alive) commandError.value = formatApiError(reason); }
    finally { if (alive) { await afterCommand(); if (alive) stopping.value = false; } }
  }
  async function deviceCommand(send: () => Promise<DeviceState>) {
    ++deviceGeneration;
    const value = await send();
    if (alive) device.value = value;
  }
  function closeServiceSession() {
    serviceClosing.value = true;
    ++generation; ++deviceGeneration;
    window.clearInterval(timer);
  }
  onMounted(() => { void refresh(); timer = window.setInterval(() => { now.value = Date.now(); void refresh(); }, 1500); });
  onBeforeUnmount(() => { alive = false; ++generation; ++deviceGeneration; window.clearInterval(timer); });
  return reactive({ run, device, starting, stopping, fresh, busy, canStart, label, error, deviceError,
    lastSuccess, intent, writing, refresh, submit, requestStop, deviceCommand, closeServiceSession });
}
type Session = ReturnType<typeof createSession>;
const key: InjectionKey<Session> = Symbol("run-session");
export function provideRunSession() { const session = createSession(); provide(key, session); return session; }
export function useRunSession() {
  const session = inject(key);
  if (!session) throw new Error("RUN_SESSION_NOT_PROVIDED");
  return session;
}
