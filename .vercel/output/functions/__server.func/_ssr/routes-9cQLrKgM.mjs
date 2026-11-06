import { i as __toESM } from "../_runtime.mjs";
import { t as RoundedBox, v as require_jsx_runtime, y as require_react } from "../_libs/@react-three/drei+[...].mjs";
import { y as Link } from "../_libs/@tanstack/react-router+[...].mjs";
//#region node_modules/.nitro/vite/services/ssr/assets/routes-9cQLrKgM.js
var import_react = /* @__PURE__ */ __toESM(require_react());
var import_jsx_runtime = require_jsx_runtime();
var __defProp = Object.defineProperty;
var __exportAll = (all, no_symbols) => {
	let target = {};
	for (var name in all) __defProp(target, name, {
		get: all[name],
		enumerable: true
	});
	if (!no_symbols) __defProp(target, Symbol.toStringTag, { value: "Module" });
	return target;
};
var W = 176;
var H = 240.5;
var T = 16.3;
var GREY = "#d4d4ce";
var BLACK = "#141416";
var WELL = "#2a2a28";
var ORANGE = "#f05a1a";
var SLOT = "#1a1a1c";
var WIDE_PARTS = [
	{
		id: "unit",
		label: "Unit"
	},
	{
		id: "case",
		label: "Case"
	},
	{
		id: "platter",
		label: "Platter"
	},
	{
		id: "fader",
		label: "Fader"
	},
	{
		id: "pads",
		label: "Pads"
	},
	{
		id: "knob",
		label: "Knob"
	}
];
function show(focus, part) {
	return focus === "unit" || focus === part;
}
function mat(color, roughness = .5) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", {
		color,
		roughness,
		metalness: .04
	});
}
function Platter({ x, y, d }) {
	const rings = d > 100 ? [
		34,
		50,
		64
	] : [
		16,
		24,
		31
	];
	const arm = d > 100 ? 22 : 12;
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", {
		position: [
			x,
			y,
			T
		],
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					0,
					0,
					.2
				],
				rotation: [
					Math.PI / 2,
					0,
					0
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
					d / 2 + .5,
					d / 2 + .5,
					.35,
					72
				] }), mat(WELL, .7)]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					0,
					0,
					2
				],
				rotation: [
					Math.PI / 2,
					0,
					0
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
					d / 2 - .4,
					d / 2 - .4,
					3.2,
					72
				] }), mat(BLACK, .42)]
			}),
			rings.map((r) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					0,
					0,
					3.35
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("torusGeometry", { args: [
					r,
					.32,
					6,
					48
				] }), mat("#3a3a3c", .6)]
			}, r)),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					0,
					0,
					3.5
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					arm,
					2.2,
					.5
				] }), mat(ORANGE, .4)]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					0,
					0,
					3.5
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					2.2,
					arm,
					.5
				] }), mat(ORANGE, .4)]
			})
		]
	});
}
function Cap({ x, y, d, h = 4, color = BLACK }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position: [
			x,
			y,
			T + h / 2
		],
		rotation: [
			Math.PI / 2,
			0,
			0
		],
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
			d / 2,
			d / 2,
			h,
			28
		] }), mat(color, .45)]
	});
}
function Led({ x, y }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position: [
			x,
			y,
			16.8
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
			16
		] }), mat(ORANGE, .3)]
	});
}
function Pitch({ x, y0, y1 }) {
	const mid = (y0 + y1) / 2;
	const len = y1 - y0;
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			3.4,
			len,
			1.1
		],
		radius: .5,
		smoothness: 2,
		position: [
			x,
			mid,
			16.85
		],
		children: mat(SLOT, .6)
	}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			12,
			16,
			5
		],
		radius: 1,
		smoothness: 2,
		position: [
			x,
			y0 + len * .62,
			19.5
		],
		children: mat("#c4c4be", .4)
	})] });
}
function Crossfader() {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			88,
			3.4,
			1.1
		],
		radius: .5,
		smoothness: 2,
		position: [
			88,
			16,
			16.85
		],
		children: mat(SLOT, .6)
	}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
		args: [
			18,
			12,
			6
		],
		radius: 1,
		smoothness: 2,
		position: [
			88,
			16,
			19.900000000000002
		],
		children: mat(BLACK, .4)
	})] });
}
function Screen({ x, y, w }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", {
		position: [
			x,
			y,
			17
		],
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
				args: [
					w,
					14,
					1.1
				],
				radius: .6,
				smoothness: 2,
				children: mat("#0c0c0e", .35)
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					-w * .18,
					1.6,
					.6
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					w * .5,
					3.2,
					.3
				] }), mat("#e6c15a", .4)]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
				position: [
					-w * .18,
					-2.4,
					.6
				],
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
					w * .5,
					3.2,
					.3
				] }), mat("#3d6fd4", .4)]
			})
		]
	});
}
function Jacks() {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position: [
			W / 2,
			238.3,
			8
		],
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("boxGeometry", { args: [
			12.5,
			4.4,
			3.4
		] }), mat(SLOT, .5)]
	}), [
		52,
		68,
		108,
		124
	].map((x) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("mesh", {
		position: [
			x,
			238.3,
			8
		],
		children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("cylinderGeometry", { args: [
			2.3,
			2.3,
			4.4,
			16
		] }), mat(SLOT, .5)]
	}, x))] });
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
function Twin({ focus }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [
		[{
			x: 46,
			cue: 40,
			play: 66,
			inn: 40,
			out: 66,
			pads: [
				36,
				50,
				64,
				78
			],
			pitch: 18
		}, {
			x: 130,
			cue: 136,
			play: 110,
			inn: 136,
			out: 110,
			pads: [
				98,
				112,
				126,
				140
			],
			pitch: 158
		}].map((d) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [
			show(focus, "platter") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Platter, {
				x: d.x,
				y: 158,
				d: 76
			}) : null,
			show(focus, "pads") ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(import_jsx_runtime.Fragment, { children: [
				/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
					x: d.cue,
					y: 40,
					d: 18,
					h: 5
				}),
				/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
					x: d.play,
					y: 40,
					d: 18,
					h: 5,
					color: "#1c1c1e"
				}),
				/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
					x: d.inn,
					y: 76,
					d: 12,
					h: 3.2
				}),
				/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
					x: d.out,
					y: 76,
					d: 12,
					h: 3.2
				}),
				/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Led, {
					x: d.inn,
					y: 88
				}),
				/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Led, {
					x: d.out,
					y: 88
				}),
				d.pads.map((x) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
					args: [
						11,
						11,
						2.2
					],
					radius: .8,
					smoothness: 2,
					position: [
						x,
						100,
						17.400000000000002
					],
					children: mat("#2c2c2e", .5)
				}, x))
			] }) : null,
			show(focus, "fader") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Pitch, {
				x: d.pitch,
				y0: 24,
				y1: 108
			}) : null
		] }, d.x)),
		show(focus, "fader") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Crossfader, {}) : null,
		show(focus, "case") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Screen, {
			x: 88,
			y: 220,
			w: 78
		}) : null,
		show(focus, "knob") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
			x: 158,
			y: 218,
			d: 16,
			h: 8,
			color: ORANGE
		}) : null
	] });
}
function Six({ focus }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [
		show(focus, "platter") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Platter, {
			x: 88,
			y: 142,
			d: 152.4
		}) : null,
		show(focus, "pads") ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(import_jsx_runtime.Fragment, { children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
				x: 28,
				y: 30,
				d: 20,
				h: 5.5
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
				x: 54,
				y: 30,
				d: 20,
				h: 5.5
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
				x: 28,
				y: 52,
				d: 12,
				h: 3.2,
				color: "#2c2c2e"
			}),
			[
				100,
				116,
				132,
				148
			].map((x) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(RoundedBox, {
				args: [
					12,
					12,
					2.2
				],
				radius: .8,
				smoothness: 2,
				position: [
					x,
					36,
					17.400000000000002
				],
				children: mat("#2c2c2e", .5)
			}, x)),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
				x: 22,
				y: 214,
				d: 12,
				h: 3.2
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
				x: 22,
				y: 196,
				d: 12,
				h: 3.2
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Led, {
				x: 34,
				y: 214
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Led, {
				x: 34,
				y: 196
			})
		] }) : null,
		show(focus, "fader") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Pitch, {
			x: 166,
			y0: 12,
			y1: 60
		}) : null,
		show(focus, "case") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Screen, {
			x: 78,
			y: 228,
			w: 64
		}) : null,
		show(focus, "knob") ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
			x: 150,
			y: 226,
			d: 14,
			h: 7,
			color: ORANGE
		}) : null
	] });
}
function WideDeck({ layout, focus = "unit" }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [show(focus, "case") ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(import_jsx_runtime.Fragment, { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Shell, {}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Jacks, {})] }) : null, layout === "twin" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Twin, { focus }) : /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Six, { focus })] });
}
var VIEW_ITEMS = [
	{
		id: "unit",
		label: "Unit"
	},
	{
		id: "bottom",
		label: "Tray"
	},
	{
		id: "top",
		label: "Deck"
	},
	{
		id: "platter",
		label: "Platter"
	},
	{
		id: "fader",
		label: "Fader"
	},
	{
		id: "lock",
		label: "Lock"
	},
	{
		id: "knob",
		label: "Knob"
	},
	{
		id: "button",
		label: "Cue / Play"
	},
	{
		id: "loop",
		label: "Loop"
	},
	{
		id: "flange",
		label: "Clips"
	},
	{
		id: "plus",
		label: "Plus"
	},
	{
		id: "foot",
		label: "Feet"
	}
];
var FACES = [
	{
		id: "left",
		label: "Left"
	},
	{
		id: "right",
		label: "Right"
	},
	{
		id: "back",
		label: "Back"
	},
	{
		id: "top",
		label: "Top"
	}
];
var GEAR_CHOICES = [
	{
		id: "none",
		label: "None"
	},
	{
		id: "jog",
		label: "Jog"
	},
	{
		id: "ko",
		label: "K.O. II"
	},
	{
		id: "riddim",
		label: "Riddim"
	},
	{
		id: "mixer",
		label: "Mixer"
	},
	{
		id: "pedal",
		label: "Pedal"
	},
	{
		id: "ting",
		label: "Ting"
	}
];
var EMPTY_SLOTS = {
	left: null,
	right: "riddim",
	back: null,
	top: null
};
function slotKey(slots) {
	return FACES.map((face) => slots[face.id] ?? "-").join(".");
}
function ViewerPanel() {
	const [mode, setMode] = (0, import_react.useState)("rig");
	const [unitFocus, setUnitFocus] = (0, import_react.useState)("unit");
	const [layout, setLayout] = (0, import_react.useState)("twin");
	const [wideFocus, setWideFocus] = (0, import_react.useState)("unit");
	const [slots, setSlots] = (0, import_react.useState)(EMPTY_SLOTS);
	const [activeFace, setActiveFace] = (0, import_react.useState)("right");
	const [spin, setSpin] = (0, import_react.useState)(true);
	const [fitTick, setFitTick] = (0, import_react.useState)(0);
	const [ready, setReady] = (0, import_react.useState)(false);
	const [Scene, setScene] = (0, import_react.useState)(null);
	(0, import_react.useEffect)(() => {
		let live = true;
		import("./unit-scene-DlyegU0V.mjs").then((mod) => {
			if (live) setScene(() => mod.UnitScene);
		});
		return () => {
			live = false;
		};
	}, []);
	const onReady = (0, import_react.useCallback)(() => setReady(true), []);
	const onInteract = (0, import_react.useCallback)(() => setSpin(false), []);
	const onPickFace = (0, import_react.useCallback)((face) => setActiveFace(face), []);
	function assign(next) {
		setSlots((prev) => {
			const slotsNext = {
				...prev,
				[activeFace]: next
			};
			if (next) {
				for (const face of FACES) if (face.id !== activeFace && slotsNext[face.id] === next) slotsNext[face.id] = null;
			}
			return slotsNext;
		});
		setFitTick((n) => n + 1);
	}
	const faceLabel = FACES.find((face) => face.id === activeFace)?.label ?? "Right";
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
		className: "flex flex-col gap-3",
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", {
				className: "relative h-viewer w-full touch-none overflow-hidden border border-line bg-surface",
				children: [Scene ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Scene, {
					mode,
					focus: unitFocus,
					slots,
					activeFace,
					watch: `${mode}:${unitFocus}:${wideFocus}:${layout}:${slotKey(slots)}:${fitTick}`,
					spin,
					layout,
					wideFocus,
					onReady,
					onInteract,
					onPickFace
				}) : null, ready ? null : /* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
					className: "pointer-events-none absolute inset-0 flex items-center justify-center text-sm text-muted",
					children: "Loading the model"
				})]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", {
				className: "flex items-center justify-between gap-3",
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
					className: "min-w-0 text-sm text-muted",
					children: mode === "wide" ? wideFocus === "unit" ? layout === "twin" ? "176 × 240.5. Two 76 mm platters, a pitch fader each, hot cues, and one crossfader." : "176 × 240.5. One 152 mm platter. Cue, play, loop and the tempo fader sit in the space around it." : `${WIDE_PARTS.find((part) => part.id === wideFocus)?.label ?? "Part"}. The rest of the deck is hidden.` : mode === "rig" ? `${faceLabel}. Pick what snaps on.` : "Drag to turn. Pinch to zoom."
				}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
					type: "button",
					className: "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase",
					onClick: () => {
						setSpin(true);
						setFitTick((n) => n + 1);
					},
					children: "Reset"
				})]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "flex gap-2",
				children: [
					["rig", "Setup"],
					["unit", "Parts"],
					["wide", "Wide"]
				].map(([item, label]) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
					type: "button",
					"aria-pressed": mode === item,
					onClick: () => setMode(item),
					className: mode === item ? "h-11 flex-1 bg-fg font-mono text-xs tracking-wide text-bg uppercase" : "h-11 flex-1 border border-line font-mono text-xs tracking-wide text-fg uppercase",
					children: label
				}, item))
			}),
			mode === "wide" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "flex gap-2",
				children: [["twin", "Twin 3"], ["six", "One 6"]].map(([item, label]) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
					type: "button",
					"aria-pressed": layout === item,
					onClick: () => {
						setLayout(item);
						setSpin(true);
						setFitTick((n) => n + 1);
					},
					className: layout === item ? "h-11 flex-1 bg-accent font-mono text-xs tracking-wide text-accent-fg uppercase" : "h-11 flex-1 border border-line font-mono text-xs tracking-wide text-fg uppercase",
					children: label
				}, item))
			}) : null,
			mode === "rig" ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(import_jsx_runtime.Fragment, { children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "grid grid-cols-4 gap-2",
				children: FACES.map((item) => {
					const selected = item.id === activeFace;
					const filled = slots[item.id] != null;
					return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("button", {
						type: "button",
						"aria-pressed": selected,
						onClick: () => setActiveFace(item.id),
						className: selected ? "h-11 bg-accent font-mono text-xs tracking-wide text-accent-fg uppercase" : "h-11 border border-line font-mono text-xs tracking-wide text-fg uppercase",
						children: [item.label, filled ? " ·" : ""]
					}, item.id);
				})
			}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "flex gap-2 overflow-x-auto pb-1",
				children: GEAR_CHOICES.map((item) => {
					const selected = item.id === "none" ? slots[activeFace] == null : slots[activeFace] === item.id;
					return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
						type: "button",
						"aria-pressed": selected,
						onClick: () => assign(item.id === "none" ? null : item.id),
						className: selected ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase" : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase",
						children: item.label
					}, item.id);
				})
			})] }) : mode === "wide" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "flex gap-2 overflow-x-auto pb-1",
				children: WIDE_PARTS.map((item) => {
					const selected = item.id === wideFocus;
					return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
						type: "button",
						"aria-pressed": selected,
						onClick: () => {
							setWideFocus(item.id);
							setSpin(true);
							setFitTick((n) => n + 1);
						},
						className: selected ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase" : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase",
						children: item.label
					}, item.id);
				})
			}) : /* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "flex gap-2 overflow-x-auto pb-1",
				children: VIEW_ITEMS.map((item) => {
					const selected = item.id === unitFocus;
					return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
						type: "button",
						"aria-pressed": selected,
						onClick: () => setUnitFocus(item.id),
						className: selected ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase" : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase",
						children: item.label
					}, item.id);
				})
			})
		]
	});
}
var RENDERS = [
	{
		src: "/field-jog/renders/dark.jpg",
		alt: "Scratch deck on a dark field, platter and orange knob"
	},
	{
		src: "/field-jog/renders/top.jpg",
		alt: "Top view of the scratch deck with cue, fader, and rev"
	},
	{
		src: "/field-jog/renders/angle.jpg",
		alt: "Three-quarter view of the grey scratch deck"
	},
	{
		src: "/field-jog/renders/three.jpg",
		alt: "Angled view showing the side pin holes and MIDI label"
	},
	{
		src: "/field-jog/renders/detail.jpg",
		alt: "Close view of the platter, orange plus, and knurled knob"
	},
	{
		src: "/field-jog/renders/side.jpg",
		alt: "Side profile of the deck, platter, and fader cap"
	},
	{
		src: "/field-jog/renders/pair.jpg",
		alt: "Scratch deck beside a K.O. II Sidekick mixer"
	}
];
var WIDE_RENDERS = [
	{
		src: "/field-jog/renders/wide/twin.jpg",
		alt: "Twin deck, two platters, pads, and three faders"
	},
	{
		src: "/field-jog/renders/wide/six.jpg",
		alt: "Single platter deck, pads along the top, orange knob"
	},
	{
		src: "/field-jog/renders/wide/six-front.jpg",
		alt: "Single platter deck, cue pads and pitch fader along the front"
	},
	{
		src: "/field-jog/renders/wide/underside.jpg",
		alt: "Underside with the speaker grille, USB-C, and card slot"
	}
];
function RenderGallery({ title = "Renders", renders = RENDERS }) {
	const [index, setIndex] = (0, import_react.useState)(0);
	const shot = renders[index] ?? renders[0];
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
		className: "flex flex-col gap-3",
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
				className: "font-mono text-xs tracking-widest text-muted uppercase",
				children: title
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("img", {
				src: shot.src,
				alt: shot.alt,
				className: "w-full border border-line bg-fg"
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", {
				className: "flex gap-2 overflow-x-auto pb-1",
				children: renders.map((item, i) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
					type: "button",
					"aria-pressed": i === index,
					"aria-label": item.alt,
					onClick: () => setIndex(i),
					className: `h-16 w-16 shrink-0 border ${i === index ? "border-accent" : "border-line"}`,
					children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("img", {
						src: item.src,
						alt: "",
						className: "h-full w-full object-cover"
					})
				}, item.src))
			})
		]
	});
}
var routes_exports = /* @__PURE__ */ __exportAll({ component: () => Home });
var PARTS = [
	[
		"top.stl",
		"Deck",
		"Grey"
	],
	[
		"bottom.stl",
		"Tray",
		"Grey"
	],
	[
		"platter.stl",
		"Platter",
		"Black"
	],
	[
		"fader.stl",
		"Fader cap",
		"Black"
	],
	[
		"fader_lock.stl",
		"Fader lock",
		"Black"
	],
	[
		"knob.stl",
		"Knob",
		"Orange"
	],
	[
		"button.stl",
		"Button ×2",
		"Black"
	],
	[
		"loop.stl",
		"Loop button ×3",
		"Black"
	],
	[
		"flange.stl",
		"Button clip ×5",
		"Black"
	],
	[
		"plus.stl",
		"Platter mark",
		"Orange"
	],
	[
		"tab.stl",
		"USB tab",
		"Orange"
	],
	[
		"bar.stl",
		"Name bar",
		"Orange"
	],
	[
		"foot.stl",
		"Foot ×4",
		"TPU"
	]
];
function Home() {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("main", {
		className: "mx-auto flex min-h-screen max-w-lg flex-col gap-8 px-5 py-8",
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("header", {
				className: "flex flex-col gap-3",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "font-mono text-xs tracking-widest text-accent uppercase",
						children: "Unofficial companion"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h1", {
						className: "text-4xl leading-none font-semibold tracking-tight",
						children: "Field Jog"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-base leading-relaxed text-muted",
						children: "A scratch-deck shell in the Sidekick footprint. 88 mm wide, 240 mm long, grey deck, orange marks, black jog and fader. Setup snaps gear onto the amp. Wide is the same deck at the K.O. II size, 176 × 240.5, with the controls that did not fit in 88 mm."
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(ViewerPanel, {}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RenderGallery, {}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("dl", {
				className: "grid grid-cols-3 divide-x divide-line border border-line",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Height",
						v: "240 mm"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Width",
						v: "88 mm"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Thick",
						v: "16 / 22"
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
				className: "text-sm leading-relaxed text-muted",
				children: "The long edge matches the EP row, and the width matches a Sidekick. The 76 mm platter is centered, with 6 mm of deck on each side. The knob sits above it. It is 16 mm thick where it meets the row, the same as a K.O. II and a Sidekick. The top 52 mm rises to 22 mm so a 2×AA holder fits. That rise is part of the top shell. The two halves close on four pairs of magnets. The deck is not engraved. Type tore out of the first print, so the lettering is vinyl, cut 1:1 for a Cricut. The raised end carries the name, the port row and the tempo label. The buttons are labeled on the deck. Pin holes are 16, 25, 48, 216 and 224 mm from the fader end, 8 mm off the bottom, on both long sides."
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
				className: "flex flex-col gap-2 text-sm leading-relaxed",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
						className: "font-mono text-xs tracking-widest text-muted uppercase",
						children: "Wide shells"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-muted",
						children: "176 × 240.5 × 16.3 mm. Twin is two 76 mm platters. Six is one 152 mm platter. Print the top face up and the tray cavity up."
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("p", {
						className: "flex flex-wrap gap-x-4 gap-y-2",
						children: [
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/wide.scad",
								target: "_blank",
								rel: "noreferrer",
								children: "SCAD"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/stl/wide-twin-top.stl",
								target: "_blank",
								rel: "noreferrer",
								children: "Twin top"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/stl/wide-twin-bottom.stl",
								target: "_blank",
								rel: "noreferrer",
								children: "Twin tray"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/stl/wide-twin-platter.stl",
								target: "_blank",
								rel: "noreferrer",
								children: "76 mm"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/stl/wide-six-top.stl",
								target: "_blank",
								rel: "noreferrer",
								children: "Six top"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/stl/wide-six-bottom.stl",
								target: "_blank",
								rel: "noreferrer",
								children: "Six tray"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/stl/wide-six-platter.stl",
								target: "_blank",
								rel: "noreferrer",
								children: "152 mm"
							})
						]
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(RenderGallery, {
				title: "Wide",
				renders: WIDE_RENDERS
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)(Link, {
				to: "/four",
				className: "flex flex-col gap-1 border border-line px-4 py-4",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("span", {
						className: "font-mono text-xs tracking-widest text-accent uppercase",
						children: "Next in the row"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("span", {
						className: "text-lg font-semibold",
						children: "Four"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("span", {
						className: "text-sm text-muted",
						children: "A four-track recorder on its own page. Same brick, five faders, a 3.2 inch screen."
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
				href: "/field-jog/field-jog.zip",
				target: "_blank",
				rel: "noreferrer",
				className: "flex h-12 items-center justify-center bg-accent font-semibold text-accent-fg",
				children: "Download print files"
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
				className: "flex flex-col gap-3",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
						className: "font-mono text-xs tracking-widest text-muted uppercase",
						children: "Vinyl"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("img", {
						src: "/field-jog/silk/preview.png",
						alt: "Vinyl lettering laid out on the deck, jacks at the top",
						className: "w-full border border-line"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-sm leading-relaxed text-muted",
						children: "Cut this in black vinyl. The screen, the knob and the platter stay clear. On the mat file the left piece is the deck and the right piece is the faceplate. A 10 mm bar sits in the gap. If the import comes in small, scale it until that bar is 10 mm."
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", {
						className: "flex flex-wrap gap-4 font-mono text-xs tracking-wide text-accent uppercase",
						children: [
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								href: "/field-jog/silk/mat-cut.svg",
								target: "_blank",
								rel: "noreferrer",
								children: "Mat"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								href: "/field-jog/silk/deck-cut.svg",
								target: "_blank",
								rel: "noreferrer",
								children: "Deck"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								href: "/field-jog/silk/faceplate-cut.svg",
								target: "_blank",
								rel: "noreferrer",
								children: "Faceplate"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								href: "/field-jog/silk/deck-guide.svg",
								target: "_blank",
								rel: "noreferrer",
								children: "Deck guide"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								href: "/field-jog/silk/faceplate-guide.svg",
								target: "_blank",
								rel: "noreferrer",
								children: "Plate guide"
							})
						]
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
				className: "flex flex-col gap-3",
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
					className: "font-mono text-xs tracking-widest text-muted uppercase",
					children: "Parts"
				}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("ul", {
					className: "divide-y divide-line border border-line",
					children: PARTS.map(([file, name, color]) => /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("li", {
						className: "flex items-center justify-between gap-3 px-3 py-3",
						children: [/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("span", {
							className: "flex flex-col",
							children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("span", {
								className: "text-sm",
								children: name
							}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("span", {
								className: "font-mono text-xs text-muted",
								children: color
							})]
						}), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
							href: `/field-jog/stl/${file}`,
							target: "_blank",
							rel: "noreferrer",
							className: "font-mono text-xs tracking-wide text-accent uppercase",
							children: "STL"
						})]
					}, file))
				})]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
				className: "flex flex-col gap-2 text-sm leading-relaxed text-muted",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
						className: "font-mono text-xs tracking-widest uppercase",
						children: "Also in the zip"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", { children: "OpenSCAD source, a Pro Micro MIDI sketch, and the print notes. Face-up, no supports, 0.2 mm layers." }),
					/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("p", {
						className: "flex gap-4",
						children: [
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/field-jog.scad",
								target: "_blank",
								rel: "noreferrer",
								children: "SCAD"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/field_jog.ino",
								target: "_blank",
								rel: "noreferrer",
								children: "Firmware"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/README.md",
								target: "_blank",
								rel: "noreferrer",
								children: "Notes"
							})
						]
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
				className: "flex flex-col gap-2 text-sm leading-relaxed",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("h2", {
						className: "font-mono text-xs tracking-widest text-muted uppercase",
						children: "ECM sleeve"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
						className: "text-muted",
						children: "A 96 × 18.8 mm handheld shell for an ECM-77B head. The capsule slides in from the tip and the cable leaves the back. The orange button is a cap."
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("p", {
						className: "flex gap-4",
						children: [
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/ecm-handy/body.stl",
								target: "_blank",
								rel: "noreferrer",
								children: "Body"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/ecm-handy/button.stl",
								target: "_blank",
								rel: "noreferrer",
								children: "Button"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/ecm-handy/ecm-handy.scad",
								target: "_blank",
								rel: "noreferrer",
								children: "SCAD"
							})
						]
					})
				]
			})
		]
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
export { WideDeck as n, routes_exports as t };
