import { Suspense, useEffect, useRef } from "react";
import { Canvas, useFrame, useLoader } from "@react-three/fiber";
import { OrbitControls } from "@react-three/drei";
import { STLLoader } from "three/addons/loaders/STLLoader.js";
import * as THREE from "three";
import type { Group, PerspectiveCamera } from "three";
import type { OrbitControls as OrbitControlsImpl } from "three-stdlib";
import type { FaceId, Slots, ViewId } from "./viewer-parts";
import { Rig, jogPose } from "./rig";
import { WideDeck, type WideLayout, type WidePart } from "./wide-deck";
import { CarryDeck, type CarryPart } from "./carry-deck";

const GREY = "#c8c8c2";
const DECK = "#d4d4ce";
const BLACK = "#141416";
const ORANGE = "#f05a1a";
const FOOT = "#2a2a28";

type Pose = {
  position: [number, number, number];
  rotation?: [number, number, number];
};

type Piece = {
  id: Exclude<ViewId, "unit">;
  file: string;
  color: string;
  poses: Pose[];
};

// Print STLs sit on the origin. These poses are the OpenSCAD assembly
// (Z up). The scene group turns that into three.js Y-up.
const PIECES: Piece[] = [
  { id: "bottom", file: "bottom.stl", color: GREY, poses: [{ position: [0, 0, 0] }] },
  { id: "top", file: "top.stl", color: DECK, poses: [{ position: [0, 0, 11.8] }] },
  { id: "platter", file: "platter.stl", color: BLACK, poses: [{ position: [44, 148, 15.3] }] },
  { id: "fader", file: "fader.stl", color: BLACK, poses: [{ position: [44, 58, 24], rotation: [Math.PI, 0, 0] }] },
  { id: "lock", file: "fader_lock.stl", color: "#1c1c1e", poses: [{ position: [36, 48, 5.2] }] },
  { id: "knob", file: "knob.stl", color: ORANGE, poses: [{ position: [78, 216, 22] }] },
  {
    id: "button",
    file: "button.stl",
    color: BLACK,
    poses: [
      { position: [17, 36, 25], rotation: [Math.PI, 0, 0] },
      { position: [17, 60, 25], rotation: [Math.PI, 0, 0] },
    ],
  },
  {
    id: "flange",
    file: "flange.stl",
    color: "#2a2a2c",
    poses: [
      { position: [17, 36, 9.85] },
      { position: [17, 60, 9.85] },
    ],
  },
  { id: "plus", file: "plus.stl", color: ORANGE, poses: [{ position: [44, 148, 18.4] }] },
  {
    id: "foot",
    file: "foot.stl",
    color: FOOT,
    poses: [
      { position: [16, 16, -1.45] },
      { position: [72, 16, -1.45] },
      { position: [16, 224, -1.45] },
      { position: [72, 224, -1.45] },
    ],
  },
];

const URLS = PIECES.map((piece) => `/field-jog/stl/${piece.file}`);

function Fit({ watch, children }: { watch: string; children: React.ReactNode }) {
  const ref = useRef<Group>(null);
  const done = useRef("");
  const span = useRef(0);

  useEffect(() => {
    done.current = "";
  }, [watch]);

  useFrame((state) => {
    const group = ref.current;
    const ctl = state.controls as OrbitControlsImpl | null;
    if (!group || !ctl) return;
    group.updateWorldMatrix(true, true);
    const box = new THREE.Box3().setFromObject(group);
    if (box.isEmpty()) return;
    const size = box.getSize(new THREE.Vector3());
    const next = size.x + size.y + size.z;
    if (done.current === watch && Math.abs(next - span.current) < 8) return;
    span.current = next;
    const center = box.getCenter(new THREE.Vector3());
    const wide = size.x > size.z * 1.3;
    const radius = Math.max(size.x, size.y, size.z) * (wide ? 0.52 : 0.62);
    const cam = state.camera as PerspectiveCamera;
    const dist = radius / Math.sin((cam.fov * Math.PI) / 360);
    const dir = (wide ? new THREE.Vector3(0.2, 1.15, 0.78) : new THREE.Vector3(0.85, 0.72, 0.95)).normalize();
    cam.position.copy(center).addScaledVector(dir, dist);
    cam.near = Math.max(0.05, dist / 200);
    cam.far = dist * 40;
    cam.updateProjectionMatrix();
    ctl.target.copy(center);
    ctl.update();
    done.current = watch;
  });

  return <group ref={ref}>{children}</group>;
}

const TOGGLES: [number, number][] = [
  [17, 86],
  [17, 102],
  [71, 48],
];

function Tacts() {
  return (
    <group>
      {TOGGLES.map(([x, y]) => (
        <mesh key={`${x}-${y}`} position={[x, y, 16.8]} rotation={[Math.PI / 2, 0, 0]}>
          <cylinderGeometry args={[3.45, 3.45, 1.6, 28]} />
          <meshStandardMaterial color={BLACK} roughness={0.45} metalness={0.05} />
        </mesh>
      ))}
    </group>
  );
}

function Assembly({ focus }: { focus: ViewId | "jog" | "rig" }) {
  const geoms = useLoader(STLLoader, URLS);
  const show = (id: Piece["id"]) => focus === "unit" || focus === "jog" || focus === "rig" || focus === id;

  return (
    <group>
      {PIECES.map((piece, index) =>
        show(piece.id)
          ? piece.poses.map((pose, poseIndex) => (
              <mesh
                key={`${piece.id}-${poseIndex}`}
                geometry={geoms[index]}
                position={pose.position}
                rotation={pose.rotation}
              >
                <meshStandardMaterial
                  color={piece.color}
                  roughness={0.46}
                  metalness={0.05}
                  flatShading
                  side={THREE.DoubleSide}
                />
              </mesh>
            ))
          : null,
      )}
      {focus === "unit" || focus === "jog" || focus === "rig" || focus === "loop" ? <Tacts /> : null}
    </group>
  );
}

function Scene({
  mode,
  focus,
  slots,
  activeFace,
  watch,
  spin,
  layout,
  wideFocus,
  carryFocus,
  onReady,
  onPickFace,
}: {
  mode: "unit" | "rig" | "wide" | "carry";
  focus: ViewId;
  slots: Slots;
  activeFace: FaceId;
  watch: string;
  spin: boolean;
  layout: WideLayout;
  wideFocus: WidePart;
  carryFocus: CarryPart;
  onReady: () => void;
  onPickFace: (face: FaceId) => void;
}) {
  useEffect(() => {
    onReady();
  }, [onReady]);

  const jogFace = (Object.keys(slots) as FaceId[]).find((id) => slots[id] === "jog");
  const jog = jogFace ? jogPose(jogFace) : null;

  return (
    <>
      <color attach="background" args={[mode === "carry" ? "#f4f2ec" : "#1c1c19"]} />
      <ambientLight intensity={0.62} />
      <hemisphereLight args={["#f3f1ea", "#2c2c28", 0.4]} />
      <directionalLight position={[90, 160, 70]} intensity={1.55} />
      <directionalLight position={[-80, 30, -40]} intensity={0.28} />
      <Fit watch={watch}>
        <group rotation={[-Math.PI / 2, 0, 0]}>
          {mode === "unit" ? (
            <Suspense fallback={null}>
              <Assembly focus={focus} />
            </Suspense>
          ) : null}
          {mode === "wide" ? <WideDeck layout={layout} focus={wideFocus} /> : null}
          {mode === "carry" ? <CarryDeck focus={carryFocus} /> : null}
          {mode === "rig" && jog && jogFace ? (
            <Suspense fallback={null}>
              <group position={jog.position} quaternion={jog.quaternion}>
                <group position={[-44, -120, -8]}>
                  <Assembly focus="rig" />
                </group>
              </group>
            </Suspense>
          ) : null}
          {mode === "rig" ? <Rig slots={slots} face={activeFace} onPick={onPickFace} /> : null}
        </group>
      </Fit>
      <OrbitControls
        makeDefault
        enableDamping
        dampingFactor={0.08}
        autoRotate={(mode === "unit" || mode === "wide" || mode === "carry") && spin}
        autoRotateSpeed={0.45}
      />
    </>
  );
}

export function UnitScene({
  mode,
  focus,
  slots,
  activeFace,
  watch,
  spin,
  layout,
  wideFocus,
  carryFocus,
  onReady,
  onInteract,
  onPickFace,
}: {
  mode: "unit" | "rig" | "wide" | "carry";
  focus: ViewId;
  slots: Slots;
  activeFace: FaceId;
  watch: string;
  spin: boolean;
  layout: WideLayout;
  wideFocus: WidePart;
  carryFocus: CarryPart;
  onReady: () => void;
  onInteract: () => void;
  onPickFace: (face: FaceId) => void;
}) {
  return (
    <Canvas
      dpr={[1, 1.75]}
      camera={{ fov: 32, position: [280, 240, 320], near: 0.1, far: 8000 }}
      gl={{ antialias: true, alpha: false }}
      onPointerDown={onInteract}
      onCreated={({ camera }) => {
        camera.lookAt(0, 90, 0);
        onReady();
      }}
    >
      <Scene
        mode={mode}
        focus={focus}
        slots={slots}
        activeFace={activeFace}
        watch={watch}
        spin={spin}
        layout={layout}
        wideFocus={wideFocus}
        carryFocus={carryFocus}
        onReady={onReady}
        onPickFace={onPickFace}
      />
    </Canvas>
  );
}
