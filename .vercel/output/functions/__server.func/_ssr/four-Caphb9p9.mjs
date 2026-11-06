import { i as __toESM } from "../_runtime.mjs";
import { g as Vector3, i as useFrame, n as OrbitControls, o as Box3, r as Canvas, t as RoundedBox, v as require_jsx_runtime, y as require_react } from "../_libs/@react-three/drei+[...].mjs";
import { y as Link } from "../_libs/@tanstack/react-router+[...].mjs";
//#region node_modules/.nitro/vite/services/ssr/assets/four-Caphb9p9.js
var import_react = /* @__PURE__ */ __toESM(require_react());
var import_jsx_runtime = require_jsx_runtime();
var TRAY = {
	w: 176,
	h: 240.5,
	t: 22,
	seat: 19.8
};
var TILES = [
	{
		id: "faders-12",
		label: "faders 12",
		x0: 2.6,
		y0: 2.6,
		x1: 72.75,
		y1: 145.75,
		holes: [
			{
				kind: "slot",
				x0: 26.2,
				y0: 41,
				x1: 29.8,
				y1: 115
			},
			{
				kind: "round",
				x0: 23.8,
				y0: 123.8,
				x1: 32.2,
				y1: 132.2
			},
			{
				kind: "slot",
				x0: 56.2,
				y0: 41,
				x1: 59.8,
				y1: 115
			},
			{
				kind: "round",
				x0: 53.8,
				y0: 123.8,
				x1: 62.2,
				y1: 132.2
			}
		]
	},
	{
		id: "faders-34",
		label: "faders 34",
		x0: 73.25,
		y0: 2.6,
		x1: 136.75,
		y1: 145.75,
		holes: [
			{
				kind: "slot",
				x0: 86.2,
				y0: 41,
				x1: 89.8,
				y1: 115
			},
			{
				kind: "round",
				x0: 83.8,
				y0: 123.8,
				x1: 92.2,
				y1: 132.2
			},
			{
				kind: "slot",
				x0: 116.2,
				y0: 41,
				x1: 119.8,
				y1: 115
			},
			{
				kind: "round",
				x0: 113.8,
				y0: 123.8,
				x1: 122.2,
				y1: 132.2
			}
		]
	},
	{
		id: "master",
		label: "master",
		x0: 137.25,
		y0: 2.6,
		x1: 173.4,
		y1: 145.75,
		holes: [{
			kind: "slot",
			x0: 154.2,
			y0: 41,
			x1: 157.8,
			y1: 115
		}, {
			kind: "round",
			x0: 152,
			y0: 124,
			x1: 160,
			y1: 132
		}]
	},
	{
		id: "transport-l",
		label: "transport l",
		x0: 2.6,
		y0: 146.25,
		x1: 82.75,
		y1: 165.25,
		holes: [
			{
				kind: "rect",
				x0: 21.5,
				y0: 152,
				x1: 34.5,
				y1: 160
			},
			{
				kind: "rect",
				x0: 43.5,
				y0: 152,
				x1: 56.5,
				y1: 160
			},
			{
				kind: "rect",
				x0: 65.5,
				y0: 152,
				x1: 78.5,
				y1: 160
			}
		]
	},
	{
		id: "transport-r",
		label: "transport r",
		x0: 83.25,
		y0: 146.25,
		x1: 173.4,
		y1: 165.25,
		holes: [
			{
				kind: "rect",
				x0: 87.5,
				y0: 152,
				x1: 100.5,
				y1: 160
			},
			{
				kind: "rect",
				x0: 109.5,
				y0: 152,
				x1: 122.5,
				y1: 160
			},
			{
				kind: "rect",
				x0: 131.5,
				y0: 152,
				x1: 144.5,
				y1: 160
			}
		]
	},
	{
		id: "screen",
		label: "screen",
		x0: 2.6,
		y0: 165.75,
		x1: 133.75,
		y1: 237.9,
		holes: [{
			kind: "rect",
			x0: 56,
			y0: 178.5,
			x1: 120,
			y1: 225.5
		}]
	},
	{
		id: "ear-r",
		label: "ear r",
		x0: 134.25,
		y0: 165.75,
		x1: 173.4,
		y1: 237.9,
		holes: []
	}
];
var W = 176;
var H = 240.5;
var T = 24;
var GREY = "#d4d4ce";
var SLOT = "#1a1a1c";
var ORANGE = "#f05a1a";
var CAP = "#c8c4bc";
var GLASS = "#101418";
function mat(color, roughness = .5) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
		color,
		roughness,
		metalness: .04
	});
}
function Shell() {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			W,
			H,
			T
		],
		radius: 3.2,
		smoothness: 3,
		position: [
			W / 2,
			H / 2,
			T / 2
		],
		children: mat(GREY, .58)
	}), [
		16,
		25,
		48,
		216,
		224
	].map((y) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position: [
			-.4,
			y,
			8
		],
		rotation: [
			0,
			0,
			Math.PI / 2
		],
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
			2.2,
			2.2,
			2.4,
			16
		] }), mat(SLOT, .6)]
	}), /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position: [
			176.4,
			y,
			8
		],
		rotation: [
			0,
			0,
			Math.PI / 2
		],
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
			2.2,
			2.2,
			2.4,
			16
		] }), mat(SLOT, .6)]
	})] }, y))] });
}
function Jack({ x, y }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position: [
			x,
			y,
			11
		],
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
			2.4,
			2.4,
			5,
			16
		] }), mat(SLOT, .5)]
	});
}
function Fader({ x, master = false }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			3.6,
			78,
			1.2
		],
		radius: .5,
		smoothness: 2,
		position: [
			x,
			76,
			24.6
		],
		children: mat(SLOT, .6)
	}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			14,
			11,
			8
		],
		radius: 1.1,
		smoothness: 2,
		position: [
			x,
			master ? 96 : 70,
			28.4
		],
		children: mat(master ? ORANGE : CAP, .4)
	})] });
}
function Key({ x, y, rec = false }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			16,
			12,
			3.4
		],
		radius: 1,
		smoothness: 2,
		position: [
			x,
			y,
			25.8
		],
		children: mat(rec ? "#2a2a2c" : "#b7b7b0", .45)
	}), rec ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position: [
			x,
			y + 7.5,
			24.5
		],
		rotation: [
			Math.PI / 2,
			0,
			0
		],
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
			1.3,
			1.3,
			.8,
			14
		] }), mat(ORANGE, .3)]
	}) : null] });
}
function Screen() {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", {
		position: [
			88,
			196,
			24.4
		],
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
				args: [
					78,
					58,
					1.4
				],
				radius: 1.2,
				smoothness: 2,
				children: mat("#0c0c0e", .4)
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					0,
					0,
					.8
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					66,
					49,
					.4
				] }), mat(GLASS, .25)]
			}),
			[
				0,
				1,
				2,
				3
			].map((i) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					-8 + i % 2 * 18,
					14 - i * 10,
					1.1
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					28 - i * 3,
					3.2,
					.3
				] }), mat(i === 0 ? "#e6c15a" : i === 3 ? ORANGE : "#8a8a84", .4)]
			}, i)),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					0,
					-20,
					1.1
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					50,
					1.2,
					.3
				] }), mat("#3d6fd4", .35)]
			})
		]
	});
}
function PortaDeck() {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Shell, {}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Screen, {}),
		[
			28,
			50,
			72,
			94,
			116,
			138
		].map((x, i) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Key, {
			x,
			y: 156,
			rec: i === 0
		}, x)),
		[
			28,
			58,
			88,
			118
		].map((x) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Fader, { x }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Key, {
			x,
			y: 128,
			rec: true
		})] }, x)),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Fader, {
			x: 156,
			master: true
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
			position: [
				156,
				128,
				28
			],
			rotation: [
				Math.PI / 2,
				0,
				0
			],
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
				6,
				7,
				8,
				24
			] }), mat(ORANGE, .4)]
		}),
		[
			18,
			38,
			58,
			78,
			98,
			118,
			138,
			158
		].map((x) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Jack, {
			x,
			y: 239.5
		}, x)),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Jack, {
			x: 36,
			y: 2
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Jack, {
			x: 88,
			y: 2
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Jack, {
			x: 140,
			y: 2
		})
	] });
}
var RESIN = "#e6e2d8";
var TRAY_COLOR = "#b7b7b0";
var CUT = "#141416";
function Fit({ watch, children }) {
	const ref = (0, import_react.useRef)(null);
	const done = (0, import_react.useRef)("");
	(0, import_react.useEffect)(() => {
		done.current = "";
	}, [watch]);
	useFrame((state) => {
		const group = ref.current;
		const ctl = state.controls;
		if (!group || !ctl || done.current === watch) return;
		group.updateWorldMatrix(true, true);
		const box = new Box3().setFromObject(group);
		if (box.isEmpty()) return;
		const size = box.getSize(new Vector3());
		const center = box.getCenter(new Vector3());
		const radius = Math.max(size.x, size.y, size.z) * .62;
		const cam = state.camera;
		const dist = radius / Math.sin(cam.fov * Math.PI / 360);
		const dir = new Vector3(.85, .72, .95).normalize();
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
function Kit({ explode }) {
	const lift = explode ? 22 : 0;
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			TRAY.w,
			TRAY.h,
			TRAY.t
		],
		radius: 2.4,
		smoothness: 2,
		position: [
			TRAY.w / 2,
			TRAY.h / 2,
			TRAY.t / 2
		],
		children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
			color: TRAY_COLOR,
			roughness: .62,
			metalness: .04
		})
	}), TILES.map((tile) => {
		const w = tile.x1 - tile.x0;
		const h = tile.y1 - tile.y0;
		return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", {
			position: [
				0,
				0,
				lift
			],
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					(tile.x0 + tile.x1) / 2,
					(tile.y0 + tile.y1) / 2,
					TRAY.seat + 1.1
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					w,
					h,
					2.2
				] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
					color: tile.id === "screen" ? "#d5d0c6" : RESIN,
					roughness: .4
				})]
			}), tile.holes.map((hole, index) => hole.kind === "round" ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					(hole.x0 + hole.x1) / 2,
					(hole.y0 + hole.y1) / 2,
					TRAY.seat + 2.3
				],
				rotation: [
					Math.PI / 2,
					0,
					0
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
					(hole.x1 - hole.x0) / 2,
					(hole.x1 - hole.x0) / 2,
					.6,
					20
				] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
					color: CUT,
					roughness: .5
				})]
			}, index) : /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					(hole.x0 + hole.x1) / 2,
					(hole.y0 + hole.y1) / 2,
					TRAY.seat + 2.3
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					hole.x1 - hole.x0,
					hole.y1 - hole.y0,
					.6
				] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
					color: CUT,
					roughness: .5
				})]
			}, index))]
		}, tile.id);
	})] });
}
function FourScene({ view = "unit" }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(Canvas, {
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
		children: [
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
				watch: view,
				children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("group", {
					rotation: [
						-Math.PI / 2,
						0,
						0
					],
					children: view === "pieces" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Kit, { explode: true }) : /* @__PURE__ */ (0, import_jsx_runtime.jsx)(PortaDeck, {})
				})
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(OrbitControls, {
				makeDefault: true,
				enableDamping: true,
				dampingFactor: .08,
				autoRotate: true,
				autoRotateSpeed: .45
			})
		]
	});
}
var SMALL = [
	["caps-plate.stl", "Caps and keys, one plate"],
	["cap.stl", "Fader cap ×4"],
	["cap-master.stl", "Master cap, orange"],
	["knob.stl", "FX knob, orange"],
	["key.stl", "Transport key ×6"],
	["rec.stl", "Record button ×4"]
];
function FourPage() {
	const [view, setView] = (0, import_react.useState)("pieces");
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("main", {
		className: "mx-auto flex min-h-screen max-w-lg flex-col gap-8 px-5 py-8",
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("header", {
				className: "flex flex-col gap-3",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Link, {
						to: "/",
						className: "font-mono text-xs tracking-widest text-accent uppercase",
						children: "Field Jog"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h1", {
						className: "text-4xl leading-none font-semibold tracking-tight",
						children: "Four"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-base leading-relaxed text-muted",
						children: "A four-track recorder in the same brick as the K.O. II. The face follows a Portastudio: four channel faders, a master, a transport row, and a 3.2 inch screen where the cassette window was."
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "h-viewer w-full touch-none overflow-hidden border border-line bg-surface",
				children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(FourScene, { view })
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "flex gap-2",
				children: [["unit", "Unit"], ["pieces", "Pieces"]].map(([id, label]) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
					type: "button",
					"aria-pressed": view === id,
					onClick: () => setView(id),
					className: view === id ? "h-11 flex-1 bg-fg font-mono text-xs tracking-wide text-bg uppercase" : "h-11 flex-1 border border-line font-mono text-xs tracking-wide text-fg uppercase",
					children: label
				}, id))
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
				className: "text-sm text-muted",
				children: view === "pieces" ? "The tray is one FDM print. The face tiles lift off. Each of them fits the 153.36 × 77.76 mm plate." : "Drag to turn. Pinch to zoom."
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("dl", {
				className: "grid grid-cols-3 divide-x divide-line border border-line",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Height",
						v: "240 mm"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Width",
						v: "176 mm"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Thick",
						v: "22 mm"
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
				className: "flex flex-col gap-3 text-sm leading-relaxed",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
						className: "font-mono text-xs tracking-widest text-muted uppercase",
						children: "Resin plate"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-muted",
						children: "The plate is 153.36 × 77.76 × 165 mm. Every resin part is kept inside 150.9 × 75.3, so there is 1.2 mm of clearance on each side. Height is not the limit. Lay the long side of a tile along the 153 mm axis. The wide fader tile is 70 × 143 mm. The screen tile, with the left ear attached, is 131 × 72 mm."
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-muted",
						children: "The tiles drop into a 2.2 mm rabbet in the tray. A drop of CA at the corners holds them. The seams are 0.5 mm, and a rib in the tray sits under each one. Tracks 1–2 share a tile, and so do tracks 3–4. Caps, keys, and the knob share one more plate."
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("ul", {
						className: "divide-y divide-line border border-line",
						children: [
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Row, {
								file: "tray.stl",
								name: "Tray",
								note: "FDM · 176 × 240"
							}),
							TILES.map((tile) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Row, {
								file: `${tile.id}.stl`,
								name: tile.label,
								note: `Resin · ${Math.round(tile.x1 - tile.x0)} × ${Math.round(tile.y1 - tile.y0)}`
							}, tile.id)),
							SMALL.map(([file, name]) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Row, {
								file,
								name,
								note: "Resin"
							}, file))
						]
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
						className: "text-fg underline",
						href: "/field-jog/four/four.scad",
						target: "_blank",
						rel: "noreferrer",
						children: "SCAD"
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
				className: "text-sm leading-relaxed text-muted",
				children: "Pin holes match the rest of the row: 16, 25, 48, 216 and 224 mm from the fader end, 8 mm off the table, on both long sides."
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
				className: "flex flex-col gap-2 text-sm leading-relaxed",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
						className: "font-mono text-xs tracking-widest text-muted uppercase",
						children: "Jacks"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-muted",
						children: "All 3.5 mm, cut in the FDM tray."
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("ul", {
						className: "list-disc pl-5 text-muted",
						children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("li", { children: "Front: headphones, instrument, mic." }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("li", { children: "Back: MIDI in, MIDI out, stereo line in, stereo line out, tracks 1 to 4." })]
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
				className: "flex flex-col gap-2 text-sm leading-relaxed",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
						className: "font-mono text-xs tracking-widest text-muted uppercase",
						children: "Brain"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-muted",
						children: "A Daisy Seed. Its onboard codec is stereo, and this box needs four inputs and six outputs, so a PCM3168A sits beside it. Teensy 4.1 is the alternate if USB-host MIDI matters more than the audio library. A Bela Mini stays stereo, so it does not fit."
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("ul", {
						className: "list-disc pl-5 text-muted",
						children: [
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("li", { children: "MIDI clock is TRS, plus USB MIDI from the Seed." }),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("li", { children: "The screen is a 3.2 inch SPI panel, 320×240, with touch." }),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("li", { children: "FX is a send inside the mix. The orange knob above the master is that send." })
						]
					})
				]
			})
		]
	});
}
function Row({ file, name, note }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("li", {
		className: "flex items-center justify-between gap-3 px-3 py-3",
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("span", {
			className: "flex flex-col",
			children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("span", {
				className: "text-sm capitalize",
				children: name
			}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("span", {
				className: "font-mono text-xs text-muted",
				children: note
			})]
		}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
			href: `/field-jog/four/${file}`,
			target: "_blank",
			rel: "noreferrer",
			className: "font-mono text-xs tracking-wide text-accent uppercase",
			children: "STL"
		})]
	});
}
function Spec({ k, v }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", {
		className: "flex flex-col gap-1 px-3 py-3",
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("dt", {
			className: "font-mono text-xs tracking-widest text-muted uppercase",
			children: k
		}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("dd", {
			className: "text-lg font-semibold",
			children: v
		})]
	});
}
//#endregion
export { FourPage as component };
