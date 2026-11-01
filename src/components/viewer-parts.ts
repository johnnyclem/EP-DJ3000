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

export const FACES = [
  { id: "left", label: "Left" },
  { id: "right", label: "Right" },
  { id: "back", label: "Back" },
  { id: "top", label: "Top" },
] as const;

export type FaceId = (typeof FACES)[number]["id"];

export const GEAR_CHOICES = [
  { id: "none", label: "None" },
  { id: "jog", label: "Jog" },
  { id: "ko", label: "K.O. II" },
  { id: "riddim", label: "Riddim" },
  { id: "mixer", label: "Mixer" },
  { id: "pedal", label: "Pedal" },
  { id: "ting", label: "Ting" },
] as const;

export type GearId = Exclude<(typeof GEAR_CHOICES)[number]["id"], "none">;

export type Slots = Record<FaceId, GearId | null>;

export const EMPTY_SLOTS: Slots = { left: null, right: "riddim", back: null, top: null };

