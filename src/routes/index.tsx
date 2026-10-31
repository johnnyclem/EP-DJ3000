import { createFileRoute } from "@tanstack/react-router";
import { ViewerPanel } from "@/components/viewer-panel";

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
          A scratch-deck shell that lines up with the EP-133 and the Sidekick. Grey deck, orange
          marks, black jog and fader. Four Technic pin holes on each long side, on the same 8 mm
          grid as the EP row.
        </p>
      </header>

      <ViewerPanel />

      <dl className="grid grid-cols-3 divide-x divide-line border border-line">
        <Spec k="Height" v="240 mm" />
        <Spec k="Width" v="120 mm" />
        <Spec k="Thick" v="26 mm" />
      </dl>

      <p className="text-sm leading-relaxed text-muted">
        Height matches the EP row. It is wider than a Sidekick because a 76 mm platter does not fit
        in 88 mm, and thicker than 16 mm because the bearing, magnet, and fader lock need a cavity.
        The pin holes sit 8 mm off the bottom, so a Technic pin meets a K.O. II on the same desk.
        Centers are 108, 116, 124 and 132 mm up each long side.
      </p>

      <a
        href="/field-jog/field-jog.zip"
        download
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
                download
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
          <a className="text-fg underline" href="/field-jog/field-jog.scad" download>
            SCAD
          </a>
          <a className="text-fg underline" href="/field-jog/field_jog.ino" download>
            Firmware
          </a>
          <a className="text-fg underline" href="/field-jog/README.md" download>
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
