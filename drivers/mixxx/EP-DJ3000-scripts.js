// EP-DJ3000 controller script for Mixxx 2.4 / 2.5 (written in ES5 style).
//
// One mapping serves both decks: the MIDI channel selects the deck
// (ch 1 -> [Channel1], ch 2 -> [Channel2]), exactly as the unit sends it, so a
// second unit or the "Linked deck" USB-MIDI cable needs no extra configuration.
// Wire numbers come from epdj3000-protocol.js (generated from protocol/epdj3000.json),
// which must be installed next to this file and the XML.
//
// Not using components.js: there are no shift layers or modes here, and the
// plain handlers are easier to test against a mock engine.
//
// Documentation relied on (Mixxx wiki / manual / source):
//   MIDI Scripting       https://github.com/mixxxdj/mixxx/wiki/midi-scripting
//     (init/shutdown + functionprefix, Script-Binding handlers, makeConnection,
//      softTakeover, scratchEnable/scratchTick/scratchDisable/isScratching, "jog")
//   Mapping file format  https://github.com/mixxxdj/mixxx/wiki/Midi-Controller-Mapping-File-Format
//   Mixxx controls       https://manual.mixxx.org/2.5/en/chapters/appendix/mixxx_controls.html
//   Signatures checked against src/controllers/scripting/legacy/controllerscriptinterfacelegacy.h
//   (engine.setParameter, scratchEnable(deck, intervalsPerRev, rpm, alpha, beta, ramp = true)).
//
// The mapping object is NOT called EPDJ3000: that global is the constants object
// defined by epdj3000-protocol.js, and Mixxx calls init()/shutdown() on whatever
// object the XML functionprefix names.

var EPDJ3000Mixxx = {};

// ---- Settings you may want to change ---------------------------------------

// What the slide pot does when the unit sends it as CC 23/55 (the unit's default
// faderRole = CROSSFADER). "crossfader" -> [Master],crossfader; "volume" -> deck
// volume; "rate" -> deck tempo slider. A unit reconfigured to faderRole =
// CHANNEL_VOLUME / PITCH sends CC 7/39 / CC 25/57, which are always deck volume /
// deck rate regardless of this setting.
EPDJ3000Mixxx.FADER_ROLE = "crossfader";

// REV button: "roll" = [ChannelN],reverseroll while held (momentary reverse that
// resumes where the track would have been, Mixxx's "censor"); "toggle" =
// [ChannelN],reverse flips on each press (CDJ-style latching reverse).
EPDJ3000Mixxx.REV_BEHAVIOR = "roll";

// Pitch-bend when the platter is moved without being touched (free-spinning
// AS5600 build): [ChannelN],jog = ticks * BEND_SCALE. UNTUNED, adjust on hardware.
EPDJ3000Mixxx.BEND_SCALE = 1.0;

// engine.scratchEnable parameters. The unit always reports 1024 ticks per
// revolution; rpm is the speed of the imaginary record at 0% pitch.
// alpha/beta are the start values the Mixxx docs suggest (1/8, alpha/32). UNTUNED.
EPDJ3000Mixxx.SCRATCH = {
    intervalsPerRev: EPDJ3000.jogTicksPerRev,
    rpm: 33 + 1 / 3,
    alpha: 1 / 8,
    beta: (1 / 8) / 32,
    rampOnTouch: true,
    rampOnRelease: true
};

// ---- Internals --------------------------------------------------------------

// Control the [ChannelN] fader/knob roles resolve to: [group, key].
EPDJ3000Mixxx.TARGETS = {
    crossfader: function () { return ["[Master]", "crossfader"]; },
    volume: function (group) { return [group, "volume"]; },
    rate: function (group) { return [group, "rate"]; },
    filter: function (group) { return ["[QuickEffectRack1_" + group + "]", "super1"]; }
};

// Mixxx's jog control is documented as -3.0..3.0.
EPDJ3000Mixxx.JOG_LIMIT = 3.0;
// One bpmNudge step (PROTOCOL.md 2.6 / config: 0.1 BPM).
EPDJ3000Mixxx.BPM_STEP = 0.1;

// Per-instance state. Mixxx runs one script engine per MIDI port, so with a second
// ("Linked deck") cable this file is loaded twice and each copy must only undo what
// it did itself.
EPDJ3000Mixxx.connections = [];
EPDJ3000Mixxx.pairs = {};
EPDJ3000Mixxx.held = {};
EPDJ3000Mixxx.scratching = {};

EPDJ3000Mixxx.log = function (message) {
    console.log("EP-DJ3000: " + message);
};

// Wraps fn(deck, group, value, status, control) so that messages on a channel that
// is not a deck channel are ignored.
EPDJ3000Mixxx.deckHandler = function (fn) {
    return function (channel, control, value, status) {
        var index = EPDJ3000.defaultChannels.indexOf(channel);
        if (index < 0) {
            return;
        }
        var deck = index + 1;
        fn(deck, "[Channel" + deck + "]", value, status, control);
    };
};

// A note is "down" for Note On with velocity > 0. Note Off (0x8n) is always "up",
// whatever its release velocity; Note On velocity 0 is "up" too (PROTOCOL.md 2.1).
EPDJ3000Mixxx.isDown = function (status, value) {
    return (status & 0xF0) !== 0x80 && value > 0;
};

// Press-and-release for Mixxx push-button controls that act on the rising edge.
EPDJ3000Mixxx.pulse = function (group, key) {
    engine.setValue(group, key, 1);
    engine.setValue(group, key, 0);
};

// 14-bit CC pair, MSB sent first (PROTOCOL.md 2.1). Either half may be skipped by the
// unit when unchanged, so each message is applied against the stored other half.
// Nothing is applied until both halves have been seen once: after a Mixxx restart
// the unit may send only the LSB, and combining it with an assumed MSB of 0 would
// make the control jump to the bottom.
EPDJ3000Mixxx.pair14 = function (name, apply) {
    var msbCC = EPDJ3000.cc[name + "Msb"];
    var lsbCC = EPDJ3000.cc[name + "Lsb"];
    return EPDJ3000Mixxx.deckHandler(function (deck, group, value, status, control) {
        var key = name + deck;
        var pair = EPDJ3000Mixxx.pairs[key];
        if (!pair) {
            pair = { msb: 0, lsb: 0, haveMsb: false, haveLsb: false };
            EPDJ3000Mixxx.pairs[key] = pair;
        }
        if (control === msbCC) {
            pair.msb = value & 0x7F;
            pair.haveMsb = true;
        } else if (control === lsbCC) {
            pair.lsb = value & 0x7F;
            pair.haveLsb = true;
        } else {
            return;
        }
        if (pair.haveMsb && pair.haveLsb) {
            apply(deck, group, (pair.msb << 7) | pair.lsb);
        }
    });
};

// 14-bit value -> 0..1 -> Mixxx parameter (the control's own range and taper).
EPDJ3000Mixxx.setFromFader = function (role, group, value14) {
    var target = EPDJ3000Mixxx.TARGETS[role](group);
    engine.setParameter(target[0], target[1], value14 / 0x3FFF);
};

// ---- MIDI input handlers (named by the XML <key> elements) -------------------

// jog: CC 22, relative offset-64, 1024 ticks/rev, positive = clockwise.
// Touched -> scratch; untouched -> pitch bend. Value 64 means "no motion".
EPDJ3000Mixxx.jog = EPDJ3000Mixxx.deckHandler(function (deck, group, value) {
    var delta = value - EPDJ3000.relativeCenter;
    if (delta === 0) {
        return;
    }
    if (engine.isScratching(deck)) {
        engine.scratchTick(deck, delta);
        return;
    }
    // [ChannelN],jog is an accumulator: Mixxx reads it once per audio callback and then zeroes it
    // (RateControl::getJogFactor). The unit sends up to 500 messages/s, so several can arrive within
    // one callback; adding to the unread value keeps all of them, overwriting would drop all but the last.
    var bend = engine.getValue(group, "jog") + delta * EPDJ3000Mixxx.BEND_SCALE;
    var limit = EPDJ3000Mixxx.JOG_LIMIT;
    engine.setValue(group, "jog", Math.max(-limit, Math.min(limit, bend)));
});

// touch: note 38. Down = platter touched = scratch mode.
EPDJ3000Mixxx.touch = EPDJ3000Mixxx.deckHandler(function (deck, group, value, status) {
    var s = EPDJ3000Mixxx.SCRATCH;
    if (EPDJ3000Mixxx.isDown(status, value)) {
        engine.scratchEnable(deck, s.intervalsPerRev, s.rpm, s.alpha, s.beta, s.rampOnTouch);
        EPDJ3000Mixxx.scratching[deck] = true;
    } else if (engine.isScratching(deck)) {
        engine.scratchDisable(deck, s.rampOnRelease);
        EPDJ3000Mixxx.scratching[deck] = false;
    }
});

// cue: note 36 -> cue_default (CDJ-style cue; honours the cue mode chosen in
// Mixxx's preferences). Held state follows the button.
EPDJ3000Mixxx.cue = EPDJ3000Mixxx.deckHandler(function (deck, group, value, status) {
    var down = EPDJ3000Mixxx.isDown(status, value);
    EPDJ3000Mixxx.held[group + "cue"] = down;
    engine.setValue(group, "cue_default", down ? 1 : 0);
});

// rev: note 37, see REV_BEHAVIOR.
EPDJ3000Mixxx.rev = EPDJ3000Mixxx.deckHandler(function (deck, group, value, status) {
    var down = EPDJ3000Mixxx.isDown(status, value);
    if (EPDJ3000Mixxx.REV_BEHAVIOR === "toggle") {
        if (down) {
            engine.setValue(group, "reverse", engine.getValue(group, "reverse") ? 0 : 1);
        }
        return;
    }
    EPDJ3000Mixxx.held[group + "rev"] = down;
    engine.setValue(group, "reverseroll", down ? 1 : 0);
});

// play: note 39 is a toggle (a platter tap, or a future play button). Only the
// press acts; the pulse's release (vel 0 / Note Off) is ignored.
EPDJ3000Mixxx.play = EPDJ3000Mixxx.deckHandler(function (deck, group, value, status) {
    if (EPDJ3000Mixxx.isDown(status, value)) {
        engine.setValue(group, "play", engine.getValue(group, "play") ? 0 : 1);
    }
});

// tap: note 41, tap tempo. [ChannelN],tempo_tap only exists since Mixxx 2.5;
// on 2.4 Mixxx logs an unknown control and nothing else happens.
EPDJ3000Mixxx.tap = EPDJ3000Mixxx.deckHandler(function (deck, group, value, status) {
    if (EPDJ3000Mixxx.isDown(status, value)) {
        EPDJ3000Mixxx.pulse(group, "tempo_tap");
    }
});

// transport: CC 29, absolute state (stop = rewind and stop, play, pause).
EPDJ3000Mixxx.transport = EPDJ3000Mixxx.deckHandler(function (deck, group, value) {
    var state = EPDJ3000.enums.TransportState;
    if (value === state.playing) {
        engine.setValue(group, "play", 1);
    } else if (value === state.paused) {
        engine.setValue(group, "play", 0);
    } else if (value === state.stopped) {
        EPDJ3000Mixxx.pulse(group, "start_stop");
    }
});

// bpm: CC 20/52, value = BPM x 10. Sets the deck's tempo (Mixxx's [ChannelN],bpm
// accepts writes and moves the rate slider to match). Ignored with no analysed
// track, out of the contract's 20.0..300.0 range, or if already within 0.05 BPM
// (the unit rounds to 0.1, so echoing back would otherwise creep the rate).
EPDJ3000Mixxx.bpm = EPDJ3000Mixxx.pair14("bpm", function (deck, group, value14) {
    if (value14 < EPDJ3000.bpmMinX10 || value14 > EPDJ3000.bpmMaxX10) {
        return;
    }
    if (!(engine.getValue(group, "file_bpm") > 0)) {
        return;
    }
    var bpm = value14 / 10;
    if (Math.abs(engine.getValue(group, "bpm") - bpm) < 0.05) {
        return;
    }
    engine.setValue(group, "bpm", bpm);
});

// bpmNudge: CC 21, relative offset-64, 0.1 BPM per step.
EPDJ3000Mixxx.bpmNudge = EPDJ3000Mixxx.deckHandler(function (deck, group, value) {
    var delta = value - EPDJ3000.relativeCenter;
    var current = engine.getValue(group, "bpm");
    if (delta === 0 || !(engine.getValue(group, "file_bpm") > 0)) {
        return;
    }
    var bpm = current + delta * EPDJ3000Mixxx.BPM_STEP;
    engine.setValue(group, "bpm",
        Math.max(EPDJ3000.bpmMinX10 / 10, Math.min(EPDJ3000.bpmMaxX10 / 10, bpm)));
});

// Slide pot (14-bit): which pair arrives depends on the unit's faderRole.
EPDJ3000Mixxx.crossfader = EPDJ3000Mixxx.pair14("crossfader", function (deck, group, value14) {
    EPDJ3000Mixxx.setFromFader(EPDJ3000Mixxx.FADER_ROLE, group, value14);
});
EPDJ3000Mixxx.channelVolume = EPDJ3000Mixxx.pair14("channelVolume", function (deck, group, value14) {
    EPDJ3000Mixxx.setFromFader("volume", group, value14);
});
EPDJ3000Mixxx.pitch = EPDJ3000Mixxx.pair14("pitch", function (deck, group, value14) {
    EPDJ3000Mixxx.setFromFader("rate", group, value14);
});

// Orange knob (14-bit): the deck's Quick Effect knob (filter by default in Mixxx).
// Chosen over pregain because it stays audible when the deck's output goes to the
// hardware mixer, and its neutral position is the middle of the range.
EPDJ3000Mixxx.knob = EPDJ3000Mixxx.pair14("knob", function (deck, group, value14) {
    EPDJ3000Mixxx.setFromFader("filter", group, value14);
});

// ---- LED feedback to the unit -------------------------------------------------

// Note On velocity > 0 lights the unit's button, 0 turns it off (PROTOCOL.md 2).
EPDJ3000Mixxx.ledCallback = function (channel, note) {
    return function (value) {
        midi.sendShortMsg(0x90 | channel, note, value ? 0x7F : 0x00);
    };
};

// ---- Lifecycle ----------------------------------------------------------------

EPDJ3000Mixxx.init = function (id, debugging) {
    var roles = ["crossfader", "volume", "rate"];
    if (roles.indexOf(EPDJ3000Mixxx.FADER_ROLE) < 0) {
        throw new Error("EPDJ3000Mixxx.FADER_ROLE must be one of " + roles.join(", "));
    }
    if (EPDJ3000Mixxx.REV_BEHAVIOR !== "roll" && EPDJ3000Mixxx.REV_BEHAVIOR !== "toggle") {
        throw new Error("EPDJ3000Mixxx.REV_BEHAVIOR must be \"roll\" or \"toggle\"");
    }

    EPDJ3000Mixxx.connections = [];
    EPDJ3000Mixxx.pairs = {};
    EPDJ3000Mixxx.held = {};
    EPDJ3000Mixxx.scratching = {};

    // [feedback control, unit note it lights]
    var leds = [
        ["cue_indicator", EPDJ3000.note.cue],
        ["play", EPDJ3000.note.play]
    ];

    EPDJ3000.defaultChannels.forEach(function (channel, index) {
        var group = "[Channel" + (index + 1) + "]";

        // Absolute controls on a physical pot: do not jump to the pot position
        // when it disagrees with Mixxx (loaded track, sync, GUI changes).
        ["volume", "rate", "filter"].forEach(function (role) {
            var target = EPDJ3000Mixxx.TARGETS[role](group);
            engine.softTakeover(target[0], target[1], true);
        });

        leds.forEach(function (led) {
            var connection = engine.makeConnection(group, led[0],
                EPDJ3000Mixxx.ledCallback(channel, led[1]));
            if (connection) {
                EPDJ3000Mixxx.connections.push(connection);
                connection.trigger();
            }
        });
    });

    EPDJ3000Mixxx.log("init " + id + (debugging ? " (controller debug on)" : ""));
};

EPDJ3000Mixxx.shutdown = function () {
    EPDJ3000Mixxx.connections.forEach(function (connection) {
        connection.disconnect();
    });
    EPDJ3000Mixxx.connections = [];

    EPDJ3000.defaultChannels.forEach(function (channel, index) {
        var deck = index + 1;
        var group = "[Channel" + deck + "]";

        // Do not leave Mixxx stuck in a state only the unit's button release would end.
        if (EPDJ3000Mixxx.scratching[deck]) {
            engine.scratchDisable(deck, false);
        }
        if (EPDJ3000Mixxx.held[group + "rev"]) {
            engine.setValue(group, "reverseroll", 0);
        }
        if (EPDJ3000Mixxx.held[group + "cue"]) {
            engine.setValue(group, "cue_default", 0);
        }

        midi.sendShortMsg(0x90 | channel, EPDJ3000.note.cue, 0x00);
        midi.sendShortMsg(0x90 | channel, EPDJ3000.note.play, 0x00);
    });
    EPDJ3000Mixxx.held = {};
    EPDJ3000Mixxx.scratching = {};
};
