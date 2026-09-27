import { bind, createApp, onCleanup, services, signal, ui } from '@cpzero/core';

type CatalogItem = { id: number; title: string; category: string };
const items: CatalogItem[] = Array.from({ length: 10_000 }, (_, index) => ({
  id: index + 1,
  title: `Field note ${String(index + 1).padStart(5, '0')}`,
  category: ['Sensor', 'Display', 'Control', 'Storage'][index % 4],
}));

// Typed local adapters keep app logic portable. Native-backed adapters can use the same API.
const removeCatalogService = services.register('catalog', {
  async getItem(id: number): Promise<CatalogItem | undefined> {
    return items[id - 1];
  },
});

const status = signal('10,000 items / 3 visible rows');
createApp({
  title: 'CPZeroJS / Catalog',
  setup(root) {
    onCleanup(removeCatalogService);
    root.update({ bg: '#0b1220', color: '#edf4fc', padding: 7, gap: 3 });
    const heading = ui.row(root, { width: '100%', height: 19, padding: 0, gap: 6 });
    ui.label(heading, { text: 'FIELD CATALOG', color: '#5de4c7', fontSize: 16 });
    ui.label(heading, { text: '10K', color: '#94a8bf', fontSize: 14 });

    ui.pagedList(ui.column(root, { width: '100%', height: 103, padding: 0 }), {
      items,
      width: '100%',
      height: 69,
      rowHeight: 23,
      gap: 2,
      renderRow(item, _index, row) {
        ui.button(row, {
          text: `${String(item.id).padStart(5, '0')}  ${item.category} field note`,
          width: '100%',
          height: 23,
          padding: 1,
          bg: '#152237',
          onPress() {
            void services.call<CatalogItem | undefined>('catalog', 'getItem', item.id).then(selected => {
              if (selected) status.value = `Selected ${selected.title} / ${selected.category}`;
            });
          },
        });
      },
    });
    const info = ui.label(root, { text: status.value, color: '#94a8bf', fontSize: 14 });
    bind(info, 'text', status);
    void services.call<CatalogItem | undefined>('catalog', 'getItem', 1).then(item => {
      if (item) status.value = `${items.length} items / ${item.category} entries`;
    });
  },
});
