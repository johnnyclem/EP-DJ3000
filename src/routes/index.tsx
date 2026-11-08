import { createFileRoute, Link } from "@tanstack/react-router";
import { ViewerPanel } from "@/components/viewer-panel";
import { RenderGallery, WIDE_RENDERS } from "@/components/render-gallery";

export const Route = createFileRoute("/")({ component: Home });

const PARTS = [
  ["top.stl", "Deck", "Grey"],
  ["bottom.stl", "Tray", "Grey"],
  ["platter.stl", "Platter", "Black"],
  ["fader.stl", "Fader cap", "Black"],
  ["fader_lock.stl", "Fader lock", "Black"],
  ["knob.stl", "Knob", "Orange"],
  ["button.stl", "Button ×2", "Black"],
  ["flange.stl", "Button clip ×2", "Black"],
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
          amp. Wide is the same deck at the K.O. II size, 176 × 240.5, with the
          controls that did not fit in 88 mm.
        </p>
      </header>

      <ViewerPanel />

      <RenderGallery />

      <dl className="grid grid-cols-3 divide-x divide-line border border-line">
        <Spec k="Height" v="240 mm" />
        <Spec k="Width" v="88 mm" />
        <Spec k="Thick" v="16 / 22" />
      </dl>

      <p className="text-sm leading-relaxed text-muted">
        The long edge matches the EP row, and the width matches a Sidekick. The 76 mm
        platter is centered, with 6 mm of deck on each side. The knob sits above it.
        It is 16 mm thick where it meets the row, the same as a K.O. II and a Sidekick.
        The top 52 mm rises to 22 mm so a 2×AA holder fits. That rise is part of
        the top shell. The two halves close on four pairs of magnets.
        The deck is not engraved. Type tore out of the first print, so the lettering
        is vinyl, cut 1:1 for a Cricut. The raised end carries the name, the port row
        and the tempo label. The buttons are labeled on the deck.
        The platter encoder is a MagnTek MT6701 on a 26 mm pad. Four M2 holes, 17.5 mm apart.
        Pin holes are 16, 25, 48, 216 and 224 mm from the fader end, 8 mm off the
        bottom, on both long sides.
      </p>

      <section className="flex flex-col gap-2 text-sm leading-relaxed">
        <h2 className="font-mono text-xs tracking-widest text-muted uppercase">Wide shells</h2>
        <p className="text-muted">
          176 × 240.5 × 16.3 mm. Twin is two 76 mm platters. Six is one 152 mm platter, set left so the pitch fader has a lane beside it. Print the top face up and the tray cavity up.
          IN, OUT and the other toggles are a 12×12×7.3 mm switch. Its own cap comes through the hole. CUE and PLAY stay printed plungers.
          Each platter sits over a MagnTek MT6701. The pad is 26 mm square, four M2 holes on a 17.5 mm square. Solder VDD, GND, SCL and SDA. Leave Analog/PWM open, and do not fit a pin header.
          The rear jacks sit between the magnet posts. On Twin the headphone jack is left of the crossfader. On Six it is centered. The microSD mouth is on the underside, 36.5 mm from the left and 27 mm in from the jack end.
        </p>
        <p className="flex flex-wrap gap-x-4 gap-y-2">
          <a className="text-fg underline" href="/field-jog/wide.scad" target="_blank" rel="noreferrer">SCAD</a>
          <a className="text-fg underline" href="/field-jog/stl/wide-twin-top.stl" target="_blank" rel="noreferrer">Twin top</a>
          <a className="text-fg underline" href="/field-jog/stl/wide-twin-bottom.stl" target="_blank" rel="noreferrer">Twin tray</a>
          <a className="text-fg underline" href="/field-jog/stl/wide-twin-platter.stl" target="_blank" rel="noreferrer">76 mm</a>
          <a className="text-fg underline" href="/field-jog/stl/wide-six-top.stl" target="_blank" rel="noreferrer">Six top</a>
          <a className="text-fg underline" href="/field-jog/stl/wide-six-bottom.stl" target="_blank" rel="noreferrer">Six tray</a>
          <a className="text-fg underline" href="/field-jog/stl/wide-six-platter.stl" target="_blank" rel="noreferrer">152 mm</a>
        </p>
      </section>

      <RenderGallery title="Wide" renders={WIDE_RENDERS} />

      <Link
        to="/four"
        className="flex flex-col gap-1 border border-line px-4 py-4"
      >
        <span className="font-mono text-xs tracking-widest text-accent uppercase">Next in the row</span>
        <span className="text-lg font-semibold">Four</span>
        <span className="text-sm text-muted">A four-track recorder on its own page. Same brick, five faders, a 3.2 inch screen.</span>
      </Link>

      <a
        href="/field-jog/field-jog.zip"
        target="_blank"
        rel="noreferrer"
        className="flex h-12 items-center justify-center bg-accent font-semibold text-accent-fg"
      >
        Download print files
      </a>

      <section className="flex flex-col gap-3">
        <h2 className="font-mono text-xs tracking-widest text-muted uppercase">Vinyl</h2>
        <img
          src="/field-jog/silk/preview.png"
          alt="Vinyl lettering laid out on the deck, jacks at the top"
          className="w-full border border-line"
        />
        <p className="text-sm leading-relaxed text-muted">
          Cut this in black vinyl. The screen, the knob and the platter stay clear.
          On the mat file the left piece is the deck and the right piece is the
          faceplate. A 10 mm bar sits in the gap. If the import comes in small,
          scale it until that bar is 10 mm.
        </p>
        <div className="flex flex-wrap gap-4 font-mono text-xs tracking-wide text-accent uppercase">
          <a href="/field-jog/silk/mat-cut.svg" target="_blank" rel="noreferrer">
            Mat
          </a>
          <a href="/field-jog/silk/deck-cut.svg" target="_blank" rel="noreferrer">
            Deck
          </a>
          <a href="/field-jog/silk/faceplate-cut.svg" target="_blank" rel="noreferrer">
            Faceplate
          </a>
          <a href="/field-jog/silk/deck-guide.svg" target="_blank" rel="noreferrer">
            Deck guide
          </a>
          <a href="/field-jog/silk/faceplate-guide.svg" target="_blank" rel="noreferrer">
            Plate guide
          </a>
        </div>
      </section>


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
      <section className="flex flex-col gap-2 text-sm leading-relaxed">
        <h2 className="font-mono text-xs tracking-widest text-muted uppercase">ECM sleeve</h2>
        <p className="text-muted">
          A 96 × 18.8 mm handheld shell for an ECM-77B head. The capsule slides in from the tip and the cable leaves the back. The orange button is a cap.
        </p>
        <p className="flex gap-4">
          <a className="text-fg underline" href="/ecm-handy/body.stl" target="_blank" rel="noreferrer">
            Body
          </a>
          <a className="text-fg underline" href="/ecm-handy/button.stl" target="_blank" rel="noreferrer">
            Button
          </a>
          <a className="text-fg underline" href="/ecm-handy/ecm-handy.scad" target="_blank" rel="noreferrer">
            SCAD
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
