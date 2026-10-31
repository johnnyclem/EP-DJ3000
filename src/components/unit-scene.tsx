import { Suspense, useEffect, useRef } from "react";
import { Canvas, useFrame, useLoader } from "@react-three/fiber";
import { OrbitControls } from "@react-three/drei";
import { STLLoader } from "three/addons/loaders/STLLoader.js";
import * as THREE from "three";
import type { Group, PerspectiveCamera } from "three";
import type { OrbitControls as OrbitControlsImpl } from "three-stdlib";
import type { ViewId } from "./viewer-parts";

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
  { id: "top", file: "top.stl", color: DECK, poses: [{ position: [0, 0, 19.8] }] },
  { id: "platter", file: "platter.stl", color: BLACK, poses: [{ position: [52, 150, 24.9] }] },
  {
    id: "fader",
    file: "fader.stl",
    color: BLACK,
    poses: [{ position: [60, 62, 34], rotation: [Math.PI, 0, 0] }],
  },
  { id: "lock", file: "fader_lock.stl", color: "#1c1c1e", poses: [{ position: [52, 52, 12.8] }] },
  { id: "knob", file: "knob.stl", color: ORANGE, poses: [{ position: [102, 172, 26] }] },
  {
    id: "button",
    file: "button.stl",
    color: BLACK,
    poses: [
      { position: [26, 62, 29.15], rotation: [Math.PI, 0, 0] },
      { position: [94, 62, 29.15], rotation: [Math.PI, 0, 0] },
    ],
  },
  { id: "plus", file: "plus.stl", color: ORANGE, poses: [{ position: [52, 150, 28] }] },
  { id: "tab", file: "tab.stl", color: ORANGE, poses: [{ position: [80.2, 218.2, 25.4] }] },
  { id: "bar", file: "bar.stl", color: ORANGE, poses: [{ position: [16.2, 207.35, 25.4] }] },
  {
    id: "foot",
    file: "foot.stl",
    color: FOOT,
    poses: [
      { position: [16, 16, -1.45] },
      { position: [104, 16, -1.45] },
      { position: [16, 224, -1.45] },
      { position: [104, 224, -1.45] },
    ],
  },
];

const URLS = PIECES.map((piece) => `/field-jog/stl/${piece.file}`);

function Fit({ watch, children }: { watch: string; children: React.ReactNode }) {
  const ref = useRef<Group>(null);
  const done = useRef("");

  useEffect(() => {
    done.current = "";
  }, [watch]);

  useFrame((state) => {
    if (done.current === watch) return;
    const group = ref.current;
    const ctl = state.controls as OrbitControlsImpl | null;
    if (!group || !ctl) return;
    group.updateWorldMatrix(true, true);
    const box = new THREE.Box3().setFromObject(group);
    if (box.isEmpty()) return;
    const center = box.getCenter(new THREE.Vector3());
    const size = box.getSize(new THREE.Vector3());
    const radius = Math.max(size.x, size.y, size.z) * 0.62;
    const cam = state.camera as PerspectiveCamera;
    const dist = radius / Math.sin((cam.fov * Math.PI) / 360);
    const dir = new THREE.Vector3(0.85, 0.7, 0.95).normalize();
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

function Assembly({ focus }: { focus: ViewId }) {
  const geoms = useLoader(STLLoader, URLS);
  const show = (id: Piece["id"]) => focus === "unit" || focus === id;

  return (
    <group rotation={[-Math.PI / 2, 0, 0]}>
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
    </group>
  );
}

function Scene({
  focus,
  watch,
  spin,
  onReady,
}: {
  focus: ViewId;
  watch: string;
  spin: boolean;
  onReady: () => void;
}) {
  useEffect(() => {
    onReady();
  }, [onReady]);

  return (
    <>
      <color attach="background" args={["#1c1c19"]} />
      <ambientLight intensity={0.62} />
      <hemisphereLight args={["#f3f1ea", "#2c2c28", 0.4]} />
      <directionalLight position={[90, 160, 70]} intensity={1.55} />
      <directionalLight position={[-80, 30, -40]} intensity={0.28} />
      <Fit watch={watch}>
        <Assembly focus={focus} />
      </Fit>
      <OrbitControls
        makeDefault
        enableDamping
        dampingFactor={0.08}
        autoRotate={spin}
        autoRotateSpeed={0.7}
      />
    </>
  );
}

export function UnitScene({
  focus,
  watch,
  spin,
  onReady,
  onInteract,
}: {
  focus: ViewId;
  watch: string;
  spin: boolean;
  onReady: () => void;
  onInteract: () => void;
}) {
  return (
    <Canvas
      dpr={[1, 1.75]}
      camera={{ fov: 32, position: [220, 180, 260], near: 0.1, far: 4000 }}
      gl={{ antialias: true, alpha: false }}
      onPointerDown={onInteract}
    >
      <Suspense fallback={null}>
        <Scene focus={focus} watch={watch} spin={spin} onReady={onReady} />
      </Suspense>
    </Canvas>
  );
}
