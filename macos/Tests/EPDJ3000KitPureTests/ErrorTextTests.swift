import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class ErrorTextTests: XCTestCase {
    func testDeviceErrors() {
        XCTAssertTrue(ErrorText.describe(DeviceError.timeout(.getInfo)).contains("GET_INFO"))
        XCTAssertTrue(ErrorText.describe(DeviceError.nak(.testTone, .notSupported)).contains("TEST_TONE"))
        XCTAssertTrue(ErrorText.describe(DeviceError.nak(.testTone, .notSupported)).contains("no DAC"))
        XCTAssertTrue(ErrorText.describe(DeviceError.unknownNak(.setConfig, code: 99)).contains("99"))
        XCTAssertTrue(ErrorText.describe(DeviceError.transportClosed).contains("disconnected"))
        XCTAssertTrue(ErrorText.describe(DeviceError.sendFailed("boom")).contains("boom"))
        XCTAssertTrue(ErrorText.describe(DeviceError.malformedReply(.linkStatus)).contains("LINK_STATUS"))
        XCTAssertTrue(ErrorText.describe(DeviceError.protocolMismatch(Info(protocolVersion: 9))).contains("v9"))
        XCTAssertEqual(ErrorText.describe(CancellationError()), "Cancelled.")
    }

    func testEverySysExErrorAndCalibrationErrorHasText() {
        for e in SysExError.allCases { XCTAssertFalse(ErrorText.describe(e).isEmpty) }
        let all: [CalibrationError] = [.insufficientTravel(span: 10, required: 1500), .unstableIdle(noisePermille: 40), .deadSector(2),
                                       .noFingerDetected(sector: 0), .insufficientContrast(weakestRisePermille: 3, requiredOnPermille: 9),
                                       .insufficientMotion(ticks: 5), .padNotConnected, .noHandDetected, .notComplete]
        for e in all { XCTAssertFalse(ErrorText.describe(e).isEmpty) }
        XCTAssertTrue(ErrorText.describe(CalibrationError.deadSector(2)).contains("Sector 3"), "sectors are shown 1-based")
    }

    func testHoldPadErrorsTalkAboutThePadAndTheRotatingJointNotASector() {
        for e in [CalibrationError.padNotConnected, .noHandDetected] {
            let text = ErrorText.describe(e)
            XCTAssertFalse(text.lowercased().contains("sector"), text)
            XCTAssertTrue(text.contains("rotating") || text.contains("spinning"), "the joint is the thing to suspect: \(text)")
        }
        XCTAssertFalse(ErrorText.describe(CalibrationError.insufficientContrast(weakestRisePermille: 3, requiredOnPermille: 9))
            .lowercased().contains("sector"), "that error is shared by the ring and the hold pad")
    }

    func testEveryEncoderVerdictSaysWhatToDo() {
        let none = ErrorText.describe(EncoderDiagnosis.Verdict.noEncoder)
        XCTAssertTrue(none.contains("SDA") && none.contains("SCL"), none)
        XCTAssertTrue(none.contains("3.3 V") && none.contains("5 V"), "the voltage strap is the other usual suspect: \(none)")

        let failing = ErrorText.describe(EncoderDiagnosis.Verdict.readsFailing(samples: 500))
        XCTAssertTrue(failing.contains("500") && failing.contains("SDA"), failing)

        let still = ErrorText.describe(EncoderDiagnosis.Verdict.notMoving(spanCounts: 1024))
        XCTAssertTrue(still.contains("1024") && still.contains("0.25 turn"), still)
        XCTAssertTrue(still.contains("magnet") && still.contains("1 to 3 mm"), "the gap is from field-jog/README.md: \(still)")
        XCTAssertTrue(still.contains("too weak") && still.contains("too strong"), "points at the firmware's magnet status: \(still)")

        let erratic = ErrorText.describe(EncoderDiagnosis.Verdict.erratic(jumps: 80, steps: 100))
        XCTAssertTrue(erratic.contains("80 of 100") && erratic.contains("magnet"), erratic)

        XCTAssertEqual(ErrorText.describe(EncoderDiagnosis.Verdict.ok(turns: 1.04, invalidSamples: 0)),
                       "The AS5600 follows the platter smoothly: 1.0 turns measured.")
        XCTAssertTrue(ErrorText.describe(EncoderDiagnosis.Verdict.ok(turns: 2, invalidSamples: 3)).contains("3 readings failed"))

        let all: [EncoderDiagnosis.Verdict] = [.noEncoder, .readsFailing(samples: 1), .notMoving(spanCounts: 0),
                                               .erratic(jumps: 1, steps: 1), .ok(turns: 1, invalidSamples: 0)]
        XCTAssertEqual(Set(all.map { ErrorText.describe($0) }).count, all.count, "one distinct message per verdict")
    }
}
