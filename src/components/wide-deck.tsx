import { RoundedBox } from "@react-three/drei";

const W = 176;
const H = 240.5;
const T = 16.3;
const GREY = "#d4d4ce";
const BLACK = "#141416";
const WELL = "#2a2a28";
const ORANGE = "#f05a1a";
const SLOT = "#1a1a1c";

export type WideLayout = "twin" | "six";

function mat(color: string, roughness = 0.5) {
  return <meshStandardMaterial color={color} roughness={roughness} metalness={0.04} />;
}

function Platter({ x, y, d }: { x: number; y: number; d: number }) {
  const rings = d > 100 ? [34, 50, 64] : [16, 24, 31];
  const arm = d > 100 ? 22 : 12;
  return (
    <group position={[x, y, T]}>
      <mesh position={[0, 0, 0.2]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[d / 2 + 0.5, d / 2 + 0.5, 0.35, 72]} />
        {mat(WELL, 0.7)}
      </mesh>
      <mesh position={[0, 0, 2]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[d / 2 - 0.4, d / 2 - 0.4, 3.2, 72]} />
        {mat(BLACK, 0.42)}
      </mesh>
      {rings.map((r) => (
        <mesh key={r} position={[0, 0, 3.35]}>
          <torusGeometry args={[r, 0.32, 6, 48]} />
          {mat("#3a3a3c", 0.6)}
        </mesh>
      ))}
      <mesh position={[0, 0, 3.5]}>
        <boxGeometry args={[arm, 2.2, 0.5]} />
        {mat(ORANGE, 0.4)}
      </mesh>
      <mesh position={[0, 0, 3.5]}>
        <boxGeometry args={[2.2, arm, 0.5]} />
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
      <RoundedBox args={[88, 3.4, 1.1]} radius={0.5} smoothness={2} position={[88, 16, T + 0.55]}>
        {mat(SLOT, 0.6)}
      </RoundedBox>
      <RoundedBox args={[18, 12, 6]} radius={1} smoothness={2} position={[88, 16, T + 3.6]}>
        {mat(BLACK, 0.4)}
      </RoundedBox>
    </group>
  );
}

function Screen({ x, y, w }: { x: number; y: number; w: number }) {
  return (
    <group position={[x, y, T + 0.7]}>
      <RoundedBox args={[w, 14, 1.1]} radius={0.6} smoothness={2}>
        {mat("#0c0c0e", 0.35)}
      </RoundedBox>
      <mesh position={[-w * 0.18, 1.6, 0.6]}>
        <boxGeometry args={[w * 0.5, 3.2, 0.3]} />
        {mat("#e6c15a", 0.4)}
      </mesh>
      <mesh position={[-w * 0.18, -2.4, 0.6]}>
        <boxGeometry args={[w * 0.5, 3.2, 0.3]} />
        {mat("#3d6fd4", 0.4)}
      </mesh>
    </group>
  );
}

function Jacks() {
  const xs = [24, 40, 128, 144, 164];
  return (
    <group>
      {xs.map((x) => (
        <mesh key={x} position={[x, H - 2.2, 8]}>
          <cylinderGeometry args={[2.3, 2.3, 4.4, 16]} />
          {mat(x === 164 ? "#3a3a3c" : SLOT, 0.5)}
        </mesh>
      ))}
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

function Twin() {
  const decks = [
    { x: 46, cue: 40, play: 66, inn: 40, out: 66, pads: [36, 50, 64, 78], pitch: 18 },
    { x: 130, cue: 136, play: 110, inn: 136, out: 110, pads: [98, 112, 126, 140], pitch: 158 },
  ];
  return (
    <group>
      {decks.map((d) => (
        <group key={d.x}>
          <Platter x={d.x} y={158} d={76} />
          <Cap x={d.cue} y={40} d={18} h={5} />
          <Cap x={d.play} y={40} d={18} h={5} color="#1c1c1e" />
          <Cap x={d.inn} y={76} d={12} h={3.2} />
          <Cap x={d.out} y={76} d={12} h={3.2} />
          <Led x={d.inn} y={88} />
          <Led x={d.out} y={88} />
          <Pitch x={d.pitch} y0={24} y1={108} />
          {d.pads.map((x) => (
            <RoundedBox key={x} args={[11, 11, 2.2]} radius={0.8} smoothness={2} position={[x, 100, T + 1.1]}>
              {mat("#2c2c2e", 0.5)}
            </RoundedBox>
          ))}
        </group>
      ))}
      <Crossfader />
      <Screen x={88} y={220} w={78} />
      <Cap x={158} y={218} d={16} h={8} color={ORANGE} />
    </group>
  );
}

function Six() {
  return (
    <group>
      <Platter x={88} y={142} d={152.4} />
      <Cap x={28} y={30} d={20} h={5.5} />
      <Cap x={54} y={30} d={20} h={5.5} />
      <Cap x={28} y={52} d={12} h={3.2} color="#2c2c2e" />
      {[100, 116, 132, 148].map((x) => (
        <RoundedBox key={x} args={[12, 12, 2.2]} radius={0.8} smoothness={2} position={[x, 36, T + 1.1]}>
          {mat("#2c2c2e", 0.5)}
        </RoundedBox>
      ))}
      <Pitch x={166} y0={12} y1={60} />
      <Cap x={22} y={214} d={12} h={3.2} />
      <Cap x={22} y={196} d={12} h={3.2} />
      <Led x={34} y={214} />
      <Led x={34} y={196} />
      <Screen x={78} y={228} w={64} />
      <Cap x={150} y={226} d={14} h={7} color={ORANGE} />
    </group>
  );
}

export function WideDeck({ layout }: { layout: WideLayout }) {
  return (
    <group>
      <Shell />
      <Jacks />
      {layout === "twin" ? <Twin /> : <Six />}
    </group>
  );
}
