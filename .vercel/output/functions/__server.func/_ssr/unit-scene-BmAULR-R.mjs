import { i as __toESM } from "../_runtime.mjs";
import { a as useLoader, g as Vector3, i as useFrame, m as Quaternion, n as OrbitControls, o as Box3, p as Matrix4, r as Canvas, t as RoundedBox, v as require_jsx_runtime, y as require_react } from "../_libs/@react-three/drei+[...].mjs";
import { n as WideDeck } from "./routes-BViQ00wP.mjs";
import { t as STLLoader } from "../_libs/three.mjs";
//#region node_modules/.nitro/vite/services/ssr/assets/unit-scene-BmAULR-R.js
var import_react = /* @__PURE__ */ __toESM(require_react());
var import_jsx_runtime = require_jsx_runtime();
var GREY$1 = "#d2d2cc";
var CREAM = "#e4dcc8";
var GREEN = "#1f6b45";
var BLACK$1 = "#141416";
var ORANGE$1 = "#f05a1a";
var BRICK = "#c8c8c2";
var INK = "#2a2a28";
var AMP_W = 229;
var AMP_D = 176;
var AMP_H = 269;
var GEAR = {
	jog: {
		w: 88,
		t: 16,
		h: 240,
		top: [
			240,
			88,
			16
		],
		color: GREY$1
	},
	ko: {
		w: 176,
		t: 16,
		h: 240,
		top: [
			240,
			176,
			16
		],
		color: GREY$1
	},
	riddim: {
		w: 176,
		t: 16,
		h: 240,
		top: [
			240,
			176,
			16
		],
		color: CREAM
	},
	mixer: {
		w: 88,
		t: 16,
		h: 240,
		top: [
			240,
			88,
			16
		],
		color: GREY$1
	},
	pedal: {
		w: 176,
		t: 26,
		h: 84,
		top: [
			176,
			84,
			26
		],
		color: "#eceae4"
	},
	ting: {
		w: 56,
		t: 16,
		h: 88,
		top: [
			88,
			56,
			16
		],
		color: CREAM
	}
};
function qBasis(x, y, z) {
	const q = new Quaternion().setFromRotationMatrix(new Matrix4().makeBasis(new Vector3(...x), new Vector3(...y), new Vector3(...z)));
	return [
		q.x,
		q.y,
		q.z,
		q.w
	];
}
function jogPose(face) {
	if (face === "right") return {
		position: [
			132.5,
			0,
			120
		],
		quaternion: qBasis([
			0,
			1,
			0
		], [
			0,
			0,
			1
		], [
			1,
			0,
			0
		])
	};
	if (face === "left") return {
		position: [
			-132.5,
			0,
			120
		],
		quaternion: qBasis([
			0,
			-1,
			0
		], [
			0,
			0,
			1
		], [
			-1,
			0,
			0
		])
	};
	if (face === "back") return {
		position: [
			0,
			106,
			120
		],
		quaternion: qBasis([
			-1,
			0,
			0
		], [
			0,
			0,
			1
		], [
			0,
			1,
			0
		])
	};
	return {
		position: [
			0,
			0,
			287
		],
		quaternion: qBasis([
			0,
			-1,
			0
		], [
			1,
			0,
			0
		], [
			0,
			0,
			1
		])
	};
}
function pose(face, gear) {
	const g = GEAR[gear];
	if (face === "top") return {
		position: [
			0,
			0,
			279 + g.top[2] / 2
		],
		args: g.top,
		panel: "up"
	};
	if (face === "back") return {
		position: [
			0,
			98 + g.t / 2,
			g.h / 2
		],
		args: [
			g.w,
			g.t,
			g.h
		],
		panel: "back"
	};
	return {
		position: [
			(face === "right" ? 1 : -1) * (124.5 + g.t / 2),
			0,
			g.h / 2
		],
		args: [
			g.t,
			g.w,
			g.h
		],
		panel: face
	};
}
function Rig({ slots, face, onPick }) {
	const pick = (id) => (event) => {
		event.stopPropagation();
		onPick(id);
	};
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Amp, {
			onPick: pick,
			active: face
		}),
		Object.keys(slots).map((id) => {
			const gear = slots[id];
			if (!gear || gear === "jog") return null;
			const placed = pose(id, gear);
			return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", {
				onClick: pick(id),
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Body, {
						args: placed.args,
						position: placed.position,
						color: GEAR[gear].color
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(FaceMark, {
						gear,
						args: placed.args,
						position: placed.position,
						panel: placed.panel
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Joint, {
						face: id,
						gear
					})
				]
			}, id);
		}),
		slots.left === "jog" || slots.right === "jog" || slots.back === "jog" || slots.top === "jog" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Joint, {
			face: faceOfJog(slots),
			gear: "jog"
		}) : null
	] });
}
function faceOfJog(slots) {
	return Object.keys(slots).find((id) => slots[id] === "jog") ?? "right";
}
function Amp({ onPick, active }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
			args: [
				AMP_W,
				AMP_D,
				AMP_H
			],
			position: [
				0,
				0,
				AMP_H / 2
			],
			radius: 3,
			smoothness: 2,
			onClick: onPick(active),
			children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
				color: GREY$1,
				roughness: .48,
				metalness: .04
			})
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
			position: [
				0,
				-89,
				AMP_H / 2 - 20
			],
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
				170,
				2,
				150
			] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
				color: "#1c1c1c",
				roughness: .8
			})]
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
			position: [
				0,
				-90,
				144.5
			],
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
				46,
				46,
				3,
				24
			] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: "#2a2a2a" })]
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
			position: [
				-40,
				-90,
				189.5
			],
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
				16,
				16,
				3,
				16
			] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: "#2a2a2a" })]
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
			position: [
				0,
				0,
				277
			],
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
				90,
				16,
				14
			] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: "#bdbdb6" })]
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
			position: [
				-80,
				0,
				275
			],
			rotation: [
				Math.PI / 2,
				0,
				0
			],
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
				5,
				5,
				4,
				12
			] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: ORANGE$1 })]
		}),
		[-1, 1].map((side) => [
			70,
			110,
			150,
			190
		].map((z) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
			position: [
				side * (AMP_W / 2),
				0,
				z
			],
			rotation: [
				0,
				0,
				Math.PI / 2
			],
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
				3.2,
				3.2,
				3,
				10
			] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: INK })]
		}, `${side}-${z}`))),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Hit, {
			id: "left",
			onPick,
			active,
			position: [
				-115.5,
				0,
				AMP_H / 2
			],
			args: [
				4,
				168,
				253
			]
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Hit, {
			id: "right",
			onPick,
			active,
			position: [
				115.5,
				0,
				AMP_H / 2
			],
			args: [
				4,
				168,
				253
			]
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Hit, {
			id: "back",
			onPick,
			active,
			position: [
				0,
				89,
				AMP_H / 2
			],
			args: [
				221,
				4,
				253
			]
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Hit, {
			id: "top",
			onPick,
			active,
			position: [
				0,
				0,
				270
			],
			args: [
				221,
				168,
				4
			]
		})
	] });
}
function Hit({ id, onPick, active, position, args }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position,
		onClick: onPick(id),
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
			color: ORANGE$1,
			transparent: true,
			opacity: active === id ? .28 : .02,
			depthWrite: false
		})]
	});
}
function Body({ args, position, color }) {
	const radius = Math.min(2, args[0] / 2 - .4, args[1] / 2 - .4, args[2] / 2 - .4);
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args,
		position,
		radius: Math.max(.6, radius),
		smoothness: 2,
		children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
			color,
			roughness: .48,
			metalness: .04
		})
	});
}
function FaceMark({ gear, args, position, panel }) {
	const [w, d, h] = args;
	const side = panel === "left" || panel === "right";
	const sign = panel === "left" ? -1 : 1;
	const lift = side ? [
		sign * (w / 2 + .6),
		0,
		0
	] : panel === "up" ? [
		0,
		0,
		h / 2 + .6
	] : [
		0,
		d / 2 + .6,
		0
	];
	const at = [
		position[0] + lift[0],
		position[1] + lift[1],
		position[2] + lift[2]
	];
	const faceW = side ? d : w;
	const faceH = side ? h : panel === "up" ? d : h;
	const screen = gear === "riddim" ? GREEN : gear === "ting" || gear === "pedal" ? ORANGE$1 : gear === "mixer" ? INK : BLACK$1;
	const screenArgs = side ? [
		1.2,
		faceW * .72,
		faceH * .22
	] : [
		faceW * .72,
		faceH * .22,
		1.2
	];
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", {
		position: at,
		rotation: panel === "up" || side ? [
			0,
			0,
			0
		] : [
			Math.PI / 2,
			0,
			0
		],
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: screenArgs }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
				color: screen,
				roughness: .4
			})] }),
			gear === "ko" || gear === "riddim" || gear === "mixer" ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: side ? [
					sign * .8,
					faceW * .28,
					faceH * .12
				] : [
					faceW * .28,
					faceH * .12,
					.4
				],
				rotation: side ? [
					0,
					0,
					Math.PI / 2
				] : [
					Math.PI / 2,
					0,
					0
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
					Math.min(8, faceW * .08),
					Math.min(8, faceW * .08),
					1.4,
					16
				] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: ORANGE$1 })]
			}) : null,
			gear === "pedal" ? [-1, 1].map((pad) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: side ? [
					sign * 5,
					pad * faceW * .22,
					0
				] : [
					pad * w * .22,
					0,
					4
				],
				rotation: side ? [
					0,
					0,
					Math.PI / 2
				] : [
					0,
					0,
					0
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
					Math.min(22, faceW * .28),
					Math.min(24, faceW * .3),
					8,
					20
				] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: BLACK$1 })]
			}, pad)) : null
		]
	});
}
function pinHeights(h) {
	const spots = [h * .38, h * .72].filter((z) => z > 18 && z < h - 14);
	return spots.length > 0 ? spots : [Math.max(20, h / 2)];
}
function Joint({ face, gear }) {
	const g = GEAR[gear];
	const heights = pinHeights(face === "top" ? 40 : g.h);
	if (face === "top") {
		const z = 274;
		return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", { children: [-40, 40].map((x) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Brick, {
			position: [
				x,
				0,
				z
			],
			args: [
				28,
				14,
				14
			]
		}, x)) });
	}
	if (face === "back") {
		const y = 93;
		return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", { children: heights.map((z) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Brick, {
			position: [
				0,
				y,
				z
			],
			args: [
				18,
				14,
				28
			]
		}, z)) });
	}
	const x = (face === "right" ? 1 : -1) * 119.5;
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", { children: heights.map((z) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Brick, {
		position: [
			x,
			0,
			z
		],
		args: [
			14,
			18,
			28
		]
	}, z)) });
}
function Brick({ position, args }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", {
		position,
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
			color: BRICK,
			roughness: .55
		})] }), /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
			args[0] * .35,
			args[1] * .35,
			args[2] * .55
		] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
			color: ORANGE$1,
			emissive: ORANGE$1,
			emissiveIntensity: .25
		})] })]
	});
}
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
			11.8
		] }]
	},
	{
		id: "platter",
		file: "platter.stl",
		color: BLACK,
		poses: [{ position: [
			44,
			148,
			15.3
		] }]
	},
	{
		id: "fader",
		file: "fader.stl",
		color: BLACK,
		poses: [{
			position: [
				44,
				58,
				24
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
			36,
			48,
			5.2
		] }]
	},
	{
		id: "knob",
		file: "knob.stl",
		color: ORANGE,
		poses: [{ position: [
			78,
			216,
			22
		] }]
	},
	{
		id: "button",
		file: "button.stl",
		color: BLACK,
		poses: [{
			position: [
				17,
				36,
				25
			],
			rotation: [
				Math.PI,
				0,
				0
			]
		}, {
			position: [
				17,
				60,
				25
			],
			rotation: [
				Math.PI,
				0,
				0
			]
		}]
	},
	{
		id: "loop",
		file: "loop.stl",
		color: BLACK,
		poses: [
			{
				position: [
					17,
					86,
					23.2
				],
				rotation: [
					Math.PI,
					0,
					0
				]
			},
			{
				position: [
					17,
					102,
					23.2
				],
				rotation: [
					Math.PI,
					0,
					0
				]
			},
			{
				position: [
					71,
					48,
					23.2
				],
				rotation: [
					Math.PI,
					0,
					0
				]
			}
		]
	},
	{
		id: "flange",
		file: "flange.stl",
		color: "#2a2a2c",
		poses: [
			{ position: [
				17,
				36,
				9.85
			] },
			{ position: [
				17,
				60,
				9.85
			] },
			{ position: [
				17,
				86,
				9.85
			] },
			{ position: [
				17,
				102,
				9.85
			] },
			{ position: [
				71,
				48,
				9.85
			] }
		]
	},
	{
		id: "plus",
		file: "plus.stl",
		color: ORANGE,
		poses: [{ position: [
			44,
			148,
			18.4
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
				72,
				16,
				-1.45
			] },
			{ position: [
				16,
				224,
				-1.45
			] },
			{ position: [
				72,
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
	const span = (0, import_react.useRef)(0);
	(0, import_react.useEffect)(() => {
		done.current = "";
	}, [watch]);
	useFrame((state) => {
		const group = ref.current;
		const ctl = state.controls;
		if (!group || !ctl) return;
		group.updateWorldMatrix(true, true);
		const box = new Box3().setFromObject(group);
		if (box.isEmpty()) return;
		const size = box.getSize(new Vector3());
		const next = size.x + size.y + size.z;
		if (done.current === watch && Math.abs(next - span.current) < 8) return;
		span.current = next;
		const center = box.getCenter(new Vector3());
		const wide = size.x > size.z * 1.3;
		const radius = Math.max(size.x, size.y, size.z) * (wide ? .52 : .62);
		const cam = state.camera;
		const dist = radius / Math.sin(cam.fov * Math.PI / 360);
		const dir = (wide ? new Vector3(.2, 1.15, .78) : new Vector3(.85, .72, .95)).normalize();
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
	const show = (id) => focus === "unit" || focus === "jog" || focus === "rig" || focus === id;
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", { children: PIECES.map((piece, index) => show(piece.id) ? piece.poses.map((pose, poseIndex) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)("mesh", {
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
	}, `${piece.id}-${poseIndex}`)) : null) });
}
function Scene({ mode, focus, slots, activeFace, watch, spin, layout, wideFocus, onReady, onPickFace }) {
	(0, import_react.useEffect)(() => {
		onReady();
	}, [onReady]);
	const jogFace = Object.keys(slots).find((id) => slots[id] === "jog");
	const jog = jogFace ? jogPose(jogFace) : null;
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
			children: /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", {
				rotation: [
					-Math.PI / 2,
					0,
					0
				],
				children: [
					mode === "unit" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(import_react.Suspense, {
						fallback: null,
						children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Assembly, { focus })
					}) : null,
					mode === "wide" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(WideDeck, {
						layout,
						focus: wideFocus
					}) : null,
					mode === "rig" && jog && jogFace ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(import_react.Suspense, {
						fallback: null,
						children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", {
							position: jog.position,
							quaternion: jog.quaternion,
							children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", {
								position: [
									-44,
									-120,
									-8
								],
								children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Assembly, { focus: "rig" })
							})
						})
					}) : null,
					mode === "rig" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Rig, {
						slots,
						face: activeFace,
						onPick: onPickFace
					}) : null
				]
			})
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(OrbitControls, {
			makeDefault: true,
			enableDamping: true,
			dampingFactor: .08,
			autoRotate: (mode === "unit" || mode === "wide") && spin,
			autoRotateSpeed: .45
		})
	] });
}
function UnitScene({ mode, focus, slots, activeFace, watch, spin, layout, wideFocus, onReady, onInteract, onPickFace }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Canvas, {
		dpr: [1, 1.75],
		camera: {
			fov: 32,
			position: [
				280,
				240,
				320
			],
			near: .1,
			far: 8e3
		},
		gl: {
			antialias: true,
			alpha: false
		},
		onPointerDown: onInteract,
		onCreated: ({ camera }) => {
			camera.lookAt(0, 90, 0);
			onReady();
		},
		children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Scene, {
			mode,
			focus,
			slots,
			activeFace,
			watch,
			spin,
			layout,
			wideFocus,
			onReady,
			onPickFace
		})
	});
}
//#endregion
export { UnitScene };
