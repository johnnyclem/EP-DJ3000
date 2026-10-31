import { i as __toESM } from "../_runtime.mjs";
import { g as require_react, h as require_jsx_runtime } from "../_libs/@react-three/drei+[...].mjs";
//#region node_modules/.nitro/vite/services/ssr/assets/routes-BZx2qJLA.js
var import_react = /* @__PURE__ */ __toESM(require_react());
var import_jsx_runtime = require_jsx_runtime();
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
		label: "Buttons"
	},
	{
		id: "plus",
		label: "Plus"
	},
	{
		id: "tab",
		label: "Tab"
	},
	{
		id: "bar",
		label: "Bar"
	},
	{
		id: "foot",
		label: "Feet"
	}
];
function ViewerPanel() {
	const [focus, setFocus] = (0, import_react.useState)("unit");
	const [spin, setSpin] = (0, import_react.useState)(true);
	const [fitTick, setFitTick] = (0, import_react.useState)(0);
	const [ready, setReady] = (0, import_react.useState)(false);
	const [Scene, setScene] = (0, import_react.useState)(null);
	(0, import_react.useEffect)(() => {
		let live = true;
		import("./unit-scene-BepQDQvz.mjs").then((mod) => {
			if (live) setScene(() => mod.UnitScene);
		});
		return () => {
			live = false;
		};
	}, []);
	const onReady = (0, import_react.useCallback)(() => setReady(true), []);
	const onInteract = (0, import_react.useCallback)(() => setSpin(false), []);
	return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("section", {
		className: "flex flex-col gap-3",
		children: [
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", {
				className: "relative h-viewer w-full touch-none overflow-hidden border border-line bg-surface",
				children: [Scene ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(Scene, {
					focus,
					watch: `${focus}:${fitTick}`,
					spin,
					onReady,
					onInteract
				}) : null, ready ? null : /* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
					className: "pointer-events-none absolute inset-0 flex items-center justify-center text-sm text-muted",
					children: "Loading the model"
				})]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", {
				className: "flex items-center justify-between gap-3",
				children: [/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
					className: "text-sm text-muted",
					children: "Drag to turn. Pinch to zoom."
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
				className: "flex gap-2 overflow-x-auto pb-1",
				children: VIEW_ITEMS.map((item) => {
					const selected = item.id === focus;
					return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("button", {
						type: "button",
						"aria-pressed": selected,
						onClick: () => setFocus(item.id),
						className: selected ? "h-11 shrink-0 bg-accent px-3 font-mono text-xs tracking-wide text-accent-fg uppercase" : "h-11 shrink-0 border border-line px-3 font-mono text-xs tracking-wide text-fg uppercase",
						children: item.label
					}, item.id);
				})
			})
		]
	});
}
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
						children: "A scratch-deck shell that lines up with the EP-133 and the Sidekick. Grey deck, orange marks, black jog and fader. Four Technic pin holes on each long side, on the same 8 mm grid as the EP row."
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)(ViewerPanel, {}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsxs)("dl", {
				className: "grid grid-cols-3 divide-x divide-line border border-line",
				children: [
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Height",
						v: "240 mm"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Width",
						v: "120 mm"
					}),
					/* @__PURE__ */ (0, import_jsx_runtime.jsx)(Spec, {
						k: "Thick",
						v: "26 mm"
					})
				]
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("p", {
				className: "text-sm leading-relaxed text-muted",
				children: "Height matches the EP row. It is wider than a Sidekick because a 76 mm platter does not fit in 88 mm, and thicker than 16 mm because the bearing, magnet, and fader lock need a cavity. The pin holes sit 8 mm off the bottom, so a Technic pin meets a K.O. II on the same desk. Centers are 108, 116, 124 and 132 mm up each long side."
			}),
			/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
				href: "/field-jog/field-jog.zip",
				download: true,
				className: "flex h-12 items-center justify-center bg-accent font-semibold text-accent-fg",
				children: "Download print files"
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
							download: true,
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
								download: true,
								children: "SCAD"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/field_jog.ino",
								download: true,
								children: "Firmware"
							}),
							/* @__PURE__ */ (0, import_jsx_runtime.jsx)("a", {
								className: "text-fg underline",
								href: "/field-jog/README.md",
								download: true,
								children: "Notes"
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
export { Home as component };
