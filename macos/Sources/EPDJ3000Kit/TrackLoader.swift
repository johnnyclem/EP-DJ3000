// Turns an audio file into what the player wants: a 48 kHz stereo Float32 AudioBuffer, plus the overview.
// Sample-rate conversion happens here, once, with AVAudioConverter at its highest quality, so playback at normal
// speed reads samples as they are instead of interpolating between them.

import AVFoundation
import EPDJ3000Engine
import Foundation

public struct LoadedTrack: Sendable {
    public let name: String
    // Qualified: CoreAudio (via AVFoundation) has its own `AudioBuffer`.
    public let buffer: EPDJ3000Engine.AudioBuffer
    public let overview: WaveformOverview
    /// What the file was, for the UI: "44.1 kHz, 2 ch".
    public let source: String
}

public enum TrackLoadError: LocalizedError, CustomStringConvertible {
    case unsupportedChannelCount(Int)
    case tooLong(seconds: Double, limit: Double)
    case empty
    case converterUnavailable
    case conversionFailed(String)

    public var description: String {
        switch self {
        case let .unsupportedChannelCount(n):
            return "This file has \(n) channels; the player handles mono and stereo."
        case let .tooLong(seconds, limit):
            return "This track is \(Int(seconds / 60)) min long; the demo player keeps whole tracks in memory and stops at \(Int(limit / 60)) min."
        case .empty:
            return "The file contains no audio."
        case .converterUnavailable:
            return "macOS cannot convert this file's format."
        case let .conversionFailed(why):
            return "Decoding failed: \(why)"
        }
    }

    public var errorDescription: String? { description }
}

public enum TrackLoader {
    public static let targetSampleRate = TestSignal.defaultSampleRate
    /// ~350 MB of Float32 at the limit, twice over while it is copied into the AudioBuffer.
    public static let maxSeconds = 12.0 * 60
    public static let overviewBuckets = 1_600

    /// Decodes off the calling actor.
    public static func load(url: URL) async throws -> LoadedTrack {
        try await Task.detached(priority: .userInitiated) { try decode(url: url) }.value
    }

    /// Wraps generated audio (the test signals) the same way a decoded file is wrapped.
    public static func wrap(name: String, source: String, left: [Float], right: [Float],
                            sampleRate: Double = targetSampleRate) -> LoadedTrack {
        LoadedTrack(name: name,
                    buffer: EPDJ3000Engine.AudioBuffer(left: left, right: right, sampleRate: sampleRate),
                    overview: WaveformOverview(left: left, right: right, buckets: overviewBuckets),
                    source: source)
    }

    private static func decode(url: URL) throws -> LoadedTrack {
        let file = try AVAudioFile(forReading: url)
        let inFormat = file.processingFormat
        let channels = Int(inFormat.channelCount)
        guard channels == 1 || channels == 2 else { throw TrackLoadError.unsupportedChannelCount(channels) }
        guard file.length > 0 else { throw TrackLoadError.empty }
        let seconds = Double(file.length) / inFormat.sampleRate
        guard seconds <= maxSeconds else { throw TrackLoadError.tooLong(seconds: seconds, limit: maxSeconds) }

        // Same channel count as the file: down- or up-mixing is done below, where its behaviour is not in doubt.
        guard let outFormat = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: targetSampleRate,
                                            channels: AVAudioChannelCount(channels), interleaved: false),
              let converter = AVAudioConverter(from: inFormat, to: outFormat) else { throw TrackLoadError.converterUnavailable }
        converter.sampleRateConverterQuality = AVAudioQuality.max.rawValue

        let chunk: AVAudioFrameCount = 16_384
        guard let inBuffer = AVAudioPCMBuffer(pcmFormat: inFormat, frameCapacity: chunk),
              let outBuffer = AVAudioPCMBuffer(pcmFormat: outFormat,
                                               frameCapacity: AVAudioFrameCount(Double(chunk) * targetSampleRate / inFormat.sampleRate) + 4_096)
        else { throw TrackLoadError.converterUnavailable }

        let expected = Int(Double(file.length) * targetSampleRate / inFormat.sampleRate) + 4_096
        var left = [Float]()
        var right = [Float]()
        left.reserveCapacity(expected)
        if channels == 2 { right.reserveCapacity(expected) }

        var inputFinished = false
        var readError: Error?
        var guardCounter = 0
        while true {
            outBuffer.frameLength = 0
            var convertError: NSError?
            let status = converter.convert(to: outBuffer, error: &convertError) { _, inputStatus in
                if inputFinished || file.framePosition >= file.length {
                    inputFinished = true
                    inputStatus.pointee = .endOfStream
                    return nil
                }
                do {
                    try file.read(into: inBuffer, frameCount: chunk)
                } catch {
                    readError = error
                    inputFinished = true
                    inputStatus.pointee = .endOfStream
                    return nil
                }
                inputStatus.pointee = .haveData
                return inBuffer
            }
            if let readError { throw TrackLoadError.conversionFailed(readError.localizedDescription) }
            if status == .error { throw TrackLoadError.conversionFailed(convertError?.localizedDescription ?? "unknown error") }

            let produced = Int(outBuffer.frameLength)
            if produced > 0, let data = outBuffer.floatChannelData {
                left.append(contentsOf: UnsafeBufferPointer(start: data[0], count: produced))
                if channels == 2 { right.append(contentsOf: UnsafeBufferPointer(start: data[1], count: produced)) }
            }
            if status == .endOfStream || status == .inputRanDry { break }
            // A converter that keeps answering "have data" with nothing in it would spin forever.
            guardCounter = produced == 0 ? guardCounter + 1 : 0
            if guardCounter > 8 { break }
        }

        guard !left.isEmpty else { throw TrackLoadError.empty }
        let stereo = channels == 2
        let name = url.deletingPathExtension().lastPathComponent
        let description = "\(String(format: "%.1f", inFormat.sampleRate / 1000)) kHz, \(stereo ? "stereo" : "mono")"
        return wrap(name: name, source: description, left: left, right: stereo ? right : left)
    }
}
