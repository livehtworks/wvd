<script setup lang="ts">
import { computed, ref } from 'vue';
import { Camera, Plus, Trash2, Upload, X } from '@lucide/vue';
import type { EnemyRule, SpecialCombatSettings } from '../api/types';

const props = defineProps<{ model: SpecialCombatSettings; strategies: string[]; locked: boolean;
  capture: () => Promise<string>; captureDisabled: boolean; initialStrategy: string; normalStrategy: string }>();
const dialog = ref<HTMLDialogElement>(), image = ref<HTMLImageElement>(), file = ref<HTMLInputElement>();
const source = ref(''), name = ref(''), strategy = ref(''), error = ref(''), busy = ref(false);
const box = ref({ x: 0, y: 0, width: 0, height: 0 }), start = ref<{ x: number; y: number }>();
const width = ref(0), height = ref(0);
const rules = computed(() => props.model.rules ?? []);
function portraitUrl(rule: EnemyRule) {
  if (rule.portrait_png_base64) return `data:image/png;base64,${rule.portrait_png_base64}`;
  const key = Array.from(new TextEncoder().encode(rule.portrait_image), byte => byte.toString(16).padStart(2, '0')).join('');
  return `/api/v1/combat/portraits/${key}`;
}
const boxStyle = computed(() => ({ left: `${box.value.x / width.value * 100}%`, top: `${box.value.y / height.value * 100}%`,
  width: `${box.value.width / width.value * 100}%`, height: `${box.value.height / height.value * 100}%` }));
function open() {
  if (props.locked) return;
  source.value = ''; name.value = ''; strategy.value = props.initialStrategy || props.model.special_strategy || props.strategies[0] || '';
  error.value = ''; box.value = { x: 0, y: 0, width: 0, height: 0 }; dialog.value?.showModal();
}
function loaded() {
  width.value = image.value!.naturalWidth; height.value = image.value!.naturalHeight;
  if (width.value <= 160 && height.value <= 220) box.value = { x: 0, y: 0, width: width.value, height: height.value };
  else if (width.value !== 900 || height.value !== 1600) error.value = '请选择900×1600截图，或已裁好的小头像';
}
function point(event: PointerEvent) {
  const rect = image.value!.getBoundingClientRect();
  return { x: Math.round(Math.max(0, Math.min(width.value, (event.clientX - rect.left) / rect.width * width.value))),
    y: Math.round(Math.max(0, Math.min(height.value, (event.clientY - rect.top) / rect.height * height.value))) };
}
function begin(event: PointerEvent) { if (busy.value) return; start.value = point(event); image.value?.setPointerCapture(event.pointerId); }
function move(event: PointerEvent) {
  if (!start.value) return;
  const p = point(event), from = start.value;
  box.value = { x: Math.min(p.x, from.x), y: Math.min(p.y, from.y), width: Math.abs(p.x - from.x), height: Math.abs(p.y - from.y) };
}
function end(event: PointerEvent) { move(event); start.value = undefined; }
async function screenshot() {
  busy.value = true; error.value = ''; box.value = { x: 0, y: 0, width: 0, height: 0 };
  try { source.value = `${await props.capture()}?v=${Date.now()}`; }
  catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { busy.value = false; }
}
async function upload(event: Event) {
  const picked = (event.target as HTMLInputElement).files?.[0];
  if (!picked) return;
  error.value = ''; box.value = { x: 0, y: 0, width: 0, height: 0 };
  if (picked.size > 8 * 1024 * 1024) { error.value = '图片不能超过8MiB'; return; }
  const reader = new FileReader();
  reader.onload = () => { source.value = String(reader.result); };
  reader.readAsDataURL(picked);
  (event.target as HTMLInputElement).value = '';
}
async function confirm() {
  if (busy.value || props.locked) return;
  const rect = box.value, label = name.value.trim();
  if (!label || rules.value.some(rule => rule.name === label)) { error.value = '请输入不重复的怪物名称'; return; }
  if (!strategy.value || !props.strategies.includes(strategy.value)) { error.value = '请选择战斗方案'; return; }
  if (!image.value || rect.width < 16 || rect.height < 16 || rect.width > 160 || rect.height > 220) {
    error.value = '请框选16至160像素宽、16至220像素高的头像'; return;
  }
  if (width.value > 160 && (rect.x < 20 || rect.x + rect.width > 180 || rect.y < 45 || rect.y + rect.height > 900)) {
    error.value = '请仅框选左侧行动条里的头像，不包含名字和其它人物'; return;
  }
  busy.value = true; error.value = '';
  try {
  const canvas = document.createElement('canvas'); canvas.width = rect.width; canvas.height = rect.height;
  canvas.getContext('2d')!.drawImage(image.value, rect.x, rect.y, rect.width, rect.height, 0, 0, rect.width, rect.height);
  const encoded = canvas.toDataURL('image/png').split(',')[1];
  const bytes = Uint8Array.from(atob(encoded), c => c.charCodeAt(0));
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  const hash = Array.from(digest, value => value.toString(16).padStart(2, '0')).join('');
  const rule: EnemyRule = { id: crypto.randomUUID(), name: label, strategy: strategy.value,
    portrait_image: `custom/monster_${hash}`, portrait_png_base64: encoded };
  props.model.rules ??= [];
  // 新列表接管时保留已有的蝎女/单头像匹配，不静默退役原条件。
  if (!props.model.rules.length && props.model.portrait && props.model.portrait_image && props.model.special_strategy)
    props.model.rules.push({ id: crypto.randomUUID(), name: '原头像规则', strategy: props.model.special_strategy, portrait_image: props.model.portrait_image });
  props.model.rules.push(rule); props.model.portrait = true;
  if (!props.model.normal_strategy) props.model.normal_strategy =
    props.strategies.includes(props.normalStrategy) ? props.normalStrategy : props.strategies[0] || '';
  dialog.value?.close(); source.value = '';
  } catch (reason) { error.value = reason instanceof Error ? reason.message : String(reason); }
  finally { busy.value = false; }
}
function remove(index: number) {
  if (props.locked) return;
  props.model.rules?.splice(index, 1);
  // Empty explicit rules must not silently revive the old single-portrait condition.
  if (!props.model.rules?.length) props.model.portrait = false;
}
</script>

<template>
  <section class="enemy-rules" aria-label="怪物方案匹配">
    <div class="section-commands"><h3>怪物方案匹配</h3><button class="button secondary" :disabled="locked || rules.length >= 64" @click="open"><Plus :size="16" />添加怪物</button></div>
    <div v-for="(rule, index) in rules" :key="rule.id" class="enemy-rule">
      <span class="row-number">{{ index + 1 }}</span>
      <img :src="portraitUrl(rule)" :alt="rule.name" />
      <input v-model="rule.name" aria-label="怪物名称" :disabled="locked" />
      <select v-model="rule.strategy" aria-label="怪物战斗方案" :disabled="locked"><option v-for="item in strategies" :key="item" :value="item">{{ item }}</option></select>
      <button class="icon-button danger" :disabled="locked" title="删除怪物规则" aria-label="删除怪物规则" @click="remove(index)"><Trash2 :size="16" /></button>
    </div>
    <dialog ref="dialog" class="portrait-dialog" aria-label="添加怪物头像" @cancel="source = ''">
      <header><h3>添加怪物</h3><button class="icon-button" aria-label="关闭添加怪物" @click="dialog?.close()"><X :size="18" /></button></header>
      <div class="form-grid">
        <label class="field"><span>怪物名称</span><input v-model="name" aria-label="新怪物名称" /></label>
        <label class="field"><span>战斗方案</span><select v-model="strategy" aria-label="新怪物战斗方案"><option v-for="item in strategies" :key="item" :value="item">{{ item }}</option></select></label>
      </div>
      <div class="command-row"><button class="button secondary" :disabled="busy || captureDisabled" @click="screenshot"><Camera :size="16" />当前游戏截图</button><button class="button secondary" :disabled="busy" @click="file?.click()"><Upload :size="16" />选择图片</button><input ref="file" hidden type="file" accept="image/png,image/jpeg" @change="upload" /></div>
      <div v-if="source" class="portrait-crop"><img ref="image" :src="source" alt="待裁切的行动条头像" draggable="false" @load="loaded" @pointerdown.prevent="begin" @pointermove="move" @pointerup="end" @pointercancel="start = undefined" /><div class="crop-outline" :style="boxStyle" /></div>
      <p v-if="error" role="alert" class="notice error">{{ error }}</p>
      <footer><span v-if="source">{{ box.width }} × {{ box.height }}</span><button class="button primary" :disabled="busy || !source || locked" @click="confirm">确认添加</button></footer>
    </dialog>
  </section>
</template>
