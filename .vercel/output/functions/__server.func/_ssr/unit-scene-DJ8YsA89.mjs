import { i as __toESM } from "../_runtime.mjs";
import { a as useLoader, g as Vector3, i as useFrame, m as Quaternion, n as OrbitControls, o as Box3, p as Matrix4, r as Canvas, t as RoundedBox, v as require_jsx_runtime, y as require_react } from "../_libs/@react-three/drei+[...].mjs";
import { t as STLLoader } from "../_libs/three.mjs";
//#region node_modules/.nitro/vite/services/ssr/assets/unit-scene-DJ8YsA89.js
var import_react = /* @__PURE__ */ __toESM(require_react());
var import_jsx_runtime = require_jsx_runtime();
var GREY$2 = "#d2d2cc";
var CREAM = "#e4dcc8";
var GREEN = "#1f6b45";
var BLACK$2 = "#141416";
var ORANGE$2 = "#f05a1a";
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
		color: GREY$2
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
		color: GREY$2
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
		color: GREY$2
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
				color: GREY$2,
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
			] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: ORANGE$2 })]
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
			color: ORANGE$2,
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
	const screen = gear === "riddim" ? GREEN : gear === "ting" || gear === "pedal" ? ORANGE$2 : gear === "mixer" ? INK : BLACK$2;
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
				] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: ORANGE$2 })]
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
				] }), /* @__PURE__ */ (0, import_jsx_runtime.jsx)("meshStandardMaterial", { color: BLACK$2 })]
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
			color: ORANGE$2,
			emissive: ORANGE$2,
			emissiveIntensity: .25
		})] })]
	});
}
var W = 176;
var H = 240.5;
var T = 16.3;
var GREY$1 = "#d4d4ce";
var BLACK$1 = "#141416";
var WELL = "#2a2a28";
var ORANGE$1 = "#f05a1a";
var SLOT = "#1a1a1c";
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
				] }), mat(BLACK$1, .42)]
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
				] }), mat(ORANGE$1, .4)]
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
				] }), mat(ORANGE$1, .4)]
			})
		]
	});
}
function Cap({ x, y, d, h = 4, color = BLACK$1 }) {
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
		] }), mat(ORANGE$1, .3)]
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
		children: mat(BLACK$1, .4)
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
		children: mat(GREY$1, .58)
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
function Twin() {
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
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Platter, {
				x: d.x,
				y: 158,
				d: 76
			}),
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
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Pitch, {
				x: d.pitch,
				y0: 24,
				y1: 108
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
		] }, d.x)),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Crossfader, {}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Screen, {
			x: 88,
			y: 220,
			w: 78
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
			x: 158,
			y: 218,
			d: 16,
			h: 8,
			color: ORANGE$1
		})
	] });
}
function Six() {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Platter, {
			x: 88,
			y: 142,
			d: 152.4
		}),
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
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Pitch, {
			x: 166,
			y0: 12,
			y1: 60
		}),
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
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Screen, {
			x: 78,
			y: 228,
			w: 64
		}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Cap, {
			x: 150,
			y: 226,
			d: 14,
			h: 7,
			color: ORANGE$1
		})
	] });
}
function WideDeck({ layout }) {
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("group", { children: [
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Shell, {}),
		/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Jacks, {}),
		layout === "twin" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Twin, {}) : /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Six, {})
	] });
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
function Scene({ mode, focus, slots, activeFace, watch, spin, layout, onReady, onPickFace }) {
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
					mode === "wide" ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(WideDeck, { layout }) : null,
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
function UnitScene({ mode, focus, slots, activeFace, watch, spin, layout, onReady, onInteract, onPickFace }) {
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
			onReady,
			onPickFace
		})
	});
}
//#endregion
export { UnitScene };
