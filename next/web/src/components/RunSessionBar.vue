<script setup lang="ts">
import { CircleStop, RefreshCw } from "@lucide/vue";
import { useRunSession } from "../stores/useRunSession";
const session = useRunSession();
</script>
<template>
  <section class="run-session-bar" aria-label="当前运行会话">
    <div class="session-facts" role="status">
      <strong>{{ session.label }}</strong>
      <span>{{ session.fresh ? '运行目标' : '最后已知目标' }}：{{ session.run?.task_name ?? session.run?.workflow_id ?? session.intent?.target ?? '无' }}</span>
      <span v-if="!session.fresh && session.run">最后已知状态：{{ session.run.state }}</span>
      <span>{{ session.run?.step_name ?? session.run?.execution?.phase ?? '' }}</span>
      <span v-if="session.run?.repeat">已完成 {{ session.run.repeat.completed_cycles }}<template v-if="session.run.repeat.target_cycles"> / {{ session.run.repeat.target_cycles }}</template> 轮</span>
      <span v-if="session.error" :title="session.error" class="session-warning">{{ session.error.split(':')[0] }}</span>
      <span v-if="session.run?.unresolved_inputs?.length" class="session-warning">{{ session.run.unresolved_inputs.length }} 次输入结果未确认</span>
    </div>
    <div class="command-row">
      <button class="icon-button" title="刷新运行状态" aria-label="刷新运行状态" @click="session.refresh"><RefreshCw :size="16" /></button>
      <button class="button danger" :disabled="session.stopping" @click="session.requestStop"><CircleStop :size="16" />{{ session.stopping ? '停止请求中' : '停止' }}</button>
    </div>
  </section>
</template>
