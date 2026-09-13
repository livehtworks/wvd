import type { Version, Capabilities, Inventory, AssetReport } from "./types";
async function get<T>(path: string): Promise<T> {
  const response = await fetch(path, {
    signal: AbortSignal.timeout(8000),
    cache: "no-store",
  });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json() as Promise<T>;
}
export const readVersion = () => get<Version>("/api/v1/version");
export const readCapabilities = () => get<Capabilities>("/api/v1/capabilities");
export const readInventory = () =>
  get<Inventory>("/migration/feature_inventory.json");
export const readAssets = () =>
  get<AssetReport>("/migration/asset_case_report.json");
