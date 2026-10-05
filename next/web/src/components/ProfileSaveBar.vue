<script setup lang="ts">
import { Save } from "@lucide/vue";
defineProps<{ label: string; dirty: boolean; saving: boolean; locked: boolean; pendingName?: boolean; error?: string; notice?: string }>();
defineEmits<{ save: []; reload: [] }>();
</script>

<template>
  <div class="profile-savebar" role="region" :aria-label="`${label}保存`">
    <div class="profile-save-status" aria-live="polite">
      <span v-if="saving">正在保存…</span>
      <span v-else-if="pendingName" class="status-chip warning">名称待确认</span>
      <span v-else-if="dirty" class="status-chip warning">有未保存更改</span>
      <span v-else>{{ label }}已保存</span>
      <p v-if="error" class="profile-save-error" role="alert">{{ error }}</p>
      <p v-else-if="notice" class="profile-save-success" role="status">{{ notice }}</p>
    </div>
    <button v-if="/PROFILE_.*CONFLICT/.test(error ?? '')" class="button secondary" :disabled="locked" @click="$emit('reload')">重新读取配置</button>
    <button class="button primary" :disabled="!dirty || locked || pendingName" @click="$emit('save')"><Save :size="16" />保存{{ label }}</button>
  </div>
</template>
