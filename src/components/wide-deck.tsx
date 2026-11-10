import { RoundedBox } from "@react-three/drei";

const W = 176;
const H = 240.5;
const T = 16.3;
const GREY = "#ebe9e3";
const BLACK = "#141416";
const ORANGE = "#ff4d1c";
const SLOT = "#1a1a1c";

export type WideLayout = "twin" | "six";
export type WidePart = "unit" | "case" | "platter" | "fader" | "pads" | "knob";

export const WIDE_PARTS = [
  { id: "unit", label: "Unit" },
  { id: "case", label: "Case" },
  { id: "platter", label: "Platters" },
  { id: "fader", label: "Faders" },
  { id: "pads", label: "Buttons" },
  { id: "knob", label: "Knob" },
] as const;

function show(focus: WidePart, part: WidePart) {
  return focus === "unit" || focus === part;
}

function mat(color: string, roughness = 0.5) {
  return <meshStandardMaterial color={color} roughness={roughness} metalness={0.04} />;
}

function Platter({ x, y, d }: { x: number; y: number; d: number }) {
  const rings = d > 100 ? [34, 50, 64] : [16, 24, 31];
  const arm = d > 100 ? 22 : 12;
  // Same snap stem as the half-size platter. Local z is the top of the case.
  // Disc underside at -0.7, magnet mouth at -10.55, barb at -7.
  return (
    <group position={[x, y, T]}>
      <mesh position={[0, 0, 1.2]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[d / 2 - 0.3, d / 2 - 0.3, 3.8, 72]} />
        {mat(BLACK, 0.42)}
      </mesh>
      <mesh position={[0, 0, -5.62]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[3.85, 3.85, 9.85, 40]} />
        {mat(BLACK, 0.5)}
      </mesh>
      <mesh position={[0, 0, -6.55]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[4.45, 4.45, 0.9, 32]} />
        {mat(BLACK, 0.55)}
      </mesh>
      <mesh position={[0, 0, -6.0]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[3.85, 4.45, 0.6, 32]} />
        {mat(BLACK, 0.55)}
      </mesh>
      <mesh position={[0, 0, -10.7]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[2.7, 3.2, 0.55, 28]} />
        {mat(SLOT, 0.7)}
      </mesh>
      {[0, 120, 240].map((deg) => {
        const a = (deg * Math.PI) / 180;
        return (
          <mesh key={deg} position={[Math.sin(a) * 3.2, Math.cos(a) * 3.2, -4.2]} rotation={[0, 0, -a]}>
            <boxGeometry args={[0.7, 1.4, 5.2]} />
            {mat("#2a2a2c", 0.8)}
          </mesh>
        );
      })}
      {rings.map((r) => (
        <mesh key={r} position={[0, 0, 3.2]}>
          <torusGeometry args={[r, 0.32, 6, 48]} />
          {mat("#3a3a3c", 0.6)}
        </mesh>
      ))}
      <mesh position={[0, 0, 3.25]}>
        <boxGeometry args={[arm, 2.2, 0.45]} />
        {mat(ORANGE, 0.4)}
      </mesh>
      <mesh position={[0, 0, 3.25]}>
        <boxGeometry args={[2.2, arm, 0.45]} />
        {mat(ORANGE, 0.4)}
      </mesh>
    </group>
  );
}

function Cap({ x, y, d, h = 4, color = BLACK }: { x: number; y: number; d: number; h?: number; color?: string }) {
  return (
    <mesh position={[x, y, T + h / 2]} rotation={[Math.PI / 2, 0, 0]}>
      <cylinderGeometry args={[d / 2, d / 2, h, 28]} />
      {mat(color, 0.45)}
    </mesh>
  );
}

function Tact({ x, y }: { x: number; y: number }) {
  return (
    <mesh position={[x, y, T + 0.8]} rotation={[Math.PI / 2, 0, 0]}>
      <cylinderGeometry args={[3.45, 3.45, 1.6, 28]} />
      {mat(BLACK, 0.45)}
    </mesh>
  );
}

function Led({ x, y }: { x: number; y: number }) {
  return (
    <mesh position={[x, y, T + 0.5]} rotation={[Math.PI / 2, 0, 0]}>
      <cylinderGeometry args={[1.3, 1.3, 0.8, 16]} />
      {mat(ORANGE, 0.3)}
    </mesh>
  );
}

function Pitch({ x, y0, y1 }: { x: number; y0: number; y1: number }) {
  const mid = (y0 + y1) / 2;
  const len = y1 - y0;
  return (
    <group>
      <RoundedBox args={[3.4, len, 1.1]} radius={0.5} smoothness={2} position={[x, mid, T + 0.55]}>
        {mat(SLOT, 0.6)}
      </RoundedBox>
      <RoundedBox args={[12, 16, 5]} radius={1} smoothness={2} position={[x, y0 + len * 0.62, T + 3.2]}>
        {mat("#c4c4be", 0.4)}
      </RoundedBox>
    </group>
  );
}

function Crossfader() {
  return (
    <group>
      <RoundedBox args={[62, 3.4, 1.1]} radius={0.5} smoothness={2} position={[88, 16, T + 0.55]}>
        {mat(SLOT, 0.6)}
      </RoundedBox>
      <RoundedBox args={[18, 12, 6]} radius={1} smoothness={2} position={[88, 16, T + 3.6]}>
        {mat(BLACK, 0.4)}
      </RoundedBox>
    </group>
  );
}

function Screen({ x, y }: { x: number; y: number }) {
  return (
    <group position={[x, y, T + 0.35]}>
      <RoundedBox args={[30.5, 11.85, 0.7]} radius={0.4} smoothness={2}>
        {mat("#0c0c0e", 0.35)}
      </RoundedBox>
      <mesh position={[-4, 1.2, 0.4]}>
        <boxGeometry args={[14, 2.4, 0.2]} />
        {mat("#e6c15a", 0.4)}
      </mesh>
      <mesh position={[-4, -1.6, 0.4]}>
        <boxGeometry args={[14, 2.4, 0.2]} />
        {mat("#3d6fd4", 0.4)}
      </mesh>
    </group>
  );
}

function Jacks({ layout }: { layout: WideLayout }) {
  const xs = [50, 66, 110, 126];
  const hp = layout === "twin" ? 41 : 88;
  return (
    <group>
      <mesh position={[W / 2, H - 2.2, 8]}>
        <boxGeometry args={[12.5, 4.4, 3.4]} />
        {mat(SLOT, 0.5)}
      </mesh>
      {xs.map((x) => (
        <mesh key={x} position={[x, H - 2.2, 8]}>
          <cylinderGeometry args={[2.3, 2.3, 4.4, 16]} />
          {mat(SLOT, 0.5)}
        </mesh>
      ))}
      <mesh position={[hp, 2.2, 8]}>
        <cylinderGeometry args={[2.3, 2.3, 4.4, 16]} />
        {mat(SLOT, 0.5)}
      </mesh>
      {/* microSD socket keep-out, underside. Mouth left edge 36.5, y 212. */}
      <mesh position={[42, 213, 0.25]}>
        <boxGeometry args={[11, 14, 0.4]} />
        {mat(SLOT, 0.45)}
      </mesh>
    </group>
  );
}

function Shell() {
  return (
    <group>
      <RoundedBox args={[W, H, T]} radius={3.2} smoothness={3} position={[W / 2, H / 2, T / 2]}>
        {mat(GREY, 0.58)}
      </RoundedBox>
      {[16, 25, 48, 216, 224].map((y) => (
        <group key={y}>
          <mesh position={[-0.4, y, 8]} rotation={[0, 0, Math.PI / 2]}>
            <cylinderGeometry args={[2.2, 2.2, 2.4, 16]} />
            {mat(SLOT, 0.6)}
          </mesh>
          <mesh position={[W + 0.4, y, 8]} rotation={[0, 0, Math.PI / 2]}>
            <cylinderGeometry args={[2.2, 2.2, 2.4, 16]} />
            {mat(SLOT, 0.6)}
          </mesh>
        </group>
      ))}
    </group>
  );
}

function Twin({ focus }: { focus: WidePart }) {
  const decks = [
    { x: 46, cue: 40, play: 66, inn: 40, out: 66, pads: [36, 50, 64, 78], pitch: 18 },
    { x: 130, cue: 136, play: 110, inn: 136, out: 110, pads: [98, 112, 126, 140], pitch: 158 },
  ];
  return (
    <group>
      {decks.map((d) => (
        <group key={d.x}>
          {show(focus, "platter") ? <Platter x={d.x} y={158} d={76} /> : null}
          {show(focus, "pads") ? (
            <>
              <Cap x={d.cue} y={40} d={18} h={5} />
              <Cap x={d.play} y={40} d={18} h={5} color="#1c1c1e" />
              <Tact x={d.inn} y={76} />
              <Tact x={d.out} y={76} />
              <Led x={d.inn} y={88} />
              <Led x={d.out} y={88} />
              {d.pads.map((x) => (
                <RoundedBox key={x} args={[11, 11, 2.2]} radius={0.8} smoothness={2} position={[x, 100, T + 1.1]}>
                  {mat("#2c2c2e", 0.5)}
                </RoundedBox>
              ))}
            </>
          ) : null}
          {show(focus, "fader") ? <Pitch x={d.pitch} y0={35} y1={97} /> : null}
        </group>
      ))}
      {show(focus, "fader") ? <Crossfader /> : null}
      {show(focus, "case") ? <Screen x={88} y={208} /> : null}
      {show(focus, "knob") ? <Cap x={158} y={210} d={14} h={8} color={ORANGE} /> : null}
    </group>
  );
}

function Six({ focus }: { focus: WidePart }) {
  return (
    <group>
      {show(focus, "platter") ? <Platter x={82} y={124} d={152.4} /> : null}
      {show(focus, "pads") ? (
        <>
          <Cap x={26} y={24} d={20} h={5.5} />
          <Cap x={52} y={24} d={20} h={5.5} />
          <Tact x={74} y={30} />
          {[104, 120, 136, 152].map((x) => (
            <RoundedBox key={x} args={[12, 12, 2.2]} radius={0.8} smoothness={2} position={[x, 32, T + 1.1]}>
              {mat("#2c2c2e", 0.5)}
            </RoundedBox>
          ))}
          <Tact x={22} y={214} />
          <Tact x={42} y={214} />
          <Led x={58} y={210} />
          <Led x={68} y={210} />
        </>
      ) : null}
      {show(focus, "fader") ? <Pitch x={167} y0={64} y1={110} /> : null}
      {show(focus, "case") ? <Screen x={96} y={210} /> : null}
      {show(focus, "knob") ? <Cap x={148} y={212} d={12} h={7} color={ORANGE} /> : null}
    </group>
  );
}

export function WideDeck({ layout, focus = "unit" }: { layout: WideLayout; focus?: WidePart }) {
  return (
    <group>
      {show(focus, "case") ? (
        <>
          <Shell />
          <Jacks layout={layout} />
        </>
      ) : null}
      {layout === "twin" ? <Twin focus={focus} /> : <Six focus={focus} />}
    </group>
  );
}
