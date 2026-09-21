<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import {
  ArrowDownToLine,
  ChevronLeft,
  ChevronRight,
  RefreshCw,
  Search,
  ListChecks,
  Images,
} from "@lucide/vue";
import { useInventory } from "../stores/useInventory";
import InventoryTable from "../components/InventoryTable.vue";
import ItemDetail from "../components/ItemDetail.vue";
const state = useInventory();
const {
  version,
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
} = state;
const tab = ref("inventory"),
  assetFilter = ref("CASE_MISMATCH");
const options: Record<string, string> = {
  all: "全部类型",
  function: "函数",
  config: "配置",
  task: "任务",
  quest_field: "任务字段",
  entry: "操作入口",
  branch: "条件分支",
  model: "数据结构",
  lambda: "回调",
  runtime_field: "运行字段",
  data_field: "字典字段",
  operation: "构建 / 更新",
  asset: "资源",
};
const assetRows = computed(() =>
  (assets.value?.references ?? []).filter(
    (r) => assetFilter.value === "all" || r.status === assetFilter.value,
  ),
);
const statusLabels: Record<string, string> = {
  CASE_MISMATCH: "大小写不符",
  EXACT: "精确匹配",
  DYNAMIC_REVIEW: "动态引用待验",
  MISSING_OR_MOD: "缺失或来自扩展",
};
watch([query, kind], () => {
  page.value = 1;
});
onMounted(load);
</script>
<template>
  <main class="app-main migration-page">
    <section class="heading">
      <div>
        <div class="eyebrow">架构迁移 / 功能基线</div>
        <h1>迁移盘点</h1>
      </div>
      <div class="command-row"><span class="mono">{{ version?.version ?? "—" }}</span><button class="icon-button" aria-label="刷新" title="刷新" :disabled="loading" @click="load"><RefreshCw :size="17" :class="{ spinning: loading }" /></button><a class="download" href="/migration/feature_inventory.json" download><ArrowDownToLine :size="17" /> JSON</a></div>
    </section>
    <div v-if="error" class="error" role="alert">连接失败：{{ error }}</div>
    <section class="metrics" aria-label="基线统计">
      <div>
        <span>函数</span
        ><strong>{{ inventory?.counts.function ?? "—" }}</strong>
      </div>
      <div>
        <span>配置字段</span
        ><strong>{{ inventory?.counts.config ?? "—" }}</strong>
      </div>
      <div>
        <span>任务</span><strong>{{ inventory?.counts.task ?? "—" }}</strong>
      </div>
      <div>
        <span>操作入口</span
        ><strong>{{ inventory?.counts.entry ?? "—" }}</strong>
      </div>
      <div>
        <span>未登记归属</span
        ><strong>{{ inventory?.coverage.unmapped_items ?? "—" }}</strong>
      </div>
      <div class="baseline">
        <span>源码基线</span
        ><code>{{ inventory?.baseline.slice(0, 12) ?? "—" }}</code
        ><span class="tag warning">生产切换未放行</span>
      </div>
    </section>
    <nav class="tabs" aria-label="视图">
      <button :aria-selected="tab === 'inventory'" @click="tab = 'inventory'">
        <ListChecks :size="17" />迁移清单</button
      ><button :aria-selected="tab === 'assets'" @click="tab = 'assets'">
        <Images :size="17" />资源核对
      </button>
    </nav>
    <section v-if="tab === 'inventory'">
      <div class="toolbar">
        <label class="search"
          ><Search :size="17" /><input
            v-model="query"
            aria-label="搜索基线项"
            placeholder="搜索基线项" /></label
        ><select v-model="kind" aria-label="类型">
          <option v-for="(label, value) in options" :value="value">
            {{ label }}
          </option></select
        ><span class="count">{{ filtered.length }} 项</span>
      </div>
      <div :class="['workspace', { 'has-detail': selected }]">
        <InventoryTable
          :items="visible"
          :selected="selected?.id"
          @select="selected = $event"
        /><ItemDetail
          v-if="selected"
          :item="selected"
          @close="selected = undefined"
        />
      </div>
      <footer class="pagination">
        <span>{{ page }} / {{ pageCount }}</span
        ><button
          class="icon-button"
          aria-label="上一页"
          title="上一页"
          :disabled="page <= 1"
          @click="page--"
        >
          <ChevronLeft :size="18" /></button
        ><button
          class="icon-button"
          aria-label="下一页"
          title="下一页"
          :disabled="page >= pageCount"
          @click="page++"
        >
          <ChevronRight :size="18" />
        </button>
      </footer>
    </section>
    <section v-else>
      <div class="toolbar">
        <h2>资源名称</h2>
        <select v-model="assetFilter" aria-label="资源状态">
          <option value="CASE_MISMATCH">大小写不符</option>
          <option value="DYNAMIC_REVIEW">动态引用待验</option>
          <option value="EXACT">精确匹配</option>
          <option value="all">全部引用</option></select
        ><span class="count">{{ assetRows.length }} 项</span>
      </div>
      <div class="table-scroll">
        <table aria-label="资源核对清单">
          <thead>
            <tr>
              <th>引用</th>
              <th>原位置</th>
              <th>实际文件</th>
              <th>状态</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(row, index) in assetRows" :key="index">
              <td class="mono">{{ row.reference }}</td>
              <td class="mono">{{ row.source_ref }}</td>
              <td class="mono">{{ row.matches.join(", ") || "运行时决定" }}</td>
              <td>
                <span :class="['tag', { warning: row.status !== 'EXACT' }]">{{
                  statusLabels[row.status]
                }}</span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
  </main>
</template>
