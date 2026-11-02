import Foundation
import XCTest
@testable import EPDJ3000KitPure
import EPDJ3000Protocol

final class WaveformOverviewTests: XCTestCase {
    func testPeaksOfASine() {
        let s = TestSignal.sine(frequency: 100, seconds: 1, amplitude: 0.5)
        let overview = WaveformOverview(left: s.left, right: s.right, buckets: 100)
        XCTAssertEqual(overview.peaks.count, 100)
        for p in overview.peaks { XCTAssertEqual(p, 0.5, accuracy: 0.01) }
    }

    func testTakesTheLouderChannelAndClamps() {
        let left = [Float](repeating: 0.1, count: 100)
        var right = [Float](repeating: 0, count: 100)
        right[55] = -3
        let overview = WaveformOverview(left: left, right: right, buckets: 10)
        XCTAssertEqual(overview.peaks[5], 1)
        XCTAssertEqual(overview.peaks[0], 0.1, accuracy: 1e-6)
    }

    func testShortAndEmptyInput() {
        XCTAssertEqual(WaveformOverview(left: [0.2, 0.4, 0.6], right: [0, 0, 0], buckets: 1000).peaks, [0.2, 0.4, 0.6])
        XCTAssertTrue(WaveformOverview(left: [], right: [], buckets: 100).peaks.isEmpty)
        XCTAssertTrue(WaveformOverview(left: [1], right: [1], buckets: 0).peaks.isEmpty)
        XCTAssertEqual(WaveformOverview(left: [.nan, .infinity], right: [0, 0], buckets: 2).peaks, [0, 0], "non-finite samples count as silence")
    }

    func testEveryFrameIsInExactlyOneBucket() {
        // A single loud frame must show up whichever bucket count is chosen.
        for buckets in [1, 2, 3, 7, 64, 99, 100] {
            for position in [0, 1, 49, 98, 99] {
                var left = [Float](repeating: 0, count: 100)
                left[position] = 0.9
                let peaks = WaveformOverview(left: left, right: left, buckets: buckets).peaks
                XCTAssertEqual(peaks.filter { $0 > 0.5 }.count, 1, "buckets \(buckets) position \(position)")
            }
        }
    }
}
