import { RoundedBox } from "@react-three/drei";

const W = 176;
const H = 240.5;
const T = 24;
const GREY = "#d4d4ce";
const BLACK = "#141416";
const SLOT = "#1a1a1c";
const ORANGE = "#f05a1a";
const CAP = "#c8c4bc";
const GLASS = "#101418";

function mat(color: string, roughness = 0.5) {
  return <meshStandardMaterial color={color} roughness={roughness} metalness={0.04} />;
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

function Jack({ x, y }: { x: number; y: number }) {
  return (
    <mesh position={[x, y, 11]}>
      <cylinderGeometry args={[2.4, 2.4, 5, 16]} />
      {mat(SLOT, 0.5)}
    </mesh>
  );
}

function Fader({ x, master = false }: { x: number; master?: boolean }) {
  return (
    <group>
      <RoundedBox args={[3.6, 78, 1.2]} radius={0.5} smoothness={2} position={[x, 76, T + 0.6]}>
        {mat(SLOT, 0.6)}
      </RoundedBox>
      <RoundedBox
        args={[14, 11, 8]}
        radius={1.1}
        smoothness={2}
        position={[x, master ? 96 : 70, T + 4.4]}
      >
        {mat(master ? ORANGE : CAP, 0.4)}
      </RoundedBox>
    </group>
  );
}

function Key({ x, y, rec = false }: { x: number; y: number; rec?: boolean }) {
  return (
    <group>
      <RoundedBox args={[16, 12, 3.4]} radius={1} smoothness={2} position={[x, y, T + 1.8]}>
        {mat(rec ? "#2a2a2c" : "#b7b7b0", 0.45)}
      </RoundedBox>
      {rec ? (
        <mesh position={[x, y + 7.5, T + 0.5]} rotation={[Math.PI / 2, 0, 0]}>
          <cylinderGeometry args={[1.3, 1.3, 0.8, 14]} />
          {mat(ORANGE, 0.3)}
        </mesh>
      ) : null}
    </group>
  );
}

function Screen() {
  const lanes = [0, 1, 2, 3];
  return (
    <group position={[88, 196, T + 0.4]}>
      <RoundedBox args={[78, 58, 1.4]} radius={1.2} smoothness={2}>
        {mat("#0c0c0e", 0.4)}
      </RoundedBox>
      <mesh position={[0, 0, 0.8]}>
        <boxGeometry args={[66, 49, 0.4]} />
        {mat(GLASS, 0.25)}
      </mesh>
      {lanes.map((i) => (
        <mesh key={i} position={[-8 + (i % 2) * 18, 14 - i * 10, 1.1]}>
          <boxGeometry args={[28 - i * 3, 3.2, 0.3]} />
          {mat(i === 0 ? "#e6c15a" : i === 3 ? ORANGE : "#8a8a84", 0.4)}
        </mesh>
      ))}
      <mesh position={[0, -20, 1.1]}>
        <boxGeometry args={[50, 1.2, 0.3]} />
        {mat("#3d6fd4", 0.35)}
      </mesh>
    </group>
  );
}

export function PortaDeck() {
  const tracks = [28, 58, 88, 118];
  const transport = [28, 50, 72, 94, 116, 138];
  const rear = [18, 38, 58, 78, 98, 118, 138, 158];
  return (
    <group>
      <Shell />
      <Screen />
      {transport.map((x, i) => (
        <Key key={x} x={x} y={156} rec={i === 0} />
      ))}
      {tracks.map((x) => (
        <group key={x}>
          <Fader x={x} />
          <Key x={x} y={128} rec />
        </group>
      ))}
      <Fader x={156} master />
      <mesh position={[156, 128, T + 4]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[6, 7, 8, 24]} />
        {mat(ORANGE, 0.4)}
      </mesh>
      {rear.map((x) => (
        <Jack key={x} x={x} y={H - 1} />
      ))}
      <Jack x={36} y={2} />
      <Jack x={88} y={2} />
      <Jack x={140} y={2} />
    </group>
  );
}
