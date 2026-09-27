import { createApp, ui, signal, bind, storage, log, type Widget } from '@cpzero/core';

// Real LVGL widgets at the Zero's native resolution. No DOM or browser runtime.
const palette = { ink: '#0b1220', panel: '#152237', muted: '#94a8bf', text: '#edf4fc', accent: '#5de4c7' };
const count = signal('0');
let total = 0;
let page: Widget | undefined;

createApp({
  title: 'CPZeroJS / Field Kit',
  setup(root) {
    root.update({ bg: palette.ink, color: palette.text, padding: 8, gap: 4 });
    const header = ui.row(root, { width: '100%', height: 20, padding: 0, gap: 6, bg: palette.ink });
    ui.label(header, { text: 'CPZeroJS', color: palette.accent, fontSize: 16 });
    ui.label(header, { text: 'FIELD KIT', color: palette.muted, fontSize: 14 });
    const content = ui.column(root, { width: '100%', height: 75, padding: 5, gap: 4, bg: palette.panel });
    const status = ui.label(root, { text: '320 x 170 / native pixels', color: palette.muted, fontSize: 14 });

    function showCounter() {
      page?.dispose();
      page = ui.row(content, { width: '100%', height: '100%', padding: 0, gap: 12, bg: palette.panel });
      const numbers = ui.column(page, { width: 131, height: '100%', padding: 0, gap: 3, bg: palette.panel });
      ui.label(numbers, { text: 'TALLY COUNTER', color: palette.muted, fontSize: 14 });
      const value = ui.label(numbers, { text: count.value, color: palette.accent, fontSize: 20 });
      bind(value, 'text', count);
      ui.label(numbers, { text: 'Saved on device', color: palette.muted, fontSize: 14 });
      const controls = ui.column(page, { grow: 1, height: '100%', padding: 0, gap: 5, bg: palette.panel });
      ui.button(controls, { text: '+ Add one', width: '100%', height: 28, bg: palette.accent, color: palette.ink, onPress() {
        count.value = String(++total);
        void storage.set('counter', total).catch(error => { log(String(error)); status.update({ text: 'Could not save counter' }); });
      }});
      ui.button(controls, { text: 'Reset', width: '100%', height: 27, bg: '#293d56', onPress() {
        total = 0; count.value = '0';
        void storage.set('counter', total).catch(error => log(String(error)));
      }});
    }

    function showAbout() {
      page?.dispose();
      page = ui.column(content, { width: '100%', height: '100%', padding: 0, gap: 4, bg: palette.panel });
      ui.label(page, { text: 'TypeScript. Native pixels.', color: palette.accent, fontSize: 16 });
      ui.label(page, { text: 'QuickJS + LVGL / no webview', fontSize: 14 });
      ui.label(page, { text: 'Edit this file. Save. See it live.', color: palette.muted, fontSize: 14 });
    }

    const nav = ui.row(root, { width: '100%', height: 25, padding: 0, gap: 5, bg: palette.ink });
    ui.button(nav, { text: 'Counter', width: 96, height: 24, padding: 2, bg: '#293d56', onPress: showCounter });
    ui.button(nav, { text: 'About SDK', width: 108, height: 24, padding: 2, bg: '#293d56', onPress: showAbout });
    showCounter();
    void storage.get<number>('counter').then(value => {
      if (typeof value === 'number' && Number.isFinite(value)) { total = value; count.value = String(total); }
    }).catch(error => log(String(error)));
  },
});
