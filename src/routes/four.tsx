import { useState } from "react";
import { createFileRoute, Link } from "@tanstack/react-router";
import { FourScene } from "@/components/four-scene";
import { TILES } from "@/data/four-kit";

export const Route = createFileRoute("/four")({
  head: () => ({
    meta: [{ title: "Four — Field Jog" }],
  }),
  component: FourPage,
});

const SMALL = [
  ["caps-plate.stl", "Caps and keys, one plate"],
  ["cap.stl", "Fader cap ×4"],
  ["cap-master.stl", "Master cap, orange"],
  ["knob.stl", "FX knob, orange"],
  ["key.stl", "Transport key ×6"],
  ["rec.stl", "Record button ×4"],
] as const;

function FourPage() {
  const [view, setView] = useState<"unit" | "pieces">("pieces");
  return (
    <main className="mx-auto flex min-h-screen max-w-lg flex-col gap-8 px-5 py-8">
      <header className="flex flex-col gap-3">
        <Link to="/" className="font-mono text-xs tracking-widest text-accent uppercase">
          Field Jog
        </Link>
        <h1 className="text-4xl leading-none font-semibold tracking-tight">Four</h1>
        <p className="text-base leading-relaxed text-muted">
          A four-track recorder in the same brick as the K.O. II. The face follows a
          Portastudio: four channel faders, a master, a transport row, and a 3.2 inch
          screen where the cassette window was.
        </p>
      </header>

      <div className="h-viewer w-full touch-none overflow-hidden border border-line bg-surface">
        <FourScene view={view} />
      </div>
      <div className="flex gap-2">
        {(
          [
            ["unit", "Unit"],
            ["pieces", "Pieces"],
          ] as const
        ).map(([id, label]) => (
          <button
            key={id}
            type="button"
            aria-pressed={view === id}
            onClick={() => setView(id)}
            className={
              view === id
                ? "h-11 flex-1 bg-fg font-mono text-xs tracking-wide text-bg uppercase"
                : "h-11 flex-1 border border-line font-mono text-xs tracking-wide text-fg uppercase"
            }
          >
            {label}
          </button>
        ))}
      </div>
      <p className="text-sm text-muted">
        {view === "pieces"
          ? "The tray is one FDM print. The face tiles lift off. Each of them fits the 153.36 × 77.76 mm plate."
          : "Drag to turn. Pinch to zoom."}
      </p>

      <dl className="grid grid-cols-3 divide-x divide-line border border-line">
        <Spec k="Height" v="240 mm" />
        <Spec k="Width" v="176 mm" />
        <Spec k="Thick" v="22 mm" />
      </dl>

      <section className="flex flex-col gap-3 text-sm leading-relaxed">
        <h2 className="font-mono text-xs tracking-widest text-muted uppercase">Resin plate</h2>
        <p className="text-muted">
          The plate is 153.36 × 77.76 × 165 mm. Every resin part is kept inside
          150.9 × 75.3, so there is 1.2 mm of clearance on each side. Height is
          not the limit. Lay the long side of a tile along the 153 mm axis. The
          wide fader tile is 70 × 143 mm. The screen tile, with the left ear
          attached, is 131 × 72 mm.
        </p>
        <p className="text-muted">
          The tiles drop into a 2.2 mm rabbet in the tray. A drop of CA at the
          corners holds them. The seams are 0.5 mm, and a rib in the tray sits under
          each one. Tracks 1–2 share a tile, and so do tracks 3–4. Caps, keys, and
          the knob share one more plate.
        </p>
        <ul className="divide-y divide-line border border-line">
          <Row file="tray.stl" name="Tray" note="FDM · 176 × 240" />
          {TILES.map((tile) => (
            <Row
              key={tile.id}
              file={`${tile.id}.stl`}
              name={tile.label}
              note={`Resin · ${Math.round(tile.x1 - tile.x0)} × ${Math.round(tile.y1 - tile.y0)}`}
            />
          ))}
          {SMALL.map(([file, name]) => (
            <Row key={file} file={file} name={name} note="Resin" />
          ))}
        </ul>
        <a className="text-fg underline" href="/field-jog/four/four.scad" target="_blank" rel="noreferrer">
          SCAD
        </a>
      </section>

      <p className="text-sm leading-relaxed text-muted">
        Pin holes match the rest of the row: 16, 25, 48, 216 and 224 mm from the
        fader end, 8 mm off the table, on both long sides.
      </p>

      <section className="flex flex-col gap-2 text-sm leading-relaxed">
        <h2 className="font-mono text-xs tracking-widest text-muted uppercase">Jacks</h2>
        <p className="text-muted">All 3.5 mm, cut in the FDM tray.</p>
        <ul className="list-disc pl-5 text-muted">
          <li>Front: headphones, instrument, mic.</li>
          <li>Back: MIDI in, MIDI out, stereo line in, stereo line out, tracks 1 to 4.</li>
        </ul>
      </section>

      <section className="flex flex-col gap-2 text-sm leading-relaxed">
        <h2 className="font-mono text-xs tracking-widest text-muted uppercase">Brain</h2>
        <p className="text-muted">
          A Daisy Seed. Its onboard codec is stereo, and this box needs four inputs
          and six outputs, so a PCM3168A sits beside it. Teensy 4.1 is the alternate
          if USB-host MIDI matters more than the audio library. A Bela Mini stays
          stereo, so it does not fit.
        </p>
        <ul className="list-disc pl-5 text-muted">
          <li>MIDI clock is TRS, plus USB MIDI from the Seed.</li>
          <li>The screen is a 3.2 inch SPI panel, 320×240, with touch.</li>
          <li>FX is a send inside the mix. The orange knob above the master is that send.</li>
        </ul>
      </section>
    </main>
  );
}

function Row({ file, name, note }: { file: string; name: string; note: string }) {
  return (
    <li className="flex items-center justify-between gap-3 px-3 py-3">
      <span className="flex flex-col">
        <span className="text-sm capitalize">{name}</span>
        <span className="font-mono text-xs text-muted">{note}</span>
      </span>
      <a
        href={`/field-jog/four/${file}`}
        target="_blank"
        rel="noreferrer"
        className="font-mono text-xs tracking-wide text-accent uppercase"
      >
        STL
      </a>
    </li>
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
