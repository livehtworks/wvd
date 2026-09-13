import { computed, ref } from "vue";
import {
  readAssets,
  readCapabilities,
  readInventory,
  readVersion,
} from "../api/client";
import type {
  AssetReport,
  Capabilities,
  Inventory,
  Item,
  Version,
} from "../api/types";

// 这里只镜像只读服务与基线；没有任务状态机、配置写入或自动重试运行命令。
export function useInventory() {
  const version = ref<Version>(),
    capabilities = ref<Capabilities>(),
    inventory = ref<Inventory>(),
    assets = ref<AssetReport>();
  const loading = ref(false),
    error = ref(""),
    query = ref(""),
    kind = ref("all"),
    page = ref(1),
    selected = ref<Item>();
  const filtered = computed(() =>
    (inventory.value?.items ?? []).filter(
      (item) =>
        (kind.value === "all" || kind.value === item.kind) &&
        `${item.name ?? ""} ${item.task_title ?? ""} ${item.legacy_symbol} ${item.source_ref} ${item.new_owner}`
          .toLocaleLowerCase()
          .includes(query.value.trim().toLocaleLowerCase()),
    ),
  );
  const pageCount = computed(() =>
    Math.max(1, Math.ceil(filtered.value.length / 40)),
  );
  const visible = computed(() =>
    filtered.value.slice((page.value - 1) * 40, page.value * 40),
  );
  async function load() {
    if (loading.value) return;
    loading.value = true;
    error.value = "";
    try {
      const [v, c, i, a] = await Promise.all([
        readVersion(),
        readCapabilities(),
        readInventory(),
        readAssets(),
      ]);
      // 请求期间仍可改变选择；应用新数据时才取 ID，不能把等待前的旧选择写回来。
      const selectedId = selected.value?.id;
      version.value = v;
      capabilities.value = c;
      inventory.value = i;
      assets.value = a;
      selected.value = i.items.find((item) => item.id === selectedId);
      page.value = Math.max(1, Math.min(page.value, pageCount.value));
    } catch (e) {
      version.value = undefined;
      error.value = e instanceof Error ? e.message : "连接失败";
    } finally {
      loading.value = false;
    }
  }
  return {
    version,
    capabilities,
    inventory,
    assets,
    loading,
    error,
    query,
    kind,
    page,
    selected,
    filtered,
    pageCount,
    visible,
    load,
  };
}
