<script setup lang="ts">
import { onMounted, ref } from "vue";
import { LayoutDashboard, Network, RefreshCw, Power } from "@lucide/vue";
import { formatApiError, readVersion, readService, shutdownService, type ServiceIdentity } from "./api/client";
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
const service = ref<ServiceIdentity>();
const exitState = ref("");
const exitAccepted = ref(false);
const exitDialog = ref<HTMLDialogElement>();
const pages = [
  { id: "workbench" as const, label: "工作台", icon: LayoutDashboard },
  { id: "workflow" as const, label: "流程编辑", icon: Network },
];

async function loadService() {
  serviceLoading.value = true;
  serviceError.value = "";
  try { version.value = await readVersion(); service.value = await readService(); }
  catch (reason) { serviceError.value = formatApiError(reason); }
  finally { serviceLoading.value = false; }
}
async function exitService() {
  if (!service.value || exitState.value) return;
  exitDialog.value?.close();
  exitState.value = "正在退出";
  serviceError.value = "";
  try {
    // 使用当前页面已连接的实例身份，旧页面不能误关部署后的新进程。
    await shutdownService(service.value.instance_id);
    exitAccepted.value = true;
    session.closeServiceSession();
    exitState.value = "退出请求已接收";
  } catch (reason) {
    serviceError.value = formatApiError(reason);
    exitState.value = "";
  }
}
function requestExit() {
  if (dirty.value.workbench || dirty.value.workflow) exitDialog.value?.showModal();
  else void exitService();
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
      <span>{{ exitState || (version ? `版本 ${version.version}` : serviceError ? '版本未获取' : '读取版本') }}</span>
      <button class="icon-button" title="刷新服务状态" aria-label="刷新服务状态" :disabled="serviceLoading || !!exitState" @click="loadService"><RefreshCw :size="16" :class="{ spinning: serviceLoading }" /></button>
      <button title="停止任务并退出工具，不关闭游戏或模拟器" :disabled="!service || !!exitState || session.writing.workbench || session.writing.workflow" @click="requestExit"><Power :size="16" />退出工具</button>
    </div>
  </header>
  <p v-if="serviceError" role="alert">{{ serviceError }}</p>
  <dialog ref="exitDialog" class="service-exit-dialog" aria-labelledby="exit-dialog-title">
    <h2 id="exit-dialog-title">有未保存更改</h2>
    <p>退出将放弃当前未保存的更改。</p>
    <div class="command-row">
      <button autofocus @click="exitDialog?.close()">取消</button>
      <button @click="exitService"><Power :size="16" />退出并放弃更改</button>
    </div>
  </dialog>
  <template v-if="!exitAccepted">
  <RunSessionBar />
  <WorkbenchPage v-if="currentPage === 'workbench'" @dirty="updateDirty('workbench', $event)" />
  <WorkflowPage v-else-if="currentPage === 'workflow'" @dirty="updateDirty('workflow', $event)" />
  <RunDiagnostics />
  </template>
</template>
