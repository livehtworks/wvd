import type {
  ApiErrorBody, AssetReport, Capabilities, Catalog, DeviceState, Inventory, JsonObject,
  ProfileEnvelope, RecognitionProbeResult, RunState, SubmissionReceipt, Version, WorkflowDefinition,
} from "./types";

export class ApiError extends Error {
  constructor(public readonly errorCode: string, message: string, public readonly status = 0, public readonly details?: unknown) {
    super(message);
    this.name = "ApiError";
  }
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  let response: Response;
  try {
    response = await fetch(path, {
      ...init,
      cache: "no-store",
      signal: init.signal ?? AbortSignal.timeout(10000),
      headers: init.body instanceof FormData ? init.headers : { "Content-Type": "application/json", ...init.headers },
    });
  } catch (error) {
    throw new ApiError("NETWORK_ERROR", error instanceof Error ? error.message : "无法连接本地服务");
  }
  const text = await response.text();
  let body: unknown;
  if (text) {
    try { body = JSON.parse(text); } catch { body = undefined; }
  }
  if (!response.ok) {
    const candidate = ((body as { error?: ApiErrorBody })?.error ?? body ?? {}) as Partial<ApiErrorBody>;
    throw new ApiError(candidate.error_code ?? `HTTP_${response.status}`, candidate.message ?? response.statusText ?? "请求失败", response.status, candidate.details);
  }
  return body as T;
}

const get = <T>(path: string) => request<T>(path);
const send = <T>(path: string, method: string, body?: unknown) => request<T>(path, {
  method,
  body: body === undefined ? undefined : JSON.stringify(body),
});

export const formatApiError = (error: unknown) => error instanceof ApiError
  ? `${error.errorCode}: ${error.message}`
  : error instanceof Error ? error.message : String(error);

export const readVersion = () => get<Version>("/api/v1/version");
export const readCapabilities = () => get<Capabilities>("/api/v1/capabilities");
export const readInventory = () => get<Inventory>("/migration/feature_inventory.json");
export const readAssets = () => get<AssetReport>("/migration/asset_case_report.json");
export const readProfile = () => get<ProfileEnvelope>("/api/v1/profile");
export const readTaskProfile = (taskId: string) => send<ProfileEnvelope>("/api/v1/profile/effective", "POST", { task_id: taskId });
export const saveProfile = (profile: ProfileEnvelope & JsonObject) => send<ProfileEnvelope>("/api/v1/profile", "PUT", profile);
export const readCatalog = () => get<Catalog>("/api/v1/catalog");
export const listWorkflows = () => get<{ workflows: WorkflowDefinition[] } | WorkflowDefinition[]>("/api/v1/workflows");
export const createWorkflow = (workflow: Omit<WorkflowDefinition, "revision">) => send<WorkflowDefinition>("/api/v1/workflows", "POST", workflow);
export const importTaskWorkflow = (taskId: string, flowId: string) => send<WorkflowDefinition>("/api/v1/workflows/from-task", "POST", { task_id: taskId, flow_id: flowId });
export const readWorkflow = (id: string) => get<WorkflowDefinition>(`/api/v1/workflows/${encodeURIComponent(id)}`);
export const saveWorkflow = (workflow: WorkflowDefinition) => send<WorkflowDefinition>(`/api/v1/workflows/${encodeURIComponent(workflow.id)}`, "PUT", workflow);
export const deleteWorkflow = (id: string, revision: string) => send<void>(`/api/v1/workflows/${encodeURIComponent(id)}`, "DELETE", { revision });
export const runWorkflow = (id: string, body: JsonObject = {}) => send<SubmissionReceipt>(`/api/v1/workflows/${encodeURIComponent(id)}/run`, "POST", body);
export const stopRun = (id?: string | number, requestId?: string) => send<RunState>(
  requestId || id === undefined ? "/api/v1/runs/current/stop" : `/api/v1/runs/${encodeURIComponent(String(id))}/stop`,
  "POST", requestId ? { request_id: requestId } : {},
);
export const readCurrentRun = () => get<RunState>("/api/v1/runs/current");
export const startTask = (taskId: string, requestId: string, profileRevision: string | undefined, resourceLocale: "en" | "zh-Hant") =>
  send<SubmissionReceipt>("/api/v1/runs/start", "POST", {
    task_id: taskId, request_id: requestId, resource_locale: resourceLocale,
    ...(profileRevision ? { profile_revision: profileRevision } : {}),
  });
export const readDevice = () => get<DeviceState>("/api/v1/device");
export const selectEmulator = () => send<{ cancelled: boolean; path?: string }>("/api/v1/device/select-emulator", "POST", {});
export const connectDevice = (body: JsonObject) => send<DeviceState>("/api/v1/device/connect", "POST", body);
export const disconnectDevice = () => send<DeviceState>("/api/v1/device/disconnect", "POST", {});
export const captureDevice = () => send<DeviceState>("/api/v1/device/capture", "POST", {});

export async function probeRecognition(payload: JsonObject, image?: File): Promise<RecognitionProbeResult> {
  if (!image) return send<RecognitionProbeResult>("/api/v1/recognition/probe", "POST", payload);
  if (image.size > 8 * 1024 * 1024) throw new Error("PROBE_IMAGE_BYTES_INVALID: 图片不能超过 8 MiB");
  const bytes = new Uint8Array(await image.arrayBuffer());
  let binary = "";
  for (let offset = 0; offset < bytes.length; offset += 0x8000)
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  return send<RecognitionProbeResult>("/api/v1/recognition/probe", "POST", {
    ...payload,
    image_name: image.name,
    image_base64: btoa(binary),
  });
}
