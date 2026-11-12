import { useCallback, useEffect, useState, type ReactElement } from "react";
import {
  EMPTY_SLOTS,
  FACES,
  GEAR_CHOICES,
  VIEW_ITEMS,
  type FaceId,
  type GearId,
  type Slots,
  type ViewId,
} from "./viewer-parts";
import { WIDE_PARTS, type WideLayout, type WidePart } from "./wide-deck";
import { CARRY_PARTS, type CarryPart } from "./carry-deck";
import { GO_PARTS, type GoPart } from "./go-deck";

type Mode = "rig" | "unit" | "wide" | "carry" | "go";

type SceneProps = {
  mode: Mode;
  focus: ViewId;
  slots: Slots;
  activeFace: FaceId;
  watch: string;
  spin: boolean;
  layout: WideLayout;
  wideFocus: WidePart;
  carryFocus: CarryPart;
  goFocus: GoPart;
  onReady: () => void;
  onInteract: () => void;
  onPickFace: (face: FaceId) => void;
};

function slotKey(slots: Slots) {
  return FACES.map((face) => slots[face.id] ?? "-").join(".");
}

export function ViewerPanel() {
  const [mode, setMode] = useState<Mode>("go");
  const [unitFocus, setUnitFocus] = useState<ViewId>("unit");
  const [layout, setLayout] = useState<WideLayout>("twin");
  const [wideFocus, setWideFocus] = useState<WidePart>("unit");
  const [carryFocus, setCarryFocus] = useState<CarryPart>("unit");
  const [goFocus, setGoFocus] = useState<GoPart>("unit");
  const [slots, setSlots] = useState<Slots>(EMPTY_SLOTS);
  const [activeFace, setActiveFace] = useState<FaceId>("right");
  const [spin, setSpin] = useState(true);
  const [fitTick, setFitTick] = useState(0);
  const [ready, setReady] = useState(false);
  const [Scene, setScene] = useState<((props: SceneProps) => ReactElement) | null>(null);

  useEffect(() => {
    let live = true;
    import("./unit-scene").then((mod) => {
      if (live) setScene(() => mod.UnitScene);
    });
    return () => {
      live = false;
    };
  }, []);

  const onReady = useCallback(() => setReady(true), []);
  const onInteract = useCallback(() => setSpin(false), []);
  const onPickFace = useCallback((face: FaceId) => setActiveFace(face), []);

  function assign(next: GearId | null) {
    setSlots((prev) => {
      const slotsNext: Slots = { ...prev, [activeFace]: next };
      if (next) {
        for (const face of FACES) {
          if (face.id !== activeFace && slotsNext[face.id] === next) slotsNext[face.id] = null;
        }
      }
      return slotsNext;
    });
    setFitTick((n) => n + 1);
  }

  const faceLabel = FACES.find((face) => face.id === activeFace)?.label ?? "Right";

  return (
    <section className="flex flex-col gap-3">
      <div className="relative h-viewer w-full touch-none overflow-hidden border border-line bg-surface">
        {Scene ? (
          <Scene
            mode={mode}
            focus={unitFocus}
            slots={slots}
            activeFace={activeFace}
            watch={`${mode}:${unitFocus}:${wideFocus}:${carryFocus}:${goFocus}:${layout}:${slotKey(slots)}:${fitTick}`}
            spin={spin}
            layout={layout}
            wideFocus={wideFocus}
            carryFocus={carryFocus}
            goFocus={goFocus}
            onReady={onReady}
            onInteract={onInteract}
            onPickFace={onPickFace}
          />
        ) : null}
        {ready ? null : (
          <p className="pointer-events-none absolute inset-0 flex items-center justify-center text-sm text-muted">
            Loading the model
          </p>
        )}
      </div>
      <div className="flex items-center justify-between gap-3">
        <p className="min-w-0 text-sm text-muted">
          {mode === "go"
            ? goFocus === "unit"
              ? "150 × 74 × 16. Prints flat on a Mars 5 Ultra. Grey shell, cream bezel, orange knob and fader, black caps."
              : `${GO_PARTS.find((part) => part.id === goFocus)?.label ?? "Part"}. The rest of the unit is hidden.`
            : mode === "carry"
            ? carryFocus === "unit"
              ? "230 × 300 × 22. Brighter stone, cyan screen, orange hardware. Fader is 125 mm on the right. Caps are the 12 mm latching switches."
              : `${CARRY_PARTS.find((part) => part.id === carryFocus)?.label ?? "Part"}. The rest of the deck is hidden.`
            : mode === "wide"
            ? wideFocus === "unit"
              ? layout === "twin"
                ? "176 × 240.5. Two 76 mm platters. Headphone jack is left of the crossfader. Card slot is the dark mark under the rear-left, clear of the magnet."
                : "176 × 240.5. One 152 mm platter, shifted left for the pitch fader. Headphone jack is centered. Card slot is the dark mark under the rear-left."
              : `${WIDE_PARTS.find((part) => part.id === wideFocus)?.label ?? "Part"}. The rest of the deck is hidden.`
            : mode === "rig"
              ? `${faceLabel}. Pick what snaps on.`
              : "Drag to turn. Pinch to zoom."}
        </p>
        <button
          type="button"
          className="h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase"
          onClick={() => {
            setSpin(true);
            setFitTick((n) => n + 1);
          }}
        >
          Reset
        </button>
      </div>
      <div className="flex gap-2">
        {(
          [
            ["rig", "Setup"],
            ["unit", "Parts"],
            ["wide", "Wide"],
            ["carry", "Carry"],
            ["go", "Go"],
          ] as const
        ).map(([item, label]) => (
          <button
            key={item}
            type="button"
            aria-pressed={mode === item}
            onClick={() => setMode(item)}
            className={
              mode === item
                ? "h-11 flex-1 bg-fg font-mono text-xs tracking-wide text-bg uppercase"
                : "h-11 flex-1 border border-line font-mono text-xs tracking-wide text-fg uppercase"
            }
          >
            {label}
          </button>
        ))}
      </div>
      {mode === "wide" ? (
        <div className="flex gap-2">
          {(
            [
              ["twin", "Twin 3"],
              ["six", "One 6"],
            ] as const
          ).map(([item, label]) => (
            <button
              key={item}
              type="button"
              aria-pressed={layout === item}
              onClick={() => {
                setLayout(item);
                setSpin(true);
                setFitTick((n) => n + 1);
              }}
              className={
                layout === item
                  ? "h-11 flex-1 bg-accent font-mono text-xs tracking-wide text-accent-fg uppercase"
                  : "h-11 flex-1 border border-line font-mono text-xs tracking-wide text-fg uppercase"
              }
            >
              {label}
            </button>
          ))}
        </div>
      ) : null}
      {mode === "rig" ? (
        <>
          <div className="grid grid-cols-4 gap-2">
            {FACES.map((item) => {
              const selected = item.id === activeFace;
              const filled = slots[item.id] != null;
              return (
                <button
                  key={item.id}
                  type="button"
                  aria-pressed={selected}
                  onClick={() => setActiveFace(item.id)}
                  className={
                    selected
                      ? "h-11 bg-accent font-mono text-xs tracking-wide text-accent-fg uppercase"
                      : "h-11 border border-line font-mono text-xs tracking-wide text-fg uppercase"
                  }
                >
                  {item.label}
                  {filled ? " ·" : ""}
                </button>
              );
            })}
          </div>
          <div className="flex gap-2 overflow-x-auto pb-1">
            {GEAR_CHOICES.map((item) => {
              const selected = item.id === "none" ? slots[activeFace] == null : slots[activeFace] === item.id;
              return (
                <button
                  key={item.id}
                  type="button"
                  aria-pressed={selected}
                  onClick={() => assign(item.id === "none" ? null : item.id)}
                  className={
                    selected
                      ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase"
                      : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase"
                  }
                >
                  {item.label}
                </button>
              );
            })}
          </div>
        </>
      ) : mode === "wide" ? (
        <div className="flex gap-2 overflow-x-auto pb-1">
          {WIDE_PARTS.map((item) => {
            const selected = item.id === wideFocus;
            return (
              <button
                key={item.id}
                type="button"
                aria-pressed={selected}
                onClick={() => {
                  setWideFocus(item.id);
                  setSpin(true);
                  setFitTick((n) => n + 1);
                }}
                className={
                  selected
                    ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase"
                    : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase"
                }
              >
                {item.label}
              </button>
            );
          })}
        </div>
      ) : mode === "go" ? (
        <div className="flex gap-2 overflow-x-auto pb-1">
          {GO_PARTS.map((item) => {
            const selected = item.id === goFocus;
            return (
              <button
                key={item.id}
                type="button"
                aria-pressed={selected}
                onClick={() => {
                  setGoFocus(item.id);
                  setSpin(true);
                  setFitTick((n) => n + 1);
                }}
                className={
                  selected
                    ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase"
                    : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase"
                }
              >
                {item.label}
              </button>
            );
          })}
        </div>
      ) : mode === "carry" ? (
        <div className="flex gap-2 overflow-x-auto pb-1">
          {CARRY_PARTS.map((item) => {
            const selected = item.id === carryFocus;
            return (
              <button
                key={item.id}
                type="button"
                aria-pressed={selected}
                onClick={() => {
                  setCarryFocus(item.id);
                  setSpin(true);
                  setFitTick((n) => n + 1);
                }}
                className={
                  selected
                    ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase"
                    : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase"
                }
              >
                {item.label}
              </button>
            );
          })}
        </div>
      ) : mode === "unit" ? (
        <div className="flex gap-2 overflow-x-auto pb-1">
          {VIEW_ITEMS.map((item) => {
            const selected = item.id === unitFocus;
            return (
              <button
                key={item.id}
                type="button"
                aria-pressed={selected}
                onClick={() => setUnitFocus(item.id)}
                className={
                  selected
                    ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase"
                    : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase"
                }
              >
                {item.label}
              </button>
            );
          })}
        </div>
      ) : null}
    </section>
  );
}
