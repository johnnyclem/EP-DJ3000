import { createFileRoute } from "@tanstack/react-router";
import { ViewerPanel } from "@/components/viewer-panel";
import { RenderGallery } from "@/components/render-gallery";

export const Route = createFileRoute("/")({ component: Home });

const PARTS = [
  ["top.stl", "Deck", "Grey"],
  ["bottom.stl", "Tray", "Grey"],
  ["platter.stl", "Platter", "Black"],
  ["fader.stl", "Fader cap", "Black"],
  ["fader_lock.stl", "Fader lock", "Black"],
  ["knob.stl", "Knob", "Orange"],
  ["button.stl", "Button ×2", "Black"],
  ["plus.stl", "Platter mark", "Orange"],
  ["tab.stl", "USB tab", "Orange"],
  ["bar.stl", "Name bar", "Orange"],
  ["foot.stl", "Foot ×4", "TPU"],
] as const;

function Home() {
  return (
    <main className="mx-auto flex min-h-screen max-w-lg flex-col gap-8 px-5 py-8">
      <header className="flex flex-col gap-3">
        <p className="font-mono text-xs tracking-widest text-accent uppercase">Unofficial companion</p>
        <h1 className="text-4xl leading-none font-semibold tracking-tight">Field Jog</h1>
        <p className="text-base leading-relaxed text-muted">
          A scratch-deck shell in the Sidekick footprint. 88 mm wide, 240 mm long,
          grey deck, orange marks, black jog and fader. Setup snaps gear onto the
          amp: left, right, back, and top.
        </p>
      </header>

      <ViewerPanel />

      <RenderGallery />

      <dl className="grid grid-cols-3 divide-x divide-line border border-line">
        <Spec k="Height" v="240 mm" />
        <Spec k="Width" v="88 mm" />
        <Spec k="Thick" v="16 mm" />
      </dl>

      <p className="text-sm leading-relaxed text-muted">
        The long edge matches the EP row, and the width matches a Sidekick. The 76 mm
        platter is centered, with 6 mm of deck on each side. The knob sits above it.
        It is 16 mm thick, the same as a K.O. II and a Sidekick, so the cases sit flush.
        The deck is not engraved. Type tore out of the first print. The top 45 mm
        is a blank plate over the battery bay, meant to be silkscreened. A thin slot
        in it takes a 0.91 inch 128×32 screen. Cue and play are round, lower left.
        Loop in and out sit above them, each with an LED. Tempo is the knob, not a fader.
        Pin holes are 16, 25, 48, 216 and 224 mm from the fader end, 8 mm off the
        bottom, on both long sides.
      </p>

      <a
        href="/field-jog/field-jog.zip"
        target="_blank"
        rel="noreferrer"
        className="flex h-12 items-center justify-center bg-accent font-semibold text-accent-fg"
      >
        Download print files
      </a>

      <section className="flex flex-col gap-3">
        <h2 className="font-mono text-xs tracking-widest text-muted uppercase">Parts</h2>
        <ul className="divide-y divide-line border border-line">
          {PARTS.map(([file, name, color]) => (
            <li key={file} className="flex items-center justify-between gap-3 px-3 py-3">
              <span className="flex flex-col">
                <span className="text-sm">{name}</span>
                <span className="font-mono text-xs text-muted">{color}</span>
              </span>
              <a
                href={`/field-jog/stl/${file}`}
                target="_blank"
                rel="noreferrer"
                className="font-mono text-xs tracking-wide text-accent uppercase"
              >
                STL
              </a>
            </li>
          ))}
        </ul>
      </section>

      <section className="flex flex-col gap-2 text-sm leading-relaxed text-muted">
        <h2 className="font-mono text-xs tracking-widest uppercase">Also in the zip</h2>
        <p>OpenSCAD source, a Pro Micro MIDI sketch, and the print notes. Face-up, no supports, 0.2 mm layers.</p>
        <p className="flex gap-4">
          <a className="text-fg underline" href="/field-jog/field-jog.scad" target="_blank" rel="noreferrer">
            SCAD
          </a>
          <a className="text-fg underline" href="/field-jog/field_jog.ino" target="_blank" rel="noreferrer">
            Firmware
          </a>
          <a className="text-fg underline" href="/field-jog/README.md" target="_blank" rel="noreferrer">
            Notes
          </a>
        </p>
      </section>
    </main>
  );
}

function Spec({ k, v }: { k: string; v: string }) {
  return (
    <div className="flex flex-col gap-1 px-3 py-3">
      <dt className="font-mono text-xs tracking-widest text-muted uppercase">{k}</dt>
      <dd className="text-lg font-semibold">{v}</dd>
    </div>
  );
}
