import { useMemo } from "react";
import { RoundedBox } from "@react-three/drei";
import * as THREE from "three";

// The original face, grown so a 6 inch ring still has a field of grey
// around it. 214 x 300 x 22. The wordmark lives above the disc.
const W = 214;
const H = 300;
const T = 22;
const GREY = "#ebe9e3";
const BLACK = "#161618";
const ORANGE = "#ff4d1c";
const INK = "#8e8e88";

const CX = 96;
const CY = 143;
const POD = 152.4;

export type CarryPart = "unit" | "case" | "platter" | "fader" | "knob" | "buttons";

export const CARRY_PARTS = [
  { id: "unit", label: "Unit" },
  { id: "case", label: "Case" },
  { id: "platter", label: "Platter" },
  { id: "fader", label: "Fader" },
  { id: "knob", label: "Knob" },
  { id: "buttons", label: "Buttons" },
] as const;

function show(focus: CarryPart, part: CarryPart) {
  return focus === "unit" || focus === part;
}

export function CarryDeck({ focus = "unit" }: { focus?: CarryPart }) {
  return (
    <group>
      {show(focus, "case") ? <Shell /> : null}
      {show(focus, "case") ? <Face /> : null}
      {show(focus, "platter") ? <Ring /> : null}
      {show(focus, "platter") ? <Screen /> : null}
      {show(focus, "knob") ? <Knob /> : null}
      {show(focus, "fader") ? <Fader /> : null}
      {show(focus, "buttons") ? <Keys /> : null}
    </group>
  );
}

function mat(color: string, roughness = 0.72) {
  return <meshStandardMaterial color={color} roughness={roughness} metalness={0.02} />;
}

function Shell() {
  const holes = [
    [14, 14],
    [W - 14, 14],
    [14, H - 14],
    [W - 14, H - 14],
  ];
  return (
    <group>
      <RoundedBox args={[W, H, T]} radius={5} smoothness={4} position={[W / 2, H / 2, T / 2]}>
        {mat(GREY, 0.78)}
      </RoundedBox>
      <mesh position={[CX, CY, T - 0.3]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[POD / 2 + 0.5, POD / 2 + 0.5, 0.7, 80]} />
        {mat("#c8c8c2", 0.8)}
      </mesh>
      {holes.map(([x, y]) => (
        <mesh key={`${x}-${y}`} position={[x, y, T + 0.2]} rotation={[Math.PI / 2, 0, 0]}>
          <cylinderGeometry args={[2.1, 2.1, 1.2, 20]} />
          {mat("#2a2a28", 0.45)}
        </mesh>
      ))}
      {[
        [16, 16],
        [W - 16, 16],
        [16, H - 16],
        [W - 16, H - 16],
      ].map(([x, y]) => (
        <mesh key={`f${x}-${y}`} position={[x, y, -0.5]} rotation={[Math.PI / 2, 0, 0]}>
          <cylinderGeometry args={[3.6, 3.6, 1, 16]} />
          {mat("#2c2c28", 0.85)}
        </mesh>
      ))}
    </group>
  );
}

function Face() {
  const map = useMemo(() => {
    const s = 10;
    const c = document.createElement("canvas");
    c.width = W * s;
    c.height = H * s;
    const g = c.getContext("2d");
    if (!g) return null;
    g.clearRect(0, 0, c.width, c.height);
    const px = (x: number) => x * s;
    const py = (y: number) => (H - y) * s;

    const emboss = (text: string, x: number, y: number, font: string, tracking: string, align: CanvasTextAlign = "left") => {
      g.font = font;
      g.letterSpacing = tracking;
      g.textAlign = align;
      g.textBaseline = "middle";
      g.lineJoin = "round";
      g.strokeStyle = "rgba(255,255,255,0.85)";
      g.lineWidth = s * 0.35;
      g.strokeText(text, px(x) - s * 0.22, py(y) - s * 0.22);
      g.fillStyle = "#3e3e3a";
      g.fillText(text, px(x) + s * 0.16, py(y) + s * 0.2);
      g.fillStyle = "#6e6e68";
      g.fillText(text, px(x), py(y));
    };

    emboss("SCRATCH", 18, 278, `500 ${20 * s}px Futura, "Avenir Next", "Century Gothic", sans-serif`, `${0.28 * 20 * s}px`);
    emboss("スクラッチ", 18, 250, `500 ${11 * s}px "Hiragino Sans", "Noto Sans JP", sans-serif`, `${0.22 * 11 * s}px`);

    g.strokeStyle = INK;
    g.lineWidth = 1.25 * s;
    g.lineCap = "butt";
    for (let i = 0; i < 8; i++) {
      const a = (i / 8) * Math.PI * 2 - Math.PI / 2;
      const r0 = POD / 2 + 3.2;
      const r1 = r0 + (i % 2 === 0 ? 7 : 4.5);
      g.beginPath();
      g.moveTo(px(CX + Math.cos(a) * r0), py(CY + Math.sin(a) * r0));
      g.lineTo(px(CX + Math.cos(a) * r1), py(CY + Math.sin(a) * r1));
      g.stroke();
    }

    const labels = ["CUE", "REV", "1", "2", "3", "4"];
    const keys = [36, 66, 96, 126, 156, 186];
    labels.forEach((label, i) => {
      emboss(label, keys[i], 46, `500 ${2.8 * s}px Futura, "Avenir Next", sans-serif`, `${0.06 * s}px`, "center");
    });

    const tex = new THREE.CanvasTexture(c);
    tex.anisotropy = 8;
    tex.colorSpace = THREE.SRGBColorSpace;
    return tex;
  }, []);

  return (
    <group>
      <mesh position={[W / 2, H / 2, T + 0.12]}>
        <planeGeometry args={[W - 2, H - 2]} />
        {map ? <meshBasicMaterial map={map} transparent /> : mat(GREY)}
      </mesh>
      <mesh position={[50, 236, T + 0.35]}>
        <boxGeometry args={[64, 1.4, 0.4]} />
        {mat(ORANGE, 0.4)}
      </mesh>
    </group>
  );
}

function Ring() {
  return (
    <group position={[CX, CY, T]}>
      <mesh position={[0, 0, 1.35]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[POD / 2 - 0.3, POD / 2 - 0.3, 3.5, 96]} />
        {mat(BLACK, 0.46)}
      </mesh>
      <mesh position={[0, 0, 1.4]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[18.2, 18.2, 3.7, 64]} />
        {mat("#101012", 0.5)}
      </mesh>
      {[30, 46, 62].map((r) => (
        <mesh key={r} position={[0, 0, 3.15]}>
          <torusGeometry args={[r, 0.28, 8, 96]} />
          {mat("#2a2a2c", 0.6)}
        </mesh>
      ))}
    </group>
  );
}

function Screen() {
  const map = useMemo(() => {
    const c = document.createElement("canvas");
    c.width = 256;
    c.height = 256;
    const g = c.getContext("2d");
    if (!g) return null;
    g.fillStyle = "#0a1218";
    g.beginPath();
    g.arc(128, 128, 128, 0, Math.PI * 2);
    g.fill();
    g.strokeStyle = "#2ec8ff";
    g.lineWidth = 7;
    g.lineCap = "round";
    g.beginPath();
    g.arc(128, 128, 104, -Math.PI / 2, Math.PI * 0.2);
    g.stroke();
    g.fillStyle = "#f4f1ea";
    g.font = "600 36px Futura, sans-serif";
    g.textAlign = "center";
    g.fillText("1:14", 128, 124);
    g.font = "500 13px Futura, sans-serif";
    g.fillStyle = "#8a8a84";
    g.fillText("LISTEN", 128, 148);
    const tex = new THREE.CanvasTexture(c);
    tex.colorSpace = THREE.SRGBColorSpace;
    return tex;
  }, []);
  return (
    <mesh position={[CX, CY, T + 3.7]}>
      <circleGeometry args={[16.2, 64]} />
      {map ? <meshStandardMaterial map={map} roughness={0.3} metalness={0.04} /> : mat("#0c0c0e", 0.3)}
    </mesh>
  );
}

function Knob() {
  const ridges = [];
  for (let i = 0; i < 28; i++) {
    const a = (i / 28) * Math.PI * 2;
    ridges.push(
      <mesh key={i} position={[Math.cos(a) * 7.4, Math.sin(a) * 7.4, 4.2]} rotation={[0, 0, a]}>
        <boxGeometry args={[1.5, 1.15, 7.2]} />
        {mat(ORANGE, 0.42)}
      </mesh>,
    );
  }
  return (
    <group position={[174, 252, T]}>
      <mesh position={[0, 0, 4.2]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[7.3, 7.3, 8, 40]} />
        {mat(ORANGE, 0.42)}
      </mesh>
      {ridges}
      <mesh position={[0, 0, 8.4]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[2.3, 2.3, 1.1, 24]} />
        {mat("#1a1a1c", 0.4)}
      </mesh>
    </group>
  );
}

function Fader() {
  // 125 mm body, right lane, same place as the pitch fader on the six.
  const x = 196;
  const y0 = CY - 62.5;
  const len = 125;
  return (
    <group>
      <RoundedBox args={[3.2, len, 1.1]} radius={0.6} smoothness={2} position={[x, y0 + len / 2, T + 0.5]}>
        {mat("#1a1a1c", 0.55)}
      </RoundedBox>
      <RoundedBox args={[14, 18, 5]} radius={1} smoothness={2} position={[x, y0 + len * 0.72, T + 3]}>
        {mat("#c8c8c2", 0.45)}
      </RoundedBox>
    </group>
  );
}

function Keys() {
  // 12 x 12 x 7.3 mm body under the skin. The Ø6.9 cap sits flush.
  // Push-on, push-off. The LED is in the cap.
  const xs = [36, 66, 96, 126, 156, 186];
  return (
    <group>
      {xs.map((x) => (
        <group key={x} position={[x, 26, T]}>
          <mesh position={[0, 0, 0.15]}>
            <boxGeometry args={[12.4, 12.4, 0.3]} />
            {mat("#bdbdb7", 0.8)}
          </mesh>
          <mesh position={[0, 0, 0.35]} rotation={[Math.PI / 2, 0, 0]}>
            <cylinderGeometry args={[3.45, 3.45, 0.45, 24]} />
            {mat(BLACK, 0.45)}
          </mesh>
          <mesh position={[0, 0, 0.62]} rotation={[Math.PI / 2, 0, 0]}>
            <cylinderGeometry args={[1.15, 1.15, 0.2, 16]} />
            {mat(ORANGE, 0.25)}
          </mesh>
        </group>
      ))}
    </group>
  );
}
