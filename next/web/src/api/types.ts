export interface Version {
  service: string;
  version: string;
  api_version: number;
  stage: string;
}
export interface Capabilities {
  platform: string;
  device_control: boolean;
  task_execution: boolean;
  maafw: { locked_version: string; loaded: boolean };
}
export interface Item {
  id: string;
  kind: string;
  legacy_symbol: string;
  original_semantics: string;
  source_ref: string;
  new_owner: string;
  new_entry: string;
  acceptance_ids: string[];
  status: string;
  name?: string;
  task_title?: string;
  path?: string;
  [key: string]: unknown;
}
export interface Inventory {
  baseline: string;
  counts: Record<string, number>;
  items: Item[];
  coverage: { unmapped_items: number };
  calls: { resolution: string }[];
}
export interface AssetReference {
  reference: string;
  source_ref: string;
  context: string;
  status: string;
  matches: string[];
}
export interface AssetReport {
  references: AssetReference[];
  case_collisions: string[][];
}
