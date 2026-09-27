<script setup lang="ts">
import type { WorkflowDefinition } from '../../api/types';
type Policy = WorkflowDefinition['checks'];
const props=defineProps<{modelValue?:Policy}>();
const emit=defineEmits<{'update:modelValue':[Policy]}>();
function update(key:string,value:unknown){emit('update:modelValue',{phase:'special',inherit:[],...props.modelValue,[key]:value});}
function enable(event:Event){emit('update:modelValue',(event.target as HTMLInputElement).checked?{phase:'special',inherit:['wvd-network-retry'],debounce_ms:1000,interval_ms:1000,protect_input:true}:undefined);}
</script>
<template>
 <details class="inspector-group"><summary>阶段检查策略</summary>
  <label class="field"><span>显式策略</span><input type="checkbox" :checked="!!modelValue" @change="enable" /></label>
  <template v-if="modelValue">
   <label class="field"><span>当前阶段</span><select :value="modelValue.phase" @change="update('phase',($event.target as HTMLSelectElement).value)"><option v-for="p in ['navigation','combat','chest','supply','exception','special','business']" :key="p" :value="p">{{({navigation:'导航',combat:'战斗',chest:'宝箱',supply:'补给',exception:'异常',special:'特殊',business:'业务'} as Record<string,string>)[p]}}</option></select></label>
   <label class="field"><span>继承规则</span><input :value="modelValue.inherit.join(', ')" @change="update('inherit',($event.target as HTMLInputElement).value.split(',').map(v=>v.trim()).filter(Boolean))" /></label>
   <label class="field"><span>未知去抖毫秒</span><input type="number" min="0" :value="modelValue.debounce_ms??1000" @change="update('debounce_ms',Number(($event.target as HTMLInputElement).value))" /></label>
   <label class="field"><span>诊断间隔毫秒</span><input type="number" min="1" :value="modelValue.interval_ms??1000" @change="update('interval_ms',Number(($event.target as HTMLInputElement).value))" /></label>
   <label class="field"><span>局部输入保护</span><input type="checkbox" :checked="modelValue.protect_input!==false" @change="update('protect_input',($event.target as HTMLInputElement).checked)" /></label>
  </template>
 </details>
</template>
