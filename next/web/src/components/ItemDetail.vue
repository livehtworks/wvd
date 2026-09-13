<script setup lang="ts">
import { X } from "@lucide/vue";
import type { Item } from "../api/types";
defineProps<{ item: Item }>();
defineEmits<{ close: [] }>();
const imageUrl = (path: string) =>
  "/reference-assets/" +
  path
    .replace("resources/images/", "")
    .split("/")
    .map(encodeURIComponent)
    .join("/");
</script>
<template>
  <aside class="detail" aria-label="基线项详情">
    <header>
      <h2>{{ item.name || item.task_title || item.legacy_symbol }}</h2>
      <button
        class="icon-button"
        title="关闭详情"
        aria-label="关闭详情"
        @click="$emit('close')"
      >
        <X :size="18" />
      </button>
    </header>
    <img
      v-if="item.path?.endsWith('.png')"
      class="asset-preview"
      :src="imageUrl(item.path)"
      :alt="item.path"
    />
    <dl>
      <dt>原有语义</dt>
      <dd>{{ item.original_semantics }}</dd>
      <dt>源位置</dt>
      <dd class="mono">{{ item.source_ref }}</dd>
      <dt>新归属</dt>
      <dd class="mono">{{ item.new_owner }}</dd>
      <dt>拟定入口</dt>
      <dd class="mono">{{ item.new_entry }}</dd>
      <dt>验收标识</dt>
      <dd class="mono">{{ item.acceptance_ids.join(", ") }}</dd>
    </dl>
    <h3>结构化记录</h3>
    <pre>{{ JSON.stringify(item, null, 2) }}</pre>
  </aside>
</template>
