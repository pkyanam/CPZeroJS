# Expandable and modal UI components

The `@cpzero/core` components provide native, keyboard-operable disclosure sections and focus scopes. They use LVGL widgets and focus commands, with no browser or DOM observer.

## Lazy disclosure

```ts
import { disclosure } from "@cpzero/core";

const tools = disclosure(panel, {
  title: "Tools",
  expanded: false,
  header: { bg: "#182334" },
  body: { padding: 6, gap: 4 },
  render(body) {
    ui.button(body, { text: "Run", onPress: runAction });
  },
  onToggle(expanded) { log(expanded ? "Tools opened" : "Tools closed"); },
});

// Later:
tools.setTitle("Developer tools");
tools.setExpanded(true);
```

`disclosure(parent, options)` returns a controller with `widget`, its focusable native-button `header`, an `expanded` getter, `setExpanded`, `setTitle`, and `dispose`. The body is created only on first expansion, then its widgets are disposed when collapsed. Reopening invokes `render` again with a fresh body widget. Parent/app teardown also disposes the subtree. `onToggle` runs only when the expanded state changes. Enter and Space use the host's standard button activation path.

## Focus scope

Use a native container as the scope, preferably a dialog or temporary modal panel:

```ts
import { focusScope } from "@cpzero/core";

const scope = focusScope(dialog, {
  initial: confirmButton,
  onEscape: closeDialog,
});

// Close the modal:
scope.release(); // same as scope.dispose()
```

The host traps Tab and Shift+Tab among focusable descendants of `dialog`; `initial` selects the initial focus target. Escape goes only to the newest active scope that supplies `onEscape`, which supports nested dialogs. Release scopes in last-opened, first-closed order. Releasing the scope restores normal focus behavior. The scope also releases automatically when its container or app is disposed. The initial widget must belong to the same app and must still be live; the native host verifies that it belongs inside the container.

Keep dialog controls reachable with Tab and provide an explicit close or cancel button as well as Escape. Mouse clicks continue to work; the native focus trap handles keyboard focus independently of pointer input.
