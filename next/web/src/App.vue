<script setup lang="ts">
import { onMounted, ref } from "vue";
import { LayoutDashboard, Network, RefreshCw } from "@lucide/vue";
import { formatApiError, readVersion } from "./api/client";
import type { Version } from "./api/types";
import { provideRunSession } from "./stores/useRunSession";
import RunSessionBar from "./components/RunSessionBar.vue";
import RunDiagnostics from "./components/RunDiagnostics.vue";
import WorkbenchPage from "./pages/WorkbenchPage.vue";
import WorkflowPage from "./pages/WorkflowPage.vue";

const session = provideRunSession();
type PageId = "workbench" | "workflow";
const currentPage = ref<PageId>("workbench");
const dirty = ref<Record<PageId, boolean>>({ workbench: false, workflow: false });
const version = ref<Version>();
const serviceError = ref("");
const serviceLoading = ref(false);
const pages = [
  { id: "workbench" as const, label: "工作台", icon: LayoutDashboard },
  { id: "workflow" as const, label: "流程编辑", icon: Network },
];

async function loadService() {
  serviceLoading.value = true;
  serviceError.value = "";
  try { version.value = await readVersion(); }
  catch (reason) { serviceError.value = formatApiError(reason); }
  finally { serviceLoading.value = false; }
}
function navigate(target: PageId) {
  if (target === currentPage.value) return;
  if (session.writing[currentPage.value]) return;
  if (dirty.value[currentPage.value] && !window.confirm("当前页面有未保存更改，确定离开？")) return;
  currentPage.value = target;
}
function updateDirty(page: PageId, value: boolean) { dirty.value[page] = value; }
onMounted(loadService);
</script>

<template>
  <header class="app-header">
    <div class="app-brand"><span class="brand-mark">W</span><div><strong>WVD</strong><small>自动化工作台</small></div></div>
    <nav class="main-nav" aria-label="主导航">
      <button v-for="page in pages" :key="page.id" :disabled="session.writing[currentPage]" :aria-current="currentPage === page.id ? 'page' : undefined" @click="navigate(page.id)"><component :is="page.icon" :size="17" />{{ page.label }}<span v-if="dirty[page.id]" class="dirty-dot" /></button>
    </nav>
    <div class="service-state" :title="serviceError || '本地服务'">
      <span :class="['connection-dot', { online: session.fresh }]" />
      <span>{{ version ? `版本 ${version.version}` : serviceError ? '版本未获取' : '读取版本' }}</span>
      <button class="icon-button" title="刷新服务状态" aria-label="刷新服务状态" :disabled="serviceLoading" @click="loadService"><RefreshCw :size="16" :class="{ spinning: serviceLoading }" /></button>
    </div>
  </header>
  <RunSessionBar />
  <WorkbenchPage v-if="currentPage === 'workbench'" @dirty="updateDirty('workbench', $event)" />
  <WorkflowPage v-else-if="currentPage === 'workflow'" @dirty="updateDirty('workflow', $event)" />
  <RunDiagnostics />
</template>
