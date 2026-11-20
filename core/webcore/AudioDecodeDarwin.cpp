// the formats WebKit's own reader cannot take on the phone: AudioToolbox opens a file from memory through
// callbacks and converts it to 32 bit float with ExtAudioFile, both in the SDK since ios 2

#include "YozoraAudioDecode.h"

#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <cstring>

namespace Yozora {

namespace {

struct MemoryFile {
    std::span<const uint8_t> bytes;
};

OSStatus readFile(void* context, SInt64 position, UInt32 requested, void* buffer, UInt32* actual)
{
    auto& file = *static_cast<MemoryFile*>(context);
    if (position < 0 || static_cast<uint64_t>(position) >= file.bytes.size()) {
        *actual = 0;
        return noErr;
    }
    size_t count = std::min<size_t>(requested, file.bytes.size() - static_cast<size_t>(position));
    std::memcpy(buffer, file.bytes.data() + position, count);
    *actual = static_cast<UInt32>(count);
    return noErr;
}

SInt64 fileSize(void* context)
{
    return static_cast<SInt64>(static_cast<MemoryFile*>(context)->bytes.size());
}

}

DecodeStatus decodeWithAudioToolbox(std::span<const uint8_t> data, DecodedAudio& out)
{
    out = { };
    MemoryFile file { data };
    AudioFileID audioFile = nullptr;
    if (AudioFileOpenWithCallbacks(&file, readFile, nullptr, fileSize, nullptr, 0, &audioFile) != noErr)
        return DecodeStatus::Unsupported;
    ExtAudioFileRef extFile = nullptr;
    DecodeStatus status = DecodeStatus::Unsupported;
    if (ExtAudioFileWrapAudioFileID(audioFile, false, &extFile) == noErr) {
        AudioStreamBasicDescription source = { };
        UInt32 size = sizeof(source);
        SInt64 length = 0;
        UInt32 lengthSize = sizeof(length);
        if (ExtAudioFileGetProperty(extFile, kExtAudioFileProperty_FileDataFormat, &size, &source) == noErr
            && ExtAudioFileGetProperty(extFile, kExtAudioFileProperty_FileLengthFrames, &lengthSize, &length) == noErr
            && source.mChannelsPerFrame && source.mChannelsPerFrame <= 2 && source.mSampleRate >= 3000 && length > 0) {
            if (static_cast<uint64_t>(length) > maxDecodedSamples / source.mChannelsPerFrame)
                status = DecodeStatus::TooLarge;
            else {
                // interleaved float at the rate of the file: the resampling is left to the caller, which does it for every format alike
                AudioStreamBasicDescription client = { };
                client.mSampleRate = source.mSampleRate;
                client.mFormatID = kAudioFormatLinearPCM;
                client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
                client.mChannelsPerFrame = source.mChannelsPerFrame;
                client.mBitsPerChannel = 32;
                client.mFramesPerPacket = 1;
                client.mBytesPerFrame = 4 * client.mChannelsPerFrame;
                client.mBytesPerPacket = client.mBytesPerFrame;
                if (ExtAudioFileSetProperty(extFile, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client) == noErr) {
                    out.sampleRate = static_cast<float>(source.mSampleRate);
                    out.channels.assign(source.mChannelsPerFrame, std::vector<float>());
                    std::vector<float> block(4096 * source.mChannelsPerFrame);
                    status = DecodeStatus::Ok;
                    // a file can state a length shorter than what it holds: the loop reads until the decoder has no more, within the cap
                    for (;;) {
                        AudioBufferList list;
                        list.mNumberBuffers = 1;
                        list.mBuffers[0].mNumberChannels = source.mChannelsPerFrame;
                        list.mBuffers[0].mDataByteSize = static_cast<UInt32>(block.size() * sizeof(float));
                        list.mBuffers[0].mData = block.data();
                        UInt32 frames = 4096;
                        if (ExtAudioFileRead(extFile, &frames, &list) != noErr) {
                            status = DecodeStatus::Malformed;
                            break;
                        }
                        if (!frames)
                            break;
                        if ((out.channels[0].size() + frames) * source.mChannelsPerFrame > maxDecodedSamples) {
                            status = DecodeStatus::TooLarge;
                            break;
                        }
                        for (UInt32 frame = 0; frame < frames; ++frame) {
                            for (UInt32 channel = 0; channel < source.mChannelsPerFrame; ++channel)
                                out.channels[channel].push_back(block[frame * source.mChannelsPerFrame + channel]);
                        }
                    }
                    if (status == DecodeStatus::Ok && out.channels[0].empty())
                        status = DecodeStatus::Malformed;
                }
            }
        }
        ExtAudioFileDispose(extFile);
    }
    AudioFileClose(audioFile);
    if (status != DecodeStatus::Ok)
        out = { };
    return status;
}

}
