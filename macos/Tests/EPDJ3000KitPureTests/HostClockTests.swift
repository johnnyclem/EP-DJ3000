import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class HostClockTests: XCTestCase {
    func testMonotonic() {
        let a = HostClock.now()
        Thread.sleep(forTimeInterval: 0.01)
        let b = HostClock.now()
        XCTAssertGreaterThan(b, a)
        XCTAssertLessThan(b - a, 1)
    }
}
