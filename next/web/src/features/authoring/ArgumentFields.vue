<script setup lang="ts">
import type { PublicParameter, Scalar } from './flowModel';
const props = defineProps<{ fields: PublicParameter[]; modelValue: Record<string, Scalar> }>();
const emit = defineEmits<{ 'update:modelValue': [Record<string, Scalar>] }>();
function value(field: PublicParameter) { return Object.hasOwn(props.modelValue, field.name) ? props.modelValue[field.name] : field.default; }
function update(field: PublicParameter, event: Event) {
  const input = event.target as HTMLInputElement;
  const v: Scalar = field.choices ? field.choices[Number(input.value)] : field.type === 'boolean' ? input.checked
    : field.type === 'integer' || field.type === 'number' ? Number(input.value) : input.value;
  emit('update:modelValue', {...props.modelValue, [field.name]:v});
}
function reset(field: PublicParameter) { const next={...props.modelValue}; delete next[field.name]; emit('update:modelValue',next); }
</script>
<template>
  <label v-for="field in fields" :key="field.name" class="field">
    <span>{{ field.label ?? field.name }}</span>
    <select v-if="field.choices" :value="field.choices.indexOf(value(field))" @change="update(field,$event)">
      <option v-for="(choice,index) in field.choices" :key="index" :value="index">{{ choice }}</option>
    </select>
    <input v-else-if="field.type === 'boolean'" type="checkbox" :checked="Boolean(value(field))" @change="update(field,$event)" />
    <input v-else :type="['integer','number'].includes(field.type)?'number':'text'" :step="field.type==='integer'?1:'any'"
      :value="String(value(field))" :min="field.min" :max="field.max" @change="update(field,$event)" />
    <button v-if="Object.hasOwn(modelValue,field.name)" type="button" class="button secondary" @click="reset(field)">沿用公共默认值</button>
  </label>
</template>
