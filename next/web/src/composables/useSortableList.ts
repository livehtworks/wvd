import Sortable from 'sortablejs';
import { onBeforeUnmount, watch, type Ref } from 'vue';

// Sortable 只负责手势；归还 DOM 后交给 Vue 更新数据，避免两套排序所有者。
export function useSortableList(element: Ref<HTMLElement | undefined>, disabled: () => boolean,
  itemSelector: string, moved: (from: number, to: number) => void) {
  let sortable: Sortable | undefined;
  let original: Element[] = [];
  watch(element, node => {
    sortable?.destroy();
    sortable = node ? new Sortable(node, {
      draggable: itemSelector, handle: '.drag-handle', animation: 140,
      forceFallback: true, fallbackOnBody: true, fallbackTolerance: 4,
      ghostClass: 'drag-placeholder', chosenClass: 'drag-selected',
      disabled: disabled(),
      onStart: () => { original = Array.from(node.children); },
      onEnd: event => {
        original.forEach(child => node.appendChild(child));
        const from = event.oldDraggableIndex, to = event.newDraggableIndex;
        if (!disabled() && from !== undefined && to !== undefined && from !== to) moved(from, to);
      },
    }) : undefined;
  }, { flush: 'post' });
  watch(disabled, value => sortable?.option('disabled', value));
  onBeforeUnmount(() => sortable?.destroy());
}
