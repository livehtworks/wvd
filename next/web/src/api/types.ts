export interface Version { service: string; version: string; api_version: number; stage: string }
export interface Capabilities { platform: string; device_control: boolean; task_execution: boolean; maafw: { locked_version: string; loaded: boolean } }
export interface ApiErrorBody { error_code: string; message: string; details?: unknown }
export type JsonObject = Record<string, unknown>;

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
export interface WvdProfile extends JsonObject {
  EMU_PATH?: string; ADB_ADRESS?: string; EMU_INDEX?: number; AUTO_START_CLASH?: boolean;
  FARM_TARGET_TEXT?: string; FARM_TARGET?: string; TASK_SPECIFIC_CONFIG?: boolean;
  WHO_WILL_OPEN_IT?: string | number; QUICK_DISARM_CHEST?: boolean;
  SKIP_COMBAT_RECOVER?: boolean; SKIP_CHEST_RECOVER?: boolean; RECOVER_WHEN_BEGINNING?: boolean;
  ACTIVE_REST?: boolean; REST_INTERVEL?: number; KARMA_ADJUST?: string | number; RE_ASSEMBLE_PARTY?: boolean;
  DEFAULT_OVERALL_STRATEGY?: string;
  TASK_POINT_STRATEGY?: { overall_strategy?: string; task_point?: Record<string, string> | Array<{ point: string; strategy: string }>; [key: string]: unknown };
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
export interface WorkflowNode { id: string; type?: string; position: { x: number; y: number }; data: WorkflowNodeData; repeat_limit?: number }
export interface WorkflowEdge {
  id: string; source: string; target: string; sourceHandle?: string | null; targetHandle?: string | null; label?: string;
  data?: { order?: number; kind?: "sequence" | "candidate" | "failure"; [key: string]: unknown };
}
export interface WorkflowDefinition extends JsonObject {
  id: string; name: string; revision?: string; description?: string; entry_node_id?: string;
  nodes: WorkflowNode[]; edges: WorkflowEdge[]; created_from?: string; runnable?: boolean;
  time_limit_ms?: number;
  validation_errors?: Array<{ node_id?: string; error_code: string; message: string }>;
}
export interface SubmissionReceipt extends JsonObject {
  accepted: boolean; request_id: string; submission_state?: string; replayed?: boolean;
  run_id?: string | number;
}
export interface RunState extends JsonObject {
  busy?: boolean; quiescent?: boolean;
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
