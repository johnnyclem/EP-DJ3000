import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class PlatterDisplayTests: XCTestCase {
    private func frame(angle: UInt16, valid: Bool = true) -> Telemetry {
        Telemetry(flags: valid ? StateFlags.angleValid.rawValue : 0, angle: angle)
    }

    private func reading(_ t: Telemetry?, jog: Double = 1.0, source: PlatterSource = .as5600, invert: Bool = false) -> PlatterDisplay.Reading {
        PlatterDisplay.reading(jogRadians: jog, telemetry: t, platterSource: source, jogInvert: invert)
    }

    func testWithTheStreamOffTheJogAngleIsDrawn() {
        let r = reading(nil, jog: 2.5)
        XCTAssertEqual(r, PlatterDisplay.Reading(radians: 2.5, source: .jog))
    }

    func testWithTheStreamOnTheChipAngleIsDrawnWhetherOrNotAnyJogWasSent() {
        // A quarter turn on the chip, a jog stream that has said nothing (an untouched platter once the pad has worked, or SUPPRESS).
        let r = reading(frame(angle: 1024), jog: 0)
        XCTAssertEqual(r.source, .telemetry)
        XCTAssertEqual(r.radians, Double.pi / 2, accuracy: 1e-12, "clockwise, 12 o'clock = 0")

        // The platter coasts a half turn on: the drawing follows, still without a jog message.
        XCTAssertEqual(reading(frame(angle: 3072), jog: 0).radians, 3 * Double.pi / 2, accuracy: 1e-12)
    }

    func testJogInvertMakesTheChipAngleAgreeWithTheJogDirection() {
        // The firmware negates ticks for jogInvert = 1 but sends Telemetry.angle raw (Engine::Device::getTelemetry).
        let r = reading(frame(angle: 1024), invert: true)
        XCTAssertEqual(r.radians, 3 * Double.pi / 2, accuracy: 1e-12, "a quarter turn the other way, wrapped into [0, 2 pi)")
        XCTAssertEqual(reading(frame(angle: 0), invert: true).radians, 0, accuracy: 1e-12)
        for raw in stride(from: 0, to: 4096, by: 97) {
            let a = reading(frame(angle: UInt16(raw))).radians
            let b = reading(frame(angle: UInt16(raw)), invert: true).radians
            XCTAssertTrue((0..<2 * Double.pi).contains(a) && (0..<2 * Double.pi).contains(b), "\(raw)")
            let sum = (a + b).truncatingRemainder(dividingBy: 2 * Double.pi)
            XCTAssertLessThan(min(sum, 2 * Double.pi - sum), 1e-9, "mirror images, \(raw)")
        }
    }

    func testAnAngleTheUnitSaysIsInvalidFallsBackToJog() {
        let r = reading(frame(angle: 2048, valid: false), jog: 0.75)
        XCTAssertEqual(r, PlatterDisplay.Reading(radians: 0.75, source: .jog), "reads failing: do not draw a stale angle")
    }

    func testTheChipAngleIsOnlyUsedForTheAS5600Platter() {
        for source in [PlatterSource.touchRing, .touchHoldOnly] {
            // With the ring Telemetry.angle is the finger's phase; it is not the platter and jog is only sent under a finger anyway.
            let r = reading(frame(angle: 1024), jog: 0.5, source: source)
            XCTAssertEqual(r, PlatterDisplay.Reading(radians: 0.5, source: .jog), "\(source)")
        }
    }

    func testOnlyTheTwelveBitsOfTheAngleCount() {
        XCTAssertEqual(reading(frame(angle: 4096 + 1024)).radians, Double.pi / 2, accuracy: 1e-12, "a defensive mask, like EncoderDiagnosis")
    }

    func testAContinuousSpinStaysContinuousAcrossTheWrap() {
        // Drawing the chip angle must not jump when the counter wraps 4095 -> 0.
        var last = reading(frame(angle: 4090)).radians
        for step in 1...12 {
            let raw = UInt16((4090 + step) % 4096)
            let now = reading(frame(angle: raw)).radians
            var d = now - last
            if d > Double.pi { d -= 2 * Double.pi } else if d < -Double.pi { d += 2 * Double.pi }
            XCTAssertEqual(d, 2 * Double.pi / 4096, accuracy: 1e-9, "step to \(raw)")
            last = now
        }
    }
}
