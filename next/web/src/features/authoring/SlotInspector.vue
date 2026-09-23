<script setup lang="ts">
import FlowCallInspector from './FlowCallInspector.vue';
import type { FlowCall, PublicFlowSummary } from './flowModel';
const props=defineProps<{modelValue:{name:string;calls?:FlowCall[]};flows:PublicFlowSummary[];currentFlowId:string}>();
const emit=defineEmits<{'update:modelValue':[{name:string;calls:FlowCall[]}];'open-definition':[string]}>();
function set(index:number,value:FlowCall){const calls=[...(props.modelValue.calls??[])];calls[index]=value;emit('update:modelValue',{name:props.modelValue.name,calls});}
function move(index:number,delta:number){const calls=[...(props.modelValue.calls??[])];const to=index+delta;if(to<0||to>=calls.length)return;[calls[index],calls[to]]=[calls[to],calls[index]];emit('update:modelValue',{name:props.modelValue.name,calls});}
</script>
<template><section class="inspector-group"><h3>附加步骤插槽</h3>
<label class="field"><span>插槽名称</span><input :value="modelValue.name" @change="emit('update:modelValue',{name:($event.target as HTMLInputElement).value,calls:modelValue.calls??[]})" /></label>
<p>默认可以为空。调用者传入该插槽时，替换这里的默认步骤，不修改公共定义。</p>
<div v-for="(call,index) in modelValue.calls??[]" :key="index"><FlowCallInspector :model-value="call" :flows="flows" :current-flow-id="currentFlowId" @update:model-value="set(index,$event)" @open-definition="emit('open-definition',$event)" /><button type="button" @click="move(index,-1)">上移</button><button type="button" @click="move(index,1)">下移</button><button type="button" @click="emit('update:modelValue',{name:modelValue.name,calls:(modelValue.calls??[]).filter((_,i)=>i!==index)})">移除</button></div>
<button type="button" @click="emit('update:modelValue',{name:modelValue.name,calls:[...(modelValue.calls??[]),{flow_id:'',arguments:{}}]})">添加默认步骤</button>
</section></template>
