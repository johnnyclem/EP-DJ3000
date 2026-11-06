import { useState } from "react";

const RENDERS = [
  { src: "/field-jog/renders/dark.jpg", alt: "Scratch deck on a dark field, platter and orange knob" },
  { src: "/field-jog/renders/top.jpg", alt: "Top view of the scratch deck with cue, fader, and rev" },
  { src: "/field-jog/renders/angle.jpg", alt: "Three-quarter view of the grey scratch deck" },
  { src: "/field-jog/renders/three.jpg", alt: "Angled view showing the side pin holes and MIDI label" },
  { src: "/field-jog/renders/detail.jpg", alt: "Close view of the platter, orange plus, and knurled knob" },
  { src: "/field-jog/renders/side.jpg", alt: "Side profile of the deck, platter, and fader cap" },
  { src: "/field-jog/renders/pair.jpg", alt: "Scratch deck beside a K.O. II Sidekick mixer" },
] as const;

export const WIDE_RENDERS = [
  { src: "/field-jog/renders/wide/twin.jpg", alt: "Twin deck, two platters, pads, and three faders" },
  { src: "/field-jog/renders/wide/six.jpg", alt: "Single platter deck, pads along the top, orange knob" },
  { src: "/field-jog/renders/wide/six-front.jpg", alt: "Single platter deck, cue pads and pitch fader along the front" },
  { src: "/field-jog/renders/wide/underside.jpg", alt: "Underside with the speaker grille, USB-C, and card slot" },
] as const;

export function RenderGallery({
  title = "Renders",
  renders = RENDERS,
}: {
  title?: string;
  renders?: readonly { src: string; alt: string }[];
}) {
  const [index, setIndex] = useState(0);
  const shot = renders[index] ?? renders[0];

  return (
    <section className="flex flex-col gap-3">
      <h2 className="font-mono text-xs tracking-widest text-muted uppercase">{title}</h2>
      <img src={shot.src} alt={shot.alt} className="w-full border border-line bg-fg" />
      <div className="flex gap-2 overflow-x-auto pb-1">
        {renders.map((item, i) => (
          <button
            key={item.src}
            type="button"
            aria-pressed={i === index}
            aria-label={item.alt}
            onClick={() => setIndex(i)}
            className={`h-16 w-16 shrink-0 border ${i === index ? "border-accent" : "border-line"}`}
          >
            <img src={item.src} alt="" className="h-full w-full object-cover" />
          </button>
        ))}
      </div>
    </section>
  );
}