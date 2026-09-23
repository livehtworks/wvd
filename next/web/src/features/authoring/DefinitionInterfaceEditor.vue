<script setup lang="ts">
import { computed } from 'vue';
import type { GraphNode, PublicInterface, PublicParameter, Scalar } from './flowModel';
const props=defineProps<{modelValue?:PublicInterface;nodes:GraphNode[]}>();
const emit=defineEmits<{'update:modelValue':[PublicInterface]}>();
const rows=computed(()=>props.modelValue?.parameters??[]);
const copy=<T,>(v:T):T=>JSON.parse(JSON.stringify(v)) as T;
function header(key:'kind'|'category',event:Event){emit('update:modelValue',{...props.modelValue,[key]:(event.target as HTMLInputElement).value});}
function update(index:number,key:string,value:unknown){const parameters=copy(rows.value);parameters[index]={...parameters[index],[key]:value};emit('update:modelValue',{...props.modelValue,parameters});}
function add(){const parameters=copy(rows.value);let n=1;while(parameters.some(p=>p.name===`parameter${n}`))n++;
 parameters.push({name:`parameter${n}`,label:'新参数',type:'string',default:'',bindings:[]});emit('update:modelValue',{...props.modelValue,parameters});}
function remove(index:number){emit('update:modelValue',{...props.modelValue,parameters:rows.value.filter((_,i)=>i!==index)});}
function fields(nodeId:string){const node=props.nodes.find(n=>n.id===nodeId);const result:Array<{path:string;value:Scalar}>=[];
 const walk=(value:unknown,path:string)=>{if(value===null)return;if(['boolean','number','string'].includes(typeof value)){result.push({path,value:value as Scalar});return;}
 if(typeof value==='object')for(const [key,item] of Object.entries(value as Record<string,unknown>)){
  if(['flow_id','binding','expected_revision','calls','extensions'].includes(key))continue;
  walk(item,`${path}/${key.replace(/~/g,'~0').replace(/\//g,'~1')}`);
 }}; if(node)walk(node.data.parameters,'');return result;}
function addBinding(index:number){update(index,'bindings',[...rows.value[index].bindings,{node:'',path:''}]);}
function bind(index:number,bindingIndex:number,key:'node'|'path',event:Event){const bindings=copy(rows.value[index].bindings);
 bindings[bindingIndex][key]=(event.target as HTMLSelectElement).value;if(key==='node')bindings[bindingIndex].path='';update(index,'bindings',bindings);}
function defaultValue(index:number,event:Event){const field=rows.value[index];const input=event.target as HTMLInputElement;
 update(index,'default',field.type==='boolean'?input.checked:['integer','number'].includes(field.type)?Number(input.value):input.value);}
function changeType(index:number,event:Event){const parameters=copy(rows.value);const type=(event.target as HTMLSelectElement).value as PublicParameter['type'];
 parameters[index]={...parameters[index],type,default:type==='boolean'?false:['integer','number'].includes(type)?0:''};
 delete parameters[index].choices;emit('update:modelValue',{...props.modelValue,parameters});}
</script>
<template>
 <details class="inspector-group"><summary>公共定义接口</summary>
  <p>修改这里会改变公共定义；调用节点中的参数只影响本次调用。</p>
  <label class="field"><span>分类</span><input :value="modelValue?.category??''" @change="header('category',$event)" /></label>
  <label class="field"><span>定义类型</span><select :value="modelValue?.kind??'task'" @change="header('kind',$event)"><option value="step">公共步骤</option><option value="block">流程块</option><option value="task">任务</option></select></label>
  <section v-for="(field,index) in rows" :key="index">
   <label class="field"><span>参数标识</span><input :value="field.name" @change="update(index,'name',($event.target as HTMLInputElement).value)" /></label>
   <label class="field"><span>显示名称</span><input :value="field.label??field.name" @change="update(index,'label',($event.target as HTMLInputElement).value)" /></label>
   <label class="field"><span>类型</span><select :value="field.type" @change="changeType(index,$event)"><option v-for="t in ['boolean','integer','number','string','resource']" :key="t" :value="t">{{t}}</option></select></label>
   <label class="field"><span>默认值</span><input v-if="field.type==='boolean'" type="checkbox" :checked="Boolean(field.default)" @change="defaultValue(index,$event)" /><input v-else :type="['integer','number'].includes(field.type)?'number':'text'" :value="String(field.default)" @change="defaultValue(index,$event)" /></label>
   <div v-for="(binding,i) in field.bindings" :key="i">
    <select :value="binding.node" @change="bind(index,i,'node',$event)"><option value="" disabled>参数作用的步骤</option><option v-for="n in nodes" :key="n.id" :value="n.id">{{n.data.label}}</option></select>
    <select :value="binding.path" @change="bind(index,i,'path',$event)"><option value="" disabled>步骤字段</option><option v-for="f in fields(binding.node)" :key="f.path" :value="f.path">{{f.path}}</option></select>
    <button type="button" @click="update(index,'bindings',field.bindings.filter((_,j)=>j!==i))">移除绑定</button>
   </div><button type="button" @click="addBinding(index)">绑定步骤字段</button><button type="button" @click="remove(index)">删除参数</button>
  </section><button type="button" @click="add">公开新参数</button>
 </details>
</template>
