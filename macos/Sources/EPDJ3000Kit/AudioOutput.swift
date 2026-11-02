// An AVAudioEngine with one AVAudioSourceNode that pulls from a ScratchPlayerCore, optionally pinned to one
// CoreAudio device. Created fresh whenever the output changes: pinning an engine that already ran to another
// device is fiddly, a new engine is not.
//
// The source node is 48 kHz stereo float whatever the device runs at; the main mixer converts to the device rate.

import AVFoundation
import AudioToolbox
import CoreAudio
import EPDJ3000Engine
import Foundation

enum AudioOutputError: LocalizedError, CustomStringConvertible {
    case noOutputUnit
    case badFormat
    case osStatus(String, OSStatus)

    var description: String {
        switch self {
        case .noOutputUnit: return "The audio engine has no output unit."
        case .badFormat: return "Could not create the 48 kHz stereo format."
        case let .osStatus(call, status): return "\(call) failed (OSStatus \(status))"
        }
    }

    var errorDescription: String? { description }
}

final class AudioOutput {
    private let engine = AVAudioEngine()
    private let sourceNode: AVAudioSourceNode
    private var observer: NSObjectProtocol?

    var isRunning: Bool { engine.isRunning }

    /// - Parameters:
    ///   - deviceID: nil = the system default output.
    ///   - onConfigurationChange: the engine stopped because the hardware changed (device unplugged, rate changed).
    ///     Called on an arbitrary thread.
    init(player: ScratchPlayerCore, deviceID: AudioDeviceID?, onConfigurationChange: @escaping @Sendable () -> Void) throws {
        guard let format = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: player.sampleRate,
                                         channels: 2, interleaved: false) else { throw AudioOutputError.badFormat }

        // Runs on the real-time thread: no allocation, no locks of ours, no Objective-C messaging. It captures only
        // the player, whose render path is allocation-free and only ever try-locks (see ParameterMailbox).
        sourceNode = AVAudioSourceNode(format: format) { _, _, frameCount, audioBufferList -> OSStatus in
            let buffers = UnsafeMutableAudioBufferListPointer(audioBufferList)
            let frames = Int(frameCount)
            if buffers.count >= 2, let left = buffers[0].mData, let right = buffers[1].mData {
                player.render(left: left.assumingMemoryBound(to: Float.self),
                              right: right.assumingMemoryBound(to: Float.self), frameCount: frames)
            } else if buffers.count == 1, let interleaved = buffers[0].mData {
                player.renderInterleaved(interleaved.assumingMemoryBound(to: Float.self), frameCount: frames)
            } else {
                for i in 0..<buffers.count {
                    if let data = buffers[i].mData { memset(data, 0, Int(buffers[i].mDataByteSize)) }
                }
            }
            return noErr
        }

        if let deviceID { try pin(to: deviceID) }
        engine.attach(sourceNode)
        engine.connect(sourceNode, to: engine.mainMixerNode, format: format)
        observer = NotificationCenter.default.addObserver(forName: .AVAudioEngineConfigurationChange, object: engine,
                                                          queue: nil) { _ in onConfigurationChange() }
    }

    deinit {
        stop()
    }

    func start() throws {
        engine.prepare()
        try engine.start()
    }

    func stop() {
        if let observer {
            NotificationCenter.default.removeObserver(observer)
            self.observer = nil
        }
        engine.stop()
    }

    /// Must happen before the engine is connected and started.
    private func pin(to deviceID: AudioDeviceID) throws {
        guard let unit = engine.outputNode.audioUnit else { throw AudioOutputError.noOutputUnit }
        var device = deviceID
        let status = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                          &device, UInt32(MemoryLayout<AudioDeviceID>.size))
        guard status == noErr else { throw AudioOutputError.osStatus("AudioUnitSetProperty(CurrentDevice)", status) }
    }
}
