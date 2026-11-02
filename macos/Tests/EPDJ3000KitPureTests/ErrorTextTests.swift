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
                                       .insufficientMotion(ticks: 5), .notComplete]
        for e in all { XCTAssertFalse(ErrorText.describe(e).isEmpty) }
        XCTAssertTrue(ErrorText.describe(CalibrationError.deadSector(2)).contains("Sector 3"), "sectors are shown 1-based")
    }
}
