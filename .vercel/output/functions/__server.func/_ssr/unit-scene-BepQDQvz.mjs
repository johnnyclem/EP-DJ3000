import { i as __toESM } from "../_runtime.mjs";
import { a as Box3, g as require_react, h as require_jsx_runtime, i as useLoader, n as Canvas, p as Vector3, r as useFrame, t as OrbitControls } from "../_libs/@react-three/drei+[...].mjs";
import { t as STLLoader } from "../_libs/three.mjs";
//#region node_modules/.nitro/vite/services/ssr/assets/unit-scene-BepQDQvz.js
var import_react = /* @__PURE__ */ __toESM(require_react());
var import_jsx_runtime = require_jsx_runtime();
var GREY = "#c8c8c2";
var DECK = "#d4d4ce";
var BLACK = "#141416";
var ORANGE = "#f05a1a";
var PIECES = [
	{
		id: "bottom",
		file: "bottom.stl",
		color: GREY,
		poses: [{ position: [
			0,
			0,
			0
		] }]
	},
	{
		id: "top",
		file: "top.stl",
		color: DECK,
		poses: [{ position: [
			0,
			0,
			19.8
		] }]
	},
	{
		id: "platter",
		file: "platter.stl",
		color: BLACK,
		poses: [{ position: [
			52,
			150,
			24.9
		] }]
	},
	{
		id: "fader",
		file: "fader.stl",
		color: BLACK,
		poses: [{
			position: [
				60,
				62,
				34
			],
			rotation: [
				Math.PI,
				0,
				0
			]
		}]
	},
	{
		id: "lock",
		file: "fader_lock.stl",
		color: "#1c1c1e",
		poses: [{ position: [
			52,
			52,
			12.8
		] }]
	},
	{
		id: "knob",
		file: "knob.stl",
		color: ORANGE,
		poses: [{ position: [
			102,
			172,
			26
		] }]
	},
	{
		id: "button",
		file: "button.stl",
		color: BLACK,
		poses: [{
			position: [
				26,
				62,
				29.15
			],
			rotation: [
				Math.PI,
				0,
				0
			]
		}, {
			position: [
				94,
				62,
				29.15
			],
			rotation: [
				Math.PI,
				0,
				0
			]
		}]
	},
	{
		id: "plus",
		file: "plus.stl",
		color: ORANGE,
		poses: [{ position: [
			52,
			150,
			28
		] }]
	},
	{
		id: "tab",
		file: "tab.stl",
		color: ORANGE,
		poses: [{ position: [
			80.2,
			218.2,
			25.4
		] }]
	},
	{
		id: "bar",
		file: "bar.stl",
		color: ORANGE,
		poses: [{ position: [
			16.2,
			207.35,
			25.4
		] }]
	},
	{
		id: "foot",
		file: "foot.stl",
		color: "#2a2a28",
		poses: [
			{ position: [
				16,
				16,
				-1.45
			] },
			{ position: [
				104,
				16,
				-1.45
			] },
			{ position: [
				16,
				224,
				-1.45
			] },
			{ position: [
				104,
				224,
				-1.45
			] }
		]
	}
];
var URLS = PIECES.map((piece) => `/field-jog/stl/${piece.file}`);
function Fit({ watch, children }) {
	const ref = (0, import_react.useRef)(null);
	const done = (0, import_react.useRef)("");
	(0, import_react.useEffect)(() => {
		done.current = "";
	}, [watch]);
	useFrame((state) => {
		if (done.current === watch) return;
		const group = ref.current;
		const ctl = state.controls;
		if (!group || !ctl) return;
		group.updateWorldMatrix(true, true);
		const box = new Box3().setFromObject(group);
		if (box.isEmpty()) return;
		const center = box.getCenter(new Vector3());
		const size = box.getSize(new Vector3());
		const radius = Math.max(size.x, size.y, size.z) * .62;
		const cam = state.camera;
		const dist = radius / Math.sin(cam.fov * Math.PI / 360);
		const dir = new Vector3(.85, .7, .95).normalize();
		cam.position.copy(center).addScaledVector(dir, dist);
		cam.near = Math.max(.05, dist / 200);
		cam.far = dist * 40;
		cam.updateProjectionMatrix();
		ctl.target.copy(center);
		ctl.update();
		done.current = watch;
	});
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", {
		ref,
		children
	});
}
function Assembly({ focus }) {
	const geoms = useLoader(STLLoader, URLS);
	const show = (id) => focus === "unit" || focus === id;
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", {
		rotation: [
			-Math.PI / 2,
			0,
			0
		],
		children: PIECES.map((piece, index) => show(piece.id) ? piece.poses.map((pose, poseIndex) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)("mesh", {
			geometry: geoms[index],
			position: pose.position,
			rotation: pose.rotation,
			children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
				color: piece.color,
				roughness: .46,
				metalness: .05,
				flatShading: true,
				side: 2
			})
		}, `${piece.id}-${poseIndex}`)) : null)
	});
}
function Scene({ focus, watch, spin, onReady }) {
	(0, import_react.useEffect)(() => {
		onReady();
	}, [onReady]);
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(import_jsx_runtime.Fragment, { children: [
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)("color", {
			attach: "background",
			args: ["#1c1c19"]
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)("ambientLight", { intensity: .62 }),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)("hemisphereLight", { args: [
			"#f3f1ea",
			"#2c2c28",
			.4
		] }),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)("directionalLight", {
			position: [
				90,
				160,
				70
			],
			intensity: 1.55
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)("directionalLight", {
			position: [
				-80,
				30,
				-40
			],
			intensity: .28
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Fit, {
			watch,
			children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Assembly, { focus })
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(OrbitControls, {
			makeDefault: true,
			enableDamping: true,
			dampingFactor: .08,
			autoRotate: spin,
			autoRotateSpeed: .7
		})
	] });
}
function UnitScene({ focus, watch, spin, onReady, onInteract }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Canvas, {
		dpr: [1, 1.75],
		camera: {
			fov: 32,
			position: [
				220,
				180,
				260
			],
			near: .1,
			far: 4e3
		},
		gl: {
			antialias: true,
			alpha: false
		},
		onPointerDown: onInteract,
		children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(import_react.Suspense, {
			fallback: null,
			children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Scene, {
				focus,
				watch,
				spin,
				onReady
			})
		})
	});
}
//#endregion
export { UnitScene };
