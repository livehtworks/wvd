<script setup lang="ts">
import ExecutionStatus from "./ExecutionStatus.vue";
import { useRunSession } from "../stores/useRunSession";
const session = useRunSession();
</script>
<template>
  <section class="run-diagnostics" aria-label="运行诊断">
    <div v-if="session.deviceError" class="notice warning" role="status">设备状态读取失败：{{ session.deviceError }}</div>
    <div v-if="session.error || session.run?.error_code || session.run?.message" class="notice error" role="alert">{{ session.error || `${session.run?.error_code ?? ''}: ${session.run?.message ?? ''}` }}</div>
    <div v-if="session.eventError" class="notice error" role="alert">事件日志读取失败：{{ session.eventError }}</div>
    <div v-if="session.run?.unresolved_inputs?.length" class="notice error" role="alert">{{ session.run.unresolved_inputs.length }} 次输入结果未确认，禁止自动重发。</div>
    <div v-if="session.run?.execution?.observation_recovery?.active" class="notice warning" role="status">观察恢复中：{{ session.run.execution.observation_recovery.code }}，{{ session.run.execution.observation_recovery.operation }}</div>
    <details>
      <summary>运行详情与诊断 · {{ session.run?.run_id ?? '暂无运行' }}</summary>
      <ExecutionStatus :run="session.run" />
      <div class="run-grid"><div><span>耗时</span><strong>{{ session.run?.elapsed_seconds ?? 0 }} 秒</strong></div><div><span>结果</span><strong>{{ session.run?.result ?? '未提供' }}</strong></div></div>
      <p v-if="session.run?.active_event">正在处理 {{ session.run.active_event.event_id }} · {{ session.run.active_event.phase ?? session.run.active_event.class }} · 第 {{ session.run.active_event.depth }} 层；原步骤 {{ session.run.suspended_step?.node_id ?? session.run.active_event.source_node }}，返回方式 {{ session.run.active_event.resume.mode }}</p>
      <p v-if="session.run?.call_stack?.length">调用层次：{{ session.run.call_stack.map(frame => frame.node_id).join(' → ') }}</p>
      <div v-if="session.run?.unresolved_inputs?.length" class="notice error">有 {{ session.run.unresolved_inputs.length }} 次输入结果未确认，流程不会自动重发<pre>{{ JSON.stringify(session.run.unresolved_inputs, null, 2) }}</pre></div>
      <p v-if="session.run?.outcome_category === 'external_blocked'">外部阻断：{{ session.run.message ?? session.run.error_code }}</p>
      <div v-if="session.run?.statistics" class="statistics"><span v-for="(value, key) in session.run.statistics" :key="key"><small>{{ key }}</small><strong>{{ value }}</strong></span></div>
      <div v-if="session.run?.diagnostics?.length" class="diagnostics"><figure v-for="item in session.run.diagnostics" :key="item.image_url ?? `${session.run.server_instance_id}/${session.run.run_id}/${item.id ?? item.label}`"><img v-if="item.image_url" :src="item.image_url" :alt="item.label ?? '诊断截图'" /><div v-else class="diagnostic-placeholder">{{ item.status ?? '未保存图片' }}</div><figcaption><strong>{{ item.label ?? item.id }}</strong><span v-if="item.stage || item.node_id">{{ item.stage ?? '阶段未知' }} · {{ item.node_id ?? '节点未知' }}</span><span v-if="item.frame_age_ms !== undefined">帧龄 {{ item.frame_age_ms }} ms</span><span v-if="item.error">{{ item.error }}</span></figcaption></figure></div>
    </details>
  </section>
</template>
