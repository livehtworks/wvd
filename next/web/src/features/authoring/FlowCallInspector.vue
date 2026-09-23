<script setup lang="ts">
import { computed } from 'vue';
import ArgumentFields from './ArgumentFields.vue';
import { groupedFlows, type FlowCall, type PublicFlowSummary, type Scalar } from './flowModel';
const props=defineProps<{modelValue:FlowCall;flows:PublicFlowSummary[];currentFlowId:string}>();
const emit=defineEmits<{'update:modelValue':[FlowCall];'open-definition':[string]}>();
const groups=computed(()=>groupedFlows(props.flows.filter(f=>f.id!==props.currentFlowId)));
const selected=computed(()=>props.flows.find(f=>f.id===props.modelValue.flow_id));
function select(event:Event){emit('update:modelValue',{flow_id:(event.target as HTMLSelectElement).value,arguments:{},extensions:{}});}
function changeSlot(slot:string,index:number,event:Event){
 const extensions=JSON.parse(JSON.stringify(props.modelValue.extensions??{})) as Record<string,FlowCall[]>;
 extensions[slot]??=[]; extensions[slot][index]={flow_id:(event.target as HTMLSelectElement).value,arguments:{}};
 emit('update:modelValue',{...props.modelValue,extensions});
}
function slotArguments(slot:string,index:number,args:Record<string,Scalar>){
 const extensions=JSON.parse(JSON.stringify(props.modelValue.extensions??{})) as Record<string,FlowCall[]>;
 extensions[slot][index]={...extensions[slot][index],arguments:args};emit('update:modelValue',{...props.modelValue,extensions});
}
function addSlot(slot:string){ const extensions=JSON.parse(JSON.stringify(props.modelValue.extensions??{}));
 extensions[slot]??=[];extensions[slot].push({flow_id:'',arguments:{}});emit('update:modelValue',{...props.modelValue,extensions});}
function removeSlot(slot:string,index:number){const extensions=JSON.parse(JSON.stringify(props.modelValue.extensions??{}));
 extensions[slot].splice(index,1);emit('update:modelValue',{...props.modelValue,extensions});}
</script>
<template>
 <div class="inspector-group">
  <h3>公共步骤 / 流程块</h3>
  <label class="field"><span>调用定义</span><select :value="modelValue.flow_id" @change="select">
   <option value="" disabled>选择公共定义</option>
   <optgroup v-for="group in groups" :key="group.category" :label="group.category"><option v-for="flow in group.items" :key="flow.id" :value="flow.id">{{flow.name}}</option></optgroup>
  </select></label>
  <p>这里修改的是本次调用参数，不会改动公共定义。</p>
  <ArgumentFields :fields="selected?.interface?.parameters??[]" :model-value="modelValue.arguments??{}"
    @update:model-value="emit('update:modelValue',{...modelValue,arguments:$event})" />
  <button v-if="selected" type="button" class="button secondary full" @click="emit('open-definition',selected.id)">展开 / 编辑公共定义</button>
  <section v-for="slot in selected?.slots??[]" :key="slot">
   <h4>附加步骤：{{slot}}</h4><p>默认不执行任何附加步骤；已有项按显示顺序执行。</p>
   <div v-for="(call,index) in modelValue.extensions?.[slot]??[]" :key="index">
    <select :value="call.flow_id" @change="changeSlot(slot,index,$event)"><option value="" disabled>选择附加流程</option><option v-for="flow in flows.filter(f=>f.id!==currentFlowId)" :key="flow.id" :value="flow.id">{{flow.name}}</option></select>
    <ArgumentFields :fields="flows.find(f=>f.id===call.flow_id)?.interface?.parameters??[]" :model-value="call.arguments??{}" @update:model-value="slotArguments(slot,index,$event)" />
    <button type="button" @click="emit('open-definition',call.flow_id)">编辑</button><button type="button" @click="removeSlot(slot,index)">移除</button>
   </div><button type="button" @click="addSlot(slot)">添加附加步骤</button>
  </section>
 </div>
</template>
