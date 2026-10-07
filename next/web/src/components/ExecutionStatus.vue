<script setup lang="ts">
import { computed, ref, watch, onUnmounted } from 'vue';
import type { RunState } from '../api/types';
const props=defineProps<{run?:RunState}>();
const execution=computed(()=>props.run?.execution);
const recovery=computed(()=>execution.value?.observation_recovery);
const recoveryLabels:Record<string,string>={retrying_observation:'正在恢复观察',awaiting_input_validation:'等待输入前复核',
 observation_restored:'已恢复',input_context_readable:'输入前复核已恢复',original_result_confirmed:'原输入结果已确认',
 failed:'观察恢复失败或已耗尽',cancelled:'恢复已停止',action_no_longer_needed:'原动作已不再需要',device_reconnected:'设备已重连，正在恢复游戏'};
const elapsed=ref(0);
let recoveryStarted=0;
watch([()=>props.run?.run_id,()=>recovery.value?.active],()=>{
 recoveryStarted=Date.now(); elapsed.value=0;
},{immediate:true});
const timer=setInterval(()=>{if(recovery.value?.active) elapsed.value=Math.floor((Date.now()-recoveryStarted)/1000);},1000);
onUnmounted(()=>clearInterval(timer));
const phases:Record<string,string>={business:'任务',navigation:'导航',combat:'战斗',chest:'宝箱',supply:'补给',special:'特殊页面',exception:'异常处理'};
const waits:Record<string,string>={normal:'正常等待',unknown:'未知页面诊断',input_result:'等待输入结果',explicit:'阶段等待',none:'推进步骤'};
const diagnosticReason=computed(()=>{
 const diagnostic=execution.value?.last_diagnostic;
 if(!diagnostic||typeof diagnostic!=='object'||!('reason' in diagnostic)) return undefined;
 const value=diagnostic.reason;
 return typeof value==='string'?value:undefined;
});
</script>
<template>
 <dl v-if="execution" class="execution-status">
  <div><dt>主目标</dt><dd>{{execution.main_objective?.farm_target_text??'未提供'}} · 路线点 {{execution.main_objective?.task_step??'未知'}}</dd></div>
  <div><dt>当前阶段</dt><dd>{{phases[execution.phase??'']??execution.phase??'历史记录未提供'}} · {{waits[execution.wait_state??'']??'历史记录未提供'}}</dd></div>
  <div><dt>当前公共块</dt><dd>{{execution.current_block??'未提供'}}</dd></div>
  <div v-if="recovery"><dt>观察恢复</dt><dd>{{recoveryLabels[recovery.state]??recovery.state}}<template v-if="recovery.active"> · 本页已观察 {{elapsed}} 秒 · 第 {{recovery.failures}} 次读取失败</template></dd></div>
  <div v-if="recovery"><dt>原始读取故障</dt><dd>{{recovery.code}} · {{recovery.operation}} · 耗时 {{recovery.command_elapsed_ms}} / 期限 {{recovery.command_timeout_ms}} 毫秒</dd></div>
  <div v-if="recovery?.command_details?.command"><dt>故障命令</dt><dd>{{recovery.command_details.command}}</dd></div>
  <div v-if="execution.return_targets?.length"><dt>交接后继续</dt><dd>{{execution.return_targets.join(' → ')}}</dd></div>
  <div v-if="diagnosticReason"><dt>最近诊断原因</dt><dd>{{diagnosticReason}}</dd></div>
 </dl>
</template>
<style scoped>
.execution-status{display:grid;gap:6px;margin:10px 0;font-size:13px}
.execution-status>div{display:grid;grid-template-columns:100px minmax(0,1fr);gap:12px}
dt{color:#60716e}dd{margin:0;overflow-wrap:anywhere}
</style>
