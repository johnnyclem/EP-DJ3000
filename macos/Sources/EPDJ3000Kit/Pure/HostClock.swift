// Monotonic seconds shared by MIDI packet timestamps and "now".
// CoreMIDI stamps packets in mach host ticks; JogVelocityEstimator wants seconds on one monotonic clock.

import Foundation
#if canImport(Darwin)
import Darwin
#endif

public enum HostClock {
    #if canImport(Darwin)
    private static let timebase: (numer: Double, denom: Double) = {
        var info = mach_timebase_info_data_t()
        mach_timebase_info(&info)
        return (Double(info.numer), Double(max(1, info.denom)))
    }()

    /// Seconds for a `MIDITimeStamp` / `mach_absolute_time()` value.
    public static func seconds(fromHostTicks ticks: UInt64) -> Double {
        Double(ticks) * timebase.numer / timebase.denom / 1e9
    }

    public static func now() -> Double {
        seconds(fromHostTicks: mach_absolute_time())
    }
    #else
    // Only so the pure files can be compiled and tested off macOS; the app never runs here.
    public static func seconds(fromHostTicks ticks: UInt64) -> Double { Double(ticks) / 1e9 }
    public static func now() -> Double { ProcessInfo.processInfo.systemUptime }
    #endif
}
