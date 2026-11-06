import { useEffect, useRef } from "react";
import { Canvas, useFrame } from "@react-three/fiber";
import { OrbitControls, RoundedBox } from "@react-three/drei";
import type { Group, PerspectiveCamera } from "three";
import type { OrbitControls as OrbitControlsImpl } from "three-stdlib";
import * as THREE from "three";
import { TILES, TRAY } from "@/data/four-kit";
import { PortaDeck } from "./porta";

const RESIN = "#e6e2d8";
const TRAY_COLOR = "#b7b7b0";
const CUT = "#141416";

function Fit({ watch, children }: { watch: string; children: React.ReactNode }) {
  const ref = useRef<Group>(null);
  const done = useRef("");

  useEffect(() => {
    done.current = "";
  }, [watch]);

  useFrame((state) => {
    const group = ref.current;
    const ctl = state.controls as OrbitControlsImpl | null;
    if (!group || !ctl || done.current === watch) return;
    group.updateWorldMatrix(true, true);
    const box = new THREE.Box3().setFromObject(group);
    if (box.isEmpty()) return;
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    const radius = Math.max(size.x, size.y, size.z) * 0.62;
    const cam = state.camera as PerspectiveCamera;
    const dist = radius / Math.sin((cam.fov * Math.PI) / 360);
    const dir = new THREE.Vector3(0.85, 0.72, 0.95).normalize();
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

function Kit({ explode }: { explode: boolean }) {
  const lift = explode ? 22 : 0;
  return (
    <group>
      <RoundedBox
        args={[TRAY.w, TRAY.h, TRAY.t]}
        radius={2.4}
        smoothness={2}
        position={[TRAY.w / 2, TRAY.h / 2, TRAY.t / 2]}
      >
        <meshStandardMaterial color={TRAY_COLOR} roughness={0.62} metalness={0.04} />
      </RoundedBox>
      {TILES.map((tile) => {
        const w = tile.x1 - tile.x0;
        const h = tile.y1 - tile.y0;
        return (
          <group key={tile.id} position={[0, 0, lift]}>
            <mesh position={[(tile.x0 + tile.x1) / 2, (tile.y0 + tile.y1) / 2, TRAY.seat + 1.1]}>
              <boxGeometry args={[w, h, 2.2]} />
              <meshStandardMaterial color={tile.id === "screen" ? "#d5d0c6" : RESIN} roughness={0.4} />
            </mesh>
            {tile.holes.map((hole, index) =>
              hole.kind === "round" ? (
                <mesh
                  key={index}
                  position={[(hole.x0 + hole.x1) / 2, (hole.y0 + hole.y1) / 2, TRAY.seat + 2.3]}
                  rotation={[Math.PI / 2, 0, 0]}
                >
                  <cylinderGeometry args={[(hole.x1 - hole.x0) / 2, (hole.x1 - hole.x0) / 2, 0.6, 20]} />
                  <meshStandardMaterial color={CUT} roughness={0.5} />
                </mesh>
              ) : (
                <mesh
                  key={index}
                  position={[(hole.x0 + hole.x1) / 2, (hole.y0 + hole.y1) / 2, TRAY.seat + 2.3]}
                >
                  <boxGeometry args={[hole.x1 - hole.x0, hole.y1 - hole.y0, 0.6]} />
                  <meshStandardMaterial color={CUT} roughness={0.5} />
                </mesh>
              ),
            )}
          </group>
        );
      })}
    </group>
  );
}

export function FourScene({ view = "unit" }: { view?: "unit" | "pieces" }) {
  return (
    <Canvas
      dpr={[1, 1.75]}
      camera={{ fov: 32, position: [280, 240, 320], near: 0.1, far: 8000 }}
      gl={{ antialias: true, alpha: false }}
    >
      <color attach="background" args={["#1c1c19"]} />
      <ambientLight intensity={0.62} />
      <hemisphereLight args={["#f3f1ea", "#2c2c28", 0.4]} />
      <directionalLight position={[90, 160, 70]} intensity={1.55} />
      <directionalLight position={[-80, 30, -40]} intensity={0.28} />
      <Fit watch={view}>
        <group rotation={[-Math.PI / 2, 0, 0]}>
          {view === "pieces" ? <Kit explode /> : <PortaDeck />}
        </group>
      </Fit>
      <OrbitControls makeDefault enableDamping dampingFactor={0.08} autoRotate autoRotateSpeed={0.45} />
    </Canvas>
  );
}
