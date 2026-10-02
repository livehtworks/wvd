<script setup lang="ts">
import type { JsonObject } from '../../api/types';
import { computed } from 'vue';
interface Option {value:string|number;label:string;role?:string;category?:string;methods?:Record<string,{default:string;available:string[]}>}
const props=withDefaults(defineProps<{modelValue:JsonObject;templates?:Option[];resources?:Option[];recognizers?:Option[];positional?:boolean;depth?:number;resourceLocale?:string}>(),{depth:0});
const emit=defineEmits<{'update:modelValue':[JsonObject]}>();
const methodNames:Record<string,string>={template:'模板',ocr:'OCR文字',bright_mask:'亮色图标'};
const materialMethods=computed(()=>{
 const entry=props.resources?.find(r=>r.value===props.modelValue.id)?.methods;
 return entry?.[props.resourceLocale??'']??entry?.shared;
});
function material(event:Event){emit('update:modelValue',{mode:'semantic',id:(event.target as HTMLSelectElement).value});}
function method(event:Event){
 const next={...props.modelValue}; const value=(event.target as HTMLSelectElement).value;
 if(value) next.method=value; else delete next.method;
 emit('update:modelValue',next);
}
function change(key:string,value:unknown){emit('update:modelValue',{...props.modelValue,[key]:value});}
function mode(event:Event){const value=(event.target as HTMLSelectElement).value;
 emit('update:modelValue',value==='semantic'?{mode:value,id:''}:value==='location'?{mode:value,id:0}
  :value==='template'||value==='bright_mask'?{mode:value,image:'',threshold:0.8,roi:[0,0,900,1600],...(value==='bright_mask'?{min_brightness:145}:{})}
  :value==='ocr'?{mode:value,expected:[],language:props.resourceLocale==='zh-Hant'?'zh-Hant':'en',match:'exact',unique:true,threshold:0.9,roi:[0,0,900,1600]}
  :value==='skill_level'?{mode:value,level:1}
  :['all','any','not'].includes(value)?{mode:value,conditions:[{mode:'semantic',id:''}]}:{mode:value});}
function children(){return (props.modelValue.conditions??[]) as JsonObject[];}
function child(index:number,value:JsonObject){const next=[...children()];next[index]=value;change('conditions',next);}
function roi(index:number,event:Event){const next=[...((props.modelValue.roi??[0,0,900,1600]) as number[])];next[index]=Number((event.target as HTMLInputElement).value);change('roi',next);}
</script>
<template>
 <fieldset class="inspector-group"><legend>{{positional?'目标位置':'页面条件'}}</legend>
  <select :value="String(modelValue.mode??'')" @change="mode"><option value="semantic">语义资源</option><option value="template">模板</option><option value="bright_mask">亮色图标</option>
   <option value="ocr">OCR文字</option><template v-if="!positional"><option value="location">地点要求</option><option v-for="t in ['all','any','not']" :key="t" :value="t">{{t}}</option><option v-for="r in recognizers??[]" :key="String(r.value)" :value="r.value">{{r.label}}</option></template>
  </select>
  <template v-if="modelValue.mode==='semantic'">
   <select aria-label="语义素材" :value="String(modelValue.id??'')" @change="material"><option value="" disabled>选择目标</option><option v-for="r in (resources??[]).filter(r=>!positional||r.role==='position')" :key="String(r.value)" :value="r.value">{{r.category}} / {{r.label}}</option></select>
   <select v-if="materialMethods" aria-label="素材识别方式" :value="String(modelValue.method??'')" @change="method"><option value="">跟随素材（{{methodNames[materialMethods.default]??materialMethods.default}}）</option><option v-for="m in materialMethods.available" :key="m" :value="m">{{methodNames[m]??m}}</option><option v-if="modelValue.method&&!materialMethods.available.includes(String(modelValue.method))" :value="String(modelValue.method)" disabled>当前语言不可用：{{modelValue.method}}</option></select>
  </template>
  <select v-else-if="modelValue.mode==='location'" :value="Number(modelValue.id??0)" @change="change('id',Number(($event.target as HTMLSelectElement).value))"><option :value="0">不限定城市</option><option :value="1">王城</option><option :value="3">要塞（第三章）</option><option v-if="![0,1,3].includes(Number(modelValue.id))" :value="Number(modelValue.id)">未登记地点 {{modelValue.id}}</option></select>
  <template v-else-if="['template','bright_mask'].includes(String(modelValue.mode))"><select :value="String(modelValue.image??'')" @change="change('image',($event.target as HTMLSelectElement).value)"><option value="" disabled>选择图片</option><option v-for="r in templates??[]" :key="String(r.value)" :value="r.value">{{r.label}}</option></select><input aria-label="匹配阈值" type="number" min="0" max="1" step="0.01" :value="Number(modelValue.threshold??0.8)" @change="change('threshold',Number(($event.target as HTMLInputElement).value))" /><input v-if="modelValue.mode==='bright_mask'" aria-label="最小亮度" type="number" min="0" max="255" :value="Number(modelValue.min_brightness??145)" @change="change('min_brightness',Number(($event.target as HTMLInputElement).value))" /></template>
  <template v-else-if="modelValue.mode==='ocr'">
   <input aria-label="OCR预期文字" :value="((modelValue.expected??[]) as string[]).join('|')" @change="change('expected',($event.target as HTMLInputElement).value.split('|').map(s=>s.trim()).filter(Boolean))" />
   <select aria-label="OCR语言" :value="String(modelValue.language??'en')" @change="change('language',($event.target as HTMLSelectElement).value)"><option value="zh-Hant">繁体中文</option><option value="en">英文</option></select>
   <select aria-label="文字匹配方式" :value="String(modelValue.match??'contains')" @change="change('match',($event.target as HTMLSelectElement).value)"><option value="exact">全文匹配</option><option value="contains" :disabled="positional">包含文字</option></select>
   <label><input type="checkbox" :disabled="positional&&Boolean(modelValue.unique)" :checked="Boolean(modelValue.unique)" @change="change('unique',($event.target as HTMLInputElement).checked)" />唯一文字框</label>
   <input aria-label="OCR置信阈值" type="number" min="0" max="1" step="0.01" :value="Number(modelValue.threshold??0.3)" @change="change('threshold',Number(($event.target as HTMLInputElement).value))" />
  </template>
  <template v-else-if="modelValue.mode==='skill_level'"><input type="number" aria-label="技能等级" min="1" max="9" :value="Number(modelValue.level??1)" @change="change('level',Number(($event.target as HTMLInputElement).value))" /><label><input type="checkbox" :checked="Boolean(modelValue.selected)" @change="change('selected',($event.target as HTMLInputElement).checked)" />仅选中态</label></template>
  <div v-if="['template','bright_mask','ocr'].includes(String(modelValue.mode))" class="roi-grid"><input v-for="(_,i) in [0,1,2,3]" :key="i" type="number" :aria-label="['X','Y','宽','高'][i]" :value="Number(((modelValue.roi??[0,0,900,1600]) as number[])[i])" @change="roi(i,$event)" /></div>
  <template v-if="['all','any','not'].includes(String(modelValue.mode))"><template v-if="depth<8"><div v-for="(item,i) in children()" :key="i"><ConditionEditor :model-value="item" :templates="templates" :resources="resources" :recognizers="recognizers" :resource-locale="resourceLocale" :depth="depth+1" @update:model-value="child(i,$event)" /><button v-if="modelValue.mode!=='not'" type="button" @click="change('conditions',children().filter((_,j)=>j!==i))">移除条件</button></div><button v-if="modelValue.mode!=='not'" type="button" @click="change('conditions',[...children(),{mode:'semantic',id:''}])">增加条件</button></template><p v-else>达到现有条件深度上限，不能继续嵌套。</p></template>
 </fieldset>
</template>
