<script setup lang="ts">
import type { Item } from "../api/types";
defineProps<{ items: Item[]; selected?: string }>();
defineEmits<{ select: [item: Item] }>();
const labels: Record<string, string> = {
  function: "函数",
  config: "配置",
  task: "任务",
  quest_field: "任务字段",
  entry: "操作入口",
  branch: "条件分支",
  model: "数据结构",
  lambda: "回调",
  runtime_field: "运行字段",
  data_field: "字典字段",
  operation: "构建 / 更新",
  asset: "资源",
};
</script>
<template>
  <div class="table-scroll">
    <table aria-label="功能迁移清单">
      <thead>
        <tr>
          <th>基线项</th>
          <th>类型</th>
          <th>来源</th>
          <th>迁移归属</th>
          <th>状态</th>
        </tr>
      </thead>
      <tbody>
        <tr
          v-for="item in items"
          :key="item.id"
          :class="{ selected: selected === item.id }"
        >
          <td>
            <button class="row-link" @click="$emit('select', item)">
              {{ item.name || item.task_title || item.legacy_symbol }}
            </button>
          </td>
          <td>{{ labels[item.kind] }}</td>
          <td class="mono">{{ item.source_ref }}</td>
          <td class="mono">{{ item.new_owner }}</td>
          <td><span class="tag">待迁移</span></td>
        </tr>
        <tr v-if="!items.length">
          <td colspan="5" class="empty">没有匹配项</td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
