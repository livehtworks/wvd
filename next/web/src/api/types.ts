export interface Version { service: string; version: string; api_version: number; stage: string }
export interface Capabilities {
  platform: string;
  device_control: boolean;
  task_execution: boolean;
  engine: "wvd_native";
  recognition: string[];
  production_switch: boolean;
}
export interface ApiErrorBody { error_code: string; message: string; details?: unknown }
export type JsonObject = Record<string, unknown>;
export type ResourceLocale = "" | "en" | "zh-Hant" | "zh-Hans" | "ja";
export const resourceLocaleOptions: ReadonlyArray<{ value: ResourceLocale; label: string }> = [
  { value: "", label: "未选择" }, { value: "zh-Hant", label: "繁中" },
  { value: "en", label: "英文" }, { value: "zh-Hans", label: "简中" }, { value: "ja", label: "日文" },
];

export interface ProfileEnvelope {
  profile: WvdProfile;
  revision?: string;
  effective_source?: string;
  task_override_active?: boolean;
}
export interface SkillSetting extends JsonObject {
  role_var?: string; skill_var?: string; target_var?: string; skill_lvl?: number; freq_var?: string | number;
}
export interface StrategyGroup extends JsonObject {
  group_name: string; skill_settings: SkillSetting[]; complete_one_as_all?: boolean;
}
export interface SpecialCombatSettings extends JsonObject {
  skull: boolean; portrait: boolean; portrait_image: string;
  normal_strategy: string; special_strategy: string;
}
export interface WvdProfile extends JsonObject {
  EMU_PATH?: string; ADB_ADRESS?: string; EMU_INDEX?: number; AUTO_START_CLASH?: boolean;
  FARM_TARGET_TEXT?: string; FARM_TARGET?: string; TASK_SPECIFIC_CONFIG?: boolean;
  WHO_WILL_OPEN_IT?: string | number; QUICK_DISARM_CHEST?: boolean;
  SKIP_COMBAT_RECOVER?: boolean; SKIP_CHEST_RECOVER?: boolean; RECOVER_WHEN_BEGINNING?: boolean;
  ACTIVE_REST?: boolean; REST_INTERVEL?: number; KARMA_ADJUST?: string | number; RE_ASSEMBLE_PARTY?: boolean;
  DEFAULT_OVERALL_STRATEGY?: string;
  TASK_POINT_STRATEGY?: { overall_strategy?: string; task_point?: Record<string, string> | Array<{ point: string; strategy: string }>; special_combat?: SpecialCombatSettings; [key: string]: unknown };
  STRATEGY?: StrategyGroup[] | Record<string, Omit<StrategyGroup, "group_name">>;
  RELOAD_STRATEGY_WHEN?: string;
  ACTIVE_BEG_MONEY?: boolean; ACTIVE_ROYALSUITE_REST?: boolean; ACTIVE_TRIUMPH?: boolean;
  ACTIVE_BEAUTIFUL_ORE?: boolean; ACTIVE_CSC?: boolean; BYPASS_THE_WALL?: boolean;
  MAX_TRY_LIMIT?: number; MAX_CRASH_LIMIT?: number;
  LANGUAGE?: string; WEBSITE_ORG_TIME?: string; AM_REFRESH_TIME?: string;
}
export interface CatalogOption { value: string | number; label: string; description?: string }
export interface TaskCatalogItem { id: string; name: string; type?: string; category?: string; description?: string; task_points?: CatalogOption[] }
export interface WorkflowNodeType { type: string; label: string; category?: string; description?: string; defaults?: JsonObject }
export interface Catalog extends JsonObject {
  task_categories?: CatalogOption[]; tasks?: TaskCatalogItem[]; roles?: CatalogOption[]; skills?: CatalogOption[];
  skill_levels?: CatalogOption[]; skill_targets?: CatalogOption[]; skill_frequencies?: CatalogOption[];
  chest_openers?: CatalogOption[]; karma_directions?: CatalogOption[]; strategy_reload_timings?: CatalogOption[];
  node_types?: WorkflowNodeType[];
  templates?: CatalogOption[]; recognizers?: CatalogOption[]; business_nodes?: CatalogOption[];
}
export interface DeviceState extends JsonObject {
  state?: string; connected?: boolean; busy?: boolean; adb_address?: string; emulator_index?: number;
  display_name?: string; screenshot_url?: string; captured_at?: string; error_code?: string; message?: string;
  operation?: { state?: string; name?: string; error?: string | null };
}
export interface WorkflowNodeData extends JsonObject { label: string; node_type: string; parameters: JsonObject }
export interface EventResume { mode: "reobserve" | "replan"; node_id?: string; guard?: JsonObject }
export interface EventRule {
  enabled: boolean; class: "overlay" | "encounter"; priority: number; detect: JsonObject;
  handler?: import("../features/authoring/flowModel").FlowCall; resume?: EventResume;
  allow_nested?: string[]; disposition?: "handled" | "external_blocked"; reason?: string;
}
export interface WorkflowNode {
  id: string; type?: string; position: { x: number; y: number }; data: WorkflowNodeData;
  repeat_limit?: number; event_overrides?: Record<string, { enabled?: boolean; arguments?: Record<string, string | number | boolean> }>;
  resume?: Record<string, EventResume>;
}
export interface WorkflowEdge {
  id: string; source: string; target: string; sourceHandle?: string | null; targetHandle?: string | null; label?: string;
  data?: { order?: number; kind?: "sequence" | "candidate" | "failure"; [key: string]: unknown };
}
export interface WorkflowDefinition extends JsonObject {
  id: string; name: string; revision?: string; description?: string; entry_node_id?: string;
  nodes: WorkflowNode[]; edges: WorkflowEdge[]; created_from?: string; runnable?: boolean;
  time_limit_ms?: number;
  interface?: import("../features/authoring/flowModel").PublicInterface;
  slots?: string[]; resource_locale?: ResourceLocale;
  events?: Record<string, EventRule>;
  validation_errors?: Array<{ node_id?: string; error_code: string; message: string }>;
  builtin_status?: "current" | "update_available" | "local_modified" | "source_unknown";
  builtin_revision?: string;
}
export interface BuiltinInspection {
  flow_id: string; status: "current" | "update_available" | "local_modified" | "source_unknown";
  local_revision: string; builtin_revision: string; accepted_builtin?: string;
  current: JsonObject; builtin: JsonObject; affected_references: string[];
}
export interface SubmissionReceipt extends JsonObject {
  accepted: boolean; request_id: string; submission_state?: string; replayed?: boolean;
  run_id?: string | number;
}
export interface RunState extends JsonObject {
  busy?: boolean; quiescent?: boolean;
  call_stack?: Array<{ definition: string; node_id: string; source_path: Array<{flow_id?:string;node_id?:string;native_node?:string}> }>;
  unresolved_inputs?: Array<{ source_path:string; basis_frame:number; basis_epoch:number; action_epoch:number; delivery_unknown:boolean }>;
  execution?: JsonObject;
  node_path?: Array<{flow_id:string;node_id:string}>;
  active_event?: {event_id:string;class?:string;source_node:string;handler_entry?:string;phase?:string;
    depth:number;resume:{mode:string;node_id?:string};path?:Array<{event_id:string;source_node:string}>};
  suspended_step?: {pipeline_node:string;node_id?:string|null;node_path?:Array<{flow_id:string;node_id:string}>};
  outcome_category?: string;
  submission?: { request_id: string; kind?: string; state?: string; error?: string | null };
  run_id?: string | number; workflow_id?: string; workflow_revision?: string; state?: string; current_node_id?: string;
  failed_node_id?: string; task_name?: string; step_name?: string; started_at?: string; elapsed_seconds?: number;
  result?: string; error_code?: string; message?: string; statistics?: Record<string, string | number>;
  diagnostics?: Array<{ id?: string; label?: string; image_url?: string; roi?: number[]; status?: string;
    reason?: string; stage?: string; node_id?: string; frame_age_ms?: number; error?: string }>;
}
export interface RecognitionProbeResult extends JsonObject {
  outcome?: "Hit" | "NoHit" | "Error"; score?: number; roi?: number[];
  match?: { x: number; y: number; width: number; height: number }; image_url?: string; error_code?: string; message?: string;
}

export interface Item {
  id: string; kind: string; legacy_symbol: string; original_semantics: string; source_ref: string;
  new_owner: string; new_entry: string; acceptance_ids: string[]; status: string;
  name?: string; task_title?: string; path?: string; [key: string]: unknown;
}
export interface Inventory { baseline: string; counts: Record<string, number>; items: Item[]; coverage: { unmapped_items: number }; calls: { resolution: string }[] }
export interface AssetReference { reference: string; source_ref: string; context: string; status: string; matches: string[] }
export interface AssetReport { references: AssetReference[]; case_collisions: string[][] }
