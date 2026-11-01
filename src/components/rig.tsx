import { RoundedBox } from "@react-three/drei";
import type { ThreeEvent } from "@react-three/fiber";
import * as THREE from "three";
import type { FaceId, GearId, Slots } from "./viewer-parts";

const GREY = "#d2d2cc";
const CREAM = "#e4dcc8";
const GREEN = "#1f6b45";
const BLACK = "#141416";
const ORANGE = "#f05a1a";
const BRICK = "#c8c8c2";
const INK = "#2a2a28";

const AMP_W = 229;
const AMP_D = 176;
const AMP_H = 269;
const GAP = 10;

const GEAR: Record<GearId, { w: number; t: number; h: number; top: [number, number, number]; color: string }> = {
  jog: { w: 88, t: 26, h: 240, top: [240, 88, 26], color: GREY },
  ko: { w: 176, t: 16, h: 240, top: [240, 176, 16], color: GREY },
  riddim: { w: 176, t: 16, h: 240, top: [240, 176, 16], color: CREAM },
  mixer: { w: 88, t: 16, h: 240, top: [240, 88, 16], color: GREY },
  pedal: { w: 176, t: 26, h: 84, top: [176, 84, 26], color: "#eceae4" },
  ting: { w: 56, t: 16, h: 88, top: [88, 56, 16], color: CREAM },
};

function qBasis(x: [number, number, number], y: [number, number, number], z: [number, number, number]): [number, number, number, number] {
  const q = new THREE.Quaternion().setFromRotationMatrix(
    new THREE.Matrix4().makeBasis(new THREE.Vector3(...x), new THREE.Vector3(...y), new THREE.Vector3(...z)),
  );
  return [q.x, q.y, q.z, q.w];
}

export function jogPose(face: FaceId): { position: [number, number, number]; quaternion: [number, number, number, number] } {
  if (face === "right") {
    return { position: [AMP_W / 2 + GAP + 13, 0, 120], quaternion: qBasis([0, 1, 0], [0, 0, 1], [1, 0, 0]) };
  }
  if (face === "left") {
    return { position: [-(AMP_W / 2 + GAP + 13), 0, 120], quaternion: qBasis([0, -1, 0], [0, 0, 1], [-1, 0, 0]) };
  }
  if (face === "back") {
    return { position: [0, AMP_D / 2 + GAP + 13, 120], quaternion: qBasis([-1, 0, 0], [0, 0, 1], [0, 1, 0]) };
  }
  return { position: [0, 0, AMP_H + GAP + 13], quaternion: qBasis([0, -1, 0], [1, 0, 0], [0, 0, 1]) };
}

function pose(face: FaceId, gear: GearId) {
  const g = GEAR[gear];
  if (face === "top") {
    return {
      position: [0, 0, AMP_H + GAP + g.top[2] / 2] as [number, number, number],
      args: g.top,
      panel: "up" as const,
    };
  }
  if (face === "back") {
    return {
      position: [0, AMP_D / 2 + GAP + g.t / 2, g.h / 2] as [number, number, number],
      args: [g.w, g.t, g.h] as [number, number, number],
      panel: "back" as const,
    };
  }
  const sign = face === "right" ? 1 : -1;
  return {
    position: [sign * (AMP_W / 2 + GAP + g.t / 2), 0, g.h / 2] as [number, number, number],
    args: [g.t, g.w, g.h] as [number, number, number],
    panel: face,
  };
}

export function Rig({
  slots,
  face,
  onPick,
}: {
  slots: Slots;
  face: FaceId;
  onPick: (face: FaceId) => void;
}) {
  const pick = (id: FaceId) => (event: ThreeEvent<MouseEvent>) => {
    event.stopPropagation();
    onPick(id);
  };

  return (
    <group>
      <Amp onPick={pick} active={face} />
      {(Object.keys(slots) as FaceId[]).map((id) => {
        const gear = slots[id];
        if (!gear || gear === "jog") return null;
        const placed = pose(id, gear);
        return (
          <group key={id} onClick={pick(id)}>
            <Body args={placed.args} position={placed.position} color={GEAR[gear].color} />
            <FaceMark gear={gear} args={placed.args} position={placed.position} panel={placed.panel} />
            <Joint face={id} gear={gear} />
          </group>
        );
      })}
      {slots.left === "jog" || slots.right === "jog" || slots.back === "jog" || slots.top === "jog" ? (
        <Joint face={faceOfJog(slots)} gear="jog" />
      ) : null}
    </group>
  );
}

function faceOfJog(slots: Slots): FaceId {
  return (Object.keys(slots) as FaceId[]).find((id) => slots[id] === "jog") ?? "right";
}

function Amp({
  onPick,
  active,
}: {
  onPick: (id: FaceId) => (event: ThreeEvent<MouseEvent>) => void;
  active: FaceId;
}) {
  return (
    <group>
      <RoundedBox
        args={[AMP_W, AMP_D, AMP_H]}
        position={[0, 0, AMP_H / 2]}
        radius={3}
        smoothness={2}
        onClick={onPick(active)}
      >
        <meshStandardMaterial color={GREY} roughness={0.48} metalness={0.04} />
      </RoundedBox>
      <mesh position={[0, -AMP_D / 2 - 1, AMP_H / 2 - 20]}>
        <boxGeometry args={[170, 2, 150]} />
        <meshStandardMaterial color="#1c1c1c" roughness={0.8} />
      </mesh>
      <mesh position={[0, -AMP_D / 2 - 2, AMP_H / 2 + 10]}>
        <cylinderGeometry args={[46, 46, 3, 24]} />
        <meshStandardMaterial color="#2a2a2a" />
      </mesh>
      <mesh position={[-40, -AMP_D / 2 - 2, AMP_H / 2 + 55]}>
        <cylinderGeometry args={[16, 16, 3, 16]} />
        <meshStandardMaterial color="#2a2a2a" />
      </mesh>
      <mesh position={[0, 0, AMP_H + 8]}>
        <boxGeometry args={[90, 16, 14]} />
        <meshStandardMaterial color="#bdbdb6" />
      </mesh>
      <mesh position={[-80, 0, AMP_H + 6]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry args={[5, 5, 4, 12]} />
        <meshStandardMaterial color={ORANGE} />
      </mesh>
      {([-1, 1] as const).map((side) =>
        [70, 110, 150, 190].map((z) => (
          <mesh key={`${side}-${z}`} position={[side * (AMP_W / 2), 0, z]} rotation={[0, 0, Math.PI / 2]}>
            <cylinderGeometry args={[3.2, 3.2, 3, 10]} />
            <meshStandardMaterial color={INK} />
          </mesh>
        )),
      )}
      <Hit id="left" onPick={onPick} active={active} position={[-(AMP_W / 2 + 1), 0, AMP_H / 2]} args={[4, AMP_D - 8, AMP_H - 16]} />
      <Hit id="right" onPick={onPick} active={active} position={[AMP_W / 2 + 1, 0, AMP_H / 2]} args={[4, AMP_D - 8, AMP_H - 16]} />
      <Hit id="back" onPick={onPick} active={active} position={[0, AMP_D / 2 + 1, AMP_H / 2]} args={[AMP_W - 8, 4, AMP_H - 16]} />
      <Hit id="top" onPick={onPick} active={active} position={[0, 0, AMP_H + 1]} args={[AMP_W - 8, AMP_D - 8, 4]} />
    </group>
  );
}

function Hit({
  id,
  onPick,
  active,
  position,
  args,
}: {
  id: FaceId;
  onPick: (id: FaceId) => (event: ThreeEvent<MouseEvent>) => void;
  active: FaceId;
  position: [number, number, number];
  args: [number, number, number];
}) {
  return (
    <mesh position={position} onClick={onPick(id)}>
      <boxGeometry args={args} />
      <meshStandardMaterial color={ORANGE} transparent opacity={active === id ? 0.28 : 0.02} depthWrite={false} />
    </mesh>
  );
}

function Body({
  args,
  position,
  color,
}: {
  args: [number, number, number];
  position: [number, number, number];
  color: string;
}) {
  const radius = Math.min(2, args[0] / 2 - 0.4, args[1] / 2 - 0.4, args[2] / 2 - 0.4);
  return (
    <RoundedBox args={args} position={position} radius={Math.max(0.6, radius)} smoothness={2}>
      <meshStandardMaterial color={color} roughness={0.48} metalness={0.04} />
    </RoundedBox>
  );
}

function FaceMark({
  gear,
  args,
  position,
  panel,
}: {
  gear: GearId;
  args: [number, number, number];
  position: [number, number, number];
  panel: "left" | "right" | "back" | "up";
}) {
  const [w, d, h] = args;
  const side = panel === "left" || panel === "right";
  const sign = panel === "left" ? -1 : 1;
  const lift: [number, number, number] = side
    ? [sign * (w / 2 + 0.6), 0, 0]
    : panel === "up"
      ? [0, 0, h / 2 + 0.6]
      : [0, d / 2 + 0.6, 0];
  const at: [number, number, number] = [position[0] + lift[0], position[1] + lift[1], position[2] + lift[2]];
  const faceW = side ? d : w;
  const faceH = side ? h : panel === "up" ? d : h;
  const screen = gear === "riddim" ? GREEN : gear === "ting" || gear === "pedal" ? ORANGE : gear === "mixer" ? INK : BLACK;
  const screenArgs: [number, number, number] = side ? [1.2, faceW * 0.72, faceH * 0.22] : [faceW * 0.72, faceH * 0.22, 1.2];
  return (
    <group position={at} rotation={panel === "up" || side ? [0, 0, 0] : [Math.PI / 2, 0, 0]}>
      <mesh>
        <boxGeometry args={screenArgs} />
        <meshStandardMaterial color={screen} roughness={0.4} />
      </mesh>
      {gear === "ko" || gear === "riddim" || gear === "mixer" ? (
        <mesh position={side ? [sign * 0.8, faceW * 0.28, faceH * 0.12] : [faceW * 0.28, faceH * 0.12, 0.4]} rotation={side ? [0, 0, Math.PI / 2] : [Math.PI / 2, 0, 0]}>
          <cylinderGeometry args={[Math.min(8, faceW * 0.08), Math.min(8, faceW * 0.08), 1.4, 16]} />
          <meshStandardMaterial color={ORANGE} />
        </mesh>
      ) : null}
      {gear === "pedal"
        ? [-1, 1].map((pad) => (
            <mesh key={pad} position={side ? [sign * 5, pad * faceW * 0.22, 0] : [pad * w * 0.22, 0, 4]} rotation={side ? [0, 0, Math.PI / 2] : [0, 0, 0]}>
              <cylinderGeometry args={[Math.min(22, faceW * 0.28), Math.min(24, faceW * 0.3), 8, 20]} />
              <meshStandardMaterial color={BLACK} />
            </mesh>
          ))
        : null}
    </group>
  );
}

function pinHeights(h: number) {
  const spots = [h * 0.38, h * 0.72].filter((z) => z > 18 && z < h - 14);
  return spots.length > 0 ? spots : [Math.max(20, h / 2)];
}

function Joint({ face, gear }: { face: FaceId; gear: GearId }) {
  const g = GEAR[gear];
  const heights = pinHeights(face === "top" ? 40 : g.h);
  if (face === "top") {
    const z = AMP_H + GAP / 2;
    return (
      <group>
        {[-40, 40].map((x) => (
          <Brick key={x} position={[x, 0, z]} args={[28, 14, GAP + 4]} />
        ))}
      </group>
    );
  }
  if (face === "back") {
    const y = AMP_D / 2 + GAP / 2;
    return (
      <group>
        {heights.map((z) => (
          <Brick key={z} position={[0, y, z]} args={[18, GAP + 4, 28]} />
        ))}
      </group>
    );
  }
  const x = (face === "right" ? 1 : -1) * (AMP_W / 2 + GAP / 2);
  return (
    <group>
      {heights.map((z) => (
        <Brick key={z} position={[x, 0, z]} args={[GAP + 4, 18, 28]} />
      ))}
    </group>
  );
}

function Brick({ position, args }: { position: [number, number, number]; args: [number, number, number] }) {
  return (
    <group position={position}>
      <mesh>
        <boxGeometry args={args} />
        <meshStandardMaterial color={BRICK} roughness={0.55} />
      </mesh>
      <mesh>
        <boxGeometry args={[args[0] * 0.35, args[1] * 0.35, args[2] * 0.55]} />
        <meshStandardMaterial color={ORANGE} emissive={ORANGE} emissiveIntensity={0.25} />
      </mesh>
    </group>
  );
}
