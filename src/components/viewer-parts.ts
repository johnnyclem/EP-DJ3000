export const VIEW_ITEMS = [
  { id: "unit", label: "Unit" },
  { id: "bottom", label: "Tray" },
  { id: "top", label: "Deck" },
  { id: "platter", label: "Platter" },
  { id: "fader", label: "Fader" },
  { id: "lock", label: "Lock" },
  { id: "knob", label: "Knob" },
  { id: "button", label: "Buttons" },
  { id: "plus", label: "Plus" },
  { id: "tab", label: "Tab" },
  { id: "bar", label: "Bar" },
  { id: "foot", label: "Feet" },
] as const;

export type ViewId = (typeof VIEW_ITEMS)[number]["id"];
