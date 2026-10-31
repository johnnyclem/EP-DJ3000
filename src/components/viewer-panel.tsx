import { useCallback, useEffect, useState, type ReactElement } from "react";
import { VIEW_ITEMS, type ViewId } from "./viewer-parts";

type SceneProps = {
  focus: ViewId;
  watch: string;
  spin: boolean;
  onReady: () => void;
  onInteract: () => void;
};

export function ViewerPanel() {
  const [focus, setFocus] = useState<ViewId>("unit");
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

  return (
    <section className="flex flex-col gap-3">
      <div className="relative h-viewer w-full touch-none overflow-hidden border border-line bg-surface">
        {Scene ? (
          <Scene
            focus={focus}
            watch={`${focus}:${fitTick}`}
            spin={spin}
            onReady={onReady}
            onInteract={onInteract}
          />
        ) : null}
        {ready ? null : (
          <p className="pointer-events-none absolute inset-0 flex items-center justify-center text-sm text-muted">
            Loading the model
          </p>
        )}
      </div>
      <div className="flex items-center justify-between gap-3">
        <p className="text-sm text-muted">Drag to turn. Pinch to zoom.</p>
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
      <div className="flex gap-2 overflow-x-auto pb-1">
        {VIEW_ITEMS.map((item) => {
          const selected = item.id === focus;
          return (
            <button
              key={item.id}
              type="button"
              aria-pressed={selected}
              onClick={() => setFocus(item.id)}
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
    </section>
  );
}
