import { RoundedBox } from "@react-three/drei";

// Field Jog Go. 150 x 74 x 16. Prints flat on a Mars 5 Ultra.
const W = 150;
const H = 74;
const T = 16;
const GREY = "#c5c3bc";
const CREAM = "#f3efe4";
const BLACK = "#161618";
const ORANGE = "#ff4d1c";
const INK = "#8a8a84";

const CX = 78;
const CY = 48;

export type GoPart = "unit" | "case" | "screen" | "fader" | "knob" | "buttons";

export const GO_PARTS = [
  { id: "unit", label: "Unit" },
  { id: "case", label: "Case" },
  { id: "screen", label: "Screen" },
  { id: "fader", label: "Fader" },
  { id: "knob", label: "Knob" },
  { id: "buttons", label: "Buttons" },
] as const;

function show(focus: GoPart, part: GoPart) {
  return focus === "unit" || focus === part;
}

function mat(color: string, roughness = 0.55) {
  return <meshStandardMaterial color={color} roughness={roughness} metalness={0.04} />;
}

export function GoDeck({ focus = "unit" }: { focus?: GoPart }) {
  return (
    <group>
      {show(focus, "case") ? <Shell /> : null}
      {show(focus, "screen") ? <Screen /> : null}
      {show(focus, "knob") ? <Knob /> : null}
      {show(focus, "fader") ? <Fader /> : null}
      {show(focus, "buttons") ? <Buttons /> : null}
    </group>
  );
}

function Shell() {
  return (
    <group>
      <RoundedBox args={[W, H, T]} radius={3.2} smoothness={4} position={[W / 2, H / 2, T / 2]}>
        {mat(GREY, 0.42)}
      </RoundedBox>
      <mesh position={[8, 67.2, T + 0.05]} rotation={[-Math.PI / 2, 0, 0]}>
        {/* wordmark is a thin dark bar; the scad has the real letters */}
      </mesh>
      <mesh position={[21, 67.4, T + 0.08]}>
        <boxGeometry args={[26, 1.3, 0.16]} />
        {mat(INK, 0.7)}
      </mesh>
      <mesh position={[21, 63.6, T + 0.06]}>
        <boxGeometry args={[26, 0.7, 0.12]} />
        {mat(ORANGE, 0.4)}
      </mesh>
      <mesh position={[32, 24, T + 0.05]}>
        <boxGeometry args={[8, 1.1, 0.1]} />
        {mat(INK, 0.75)}
      </mesh>
      <mesh position={[33, 9, T + 0.05]}>
        <boxGeometry args={[10, 1.1, 0.1]} />
        {mat(INK, 0.75)}
      </mesh>
      {/* USB-C, left end */}
      <mesh position={[-0.2, 55.5, 8.9]}>
        <boxGeometry args={[1.2, 11, 4.6]} />
        {mat("#2a2a28", 0.4)}
      </mesh>
      {/* headphone, right end */}
      <mesh position={[W + 0.2, 40, 8]} rotation={[0, 0, Math.PI / 2]}>
        <cylinderGeometry args={[3.15, 3.15, 1.4, 24]} />
        {mat("#2a2a28", 0.4)}
      </mesh>
    </group>
  );
}

function Screen() {
  return (
    <group position={[CX, CY, T]}>
      <mesh>
        <cylinderGeometry args={[20.1, 20.1, 0.3, 64]} />
        {mat(CREAM, 0.35)}
      </mesh>
      <mesh position={[0, 0, 0.2]}>
        <cylinderGeometry args={[16.2, 16.2, 0.25, 64]} />
        {mat("#0e1c22", 0.25)}
      </mesh>
      <mesh position={[0, 0, 0.36]} rotation={[Math.PI / 2, 0, 0]}>
        <torusGeometry args={[10.5, 0.7, 8, 40, Math.PI * 1.35]} />
        {mat("#3ee0ff", 0.3)}
      </mesh>
    </group>
  );
}

function Knob() {
  return (
    <group position={[132, 58, T]}>
      <mesh>
        <cylinderGeometry args={[8, 8, 8.5, 32]} />
        {mat(ORANGE, 0.38)}
      </mesh>
      <mesh position={[0, 6.2, 3]} rotation={[Math.PI / 2, 0, 0]}>
        <boxGeometry args={[0.7, 2, 1.2]} />
        {mat(CREAM, 0.4)}
      </mesh>
    </group>
  );
}

function Fader() {
  return (
    <group position={[78, 14, T]}>
      <mesh>
        <boxGeometry args={[62, 3.2, 0.4]} />
        {mat("#1a1a1c", 0.5)}
      </mesh>
      <mesh position={[6, 0, 2.6]}>
        <boxGeometry args={[16, 8, 5]} />
        {mat(ORANGE, 0.36)}
      </mesh>
    </group>
  );
}

function Buttons() {
  return (
    <group>
      {[
        [18, 24],
        [18, 9],
      ].map(([x, y]) => (
        <mesh key={`${x}-${y}`} position={[x, y, T - 0.2]}>
          <cylinderGeometry args={[6.8, 6.8, 2.4, 28]} />
          {mat(BLACK, 0.4)}
        </mesh>
      ))}
      {[127, 141].map((x) => (
        <group key={x}>
          <mesh position={[x, 11, T - 0.3]}>
            <cylinderGeometry args={[4.1, 4.1, 2, 22]} />
            {mat(BLACK, 0.4)}
          </mesh>
          <mesh position={[x, 20.2, T + 0.15]}>
            <cylinderGeometry args={[1.55, 1.55, 0.6, 16]} />
            {mat("#ffb020", 0.3)}
          </mesh>
        </group>
      ))}
    </group>
  );
}
