//
//  main.cpp
//  AudioGetter
//
//  Created by Brice Edelman on 9/8/23.
//

#include <iostream>
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include "GlobalHotkeySetup.h"
#include <sndfile.hh>
#include "GlobalVars.h"
#include "FunctionsAndVars.h"
#include <ApplicationServices/ApplicationServices.h>
#include <mutex>

std::mutex renderMutex;

struct MyContextStruct {
    AudioUnit audioUnit;
    RingBuffer &ringBuffer;
    AudioStreamBasicDescription streamFormat;
};

// ... in some initialization function or at the beginning of your program
void setupGlobalStreamFormat() {
    globalStreamFormat.mSampleRate = 44100;  // Adjust as necessary; common values are 44100 or 48000 Hz
    globalStreamFormat.mFormatID = kAudioFormatLinearPCM;
    globalStreamFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    globalStreamFormat.mBitsPerChannel = 32;
    globalStreamFormat.mChannelsPerFrame = 2;  // Stereo; set to 1 for mono
    globalStreamFormat.mFramesPerPacket = 1;
    globalStreamFormat.mBytesPerFrame = (globalStreamFormat.mBitsPerChannel / 8) * globalStreamFormat.mChannelsPerFrame;
    globalStreamFormat.mBytesPerPacket = globalStreamFormat.mBytesPerFrame * globalStreamFormat.mFramesPerPacket;
}

RingBuffer globalRingBuffer(882000); // define global variables here
AudioStreamBasicDescription globalStreamFormat; // define global variable here

OSStatus currStatus;
UInt32 dataSize = sizeof(currStatus);

AudioDeviceID getDefaultOutputDevice() {
    AudioDeviceID outputDeviceID = kAudioObjectUnknown;

    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementWildcard
    };

    UInt32 dataSize = sizeof(AudioDeviceID);
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, nullptr, &dataSize, &outputDeviceID);
    if (status != kAudioHardwareNoError) {
        std::cerr << "Error getting default output device: " << status << std::endl;
    }

    return outputDeviceID;
}


AudioDeviceID getBlackHoleDevice() {
    AudioDeviceID deviceID = kAudioObjectUnknown;

    AudioObjectPropertyAddress propertyAddress = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };

    UInt32 dataSize = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize);
    if (status != noErr) {
        return deviceID;
    }

    UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);
    AudioDeviceID* audioDevices = new AudioDeviceID[deviceCount];

    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize, audioDevices);
    if (status != noErr) {
        delete[] audioDevices;
        return deviceID;
    }

    for (UInt32 i = 0; i < deviceCount; ++i) {
        CFStringRef deviceName = NULL;
        dataSize = sizeof(deviceName);
        propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
        status = AudioObjectGetPropertyData(audioDevices[i], &propertyAddress, 0, NULL, &dataSize, &deviceName);
        if (status == noErr && deviceName != NULL) {
            char name[64];
            CFStringGetCString(deviceName, name, sizeof(name), kCFStringEncodingASCII);
            CFRelease(deviceName);

            if (strstr(name, "BlackHole") != NULL) {
                deviceID = audioDevices[i];
                break;
            }
        }
    }

    delete[] audioDevices;
    return deviceID;
}



OSStatus MyAURenderCallback(
     void *inRefCon,
     AudioUnitRenderActionFlags *ioActionFlags,
     const AudioTimeStamp *inTimeStamp,
     UInt32 inBusNumber,
     UInt32 inNumberFrames,
     AudioBufferList *ioData
) {
    std::lock_guard<std::mutex> lock(renderMutex);
    if (*ioActionFlags & kAudioUnitRenderAction_PostRender) {
        auto context = static_cast<MyContextStruct*>(inRefCon);

        if (ioData->mNumberBuffers > 0 && ioData->mBuffers[0].mData != nullptr) {
            float* data = (float*)ioData->mBuffers[0].mData;
            size_t numSamples = ioData->mBuffers[0].mDataByteSize / sizeof(float);

            // Ensure that we have at least 5 samples to print
            size_t samplesToPrint = std::min<size_t>(5, numSamples);

            for (size_t i = 0; i < samplesToPrint; ++i) {
                printf("Sample %zu: %f\n", i, data[i]);
            }

            // Write data to ring buffer
            context->ringBuffer.write(data, numSamples * sizeof(float));

        }
    }

    return noErr;
}


CGEventRef myCGEventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon) {
    if (type == kCGEventKeyDown) {
        // Get the current modifiers and the keycode of the pressed key
        CGEventFlags flags = CGEventGetFlags(event);
        CGKeyCode keyCode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

        // Check if Ctrl+Command+C was pressed; using 8 as the virtual keycode for "C"
        if (keyCode == 8 &&
            (flags & kCGEventFlagMaskControl) &&
            (flags & kCGEventFlagMaskCommand))
        {
            std::cout << "Ctrl+Command+C detected" << std::endl;
            writeRingBufferToFile();
        }
    }
    return event;
}

void writeRingBufferToFile() {
    // Configure sndfile settings based on the stream format
    SF_INFO sfInfo;
    memset(&sfInfo, 0, sizeof(sfInfo));
    sfInfo.samplerate = globalStreamFormat.mSampleRate;
    sfInfo.channels = globalStreamFormat.mChannelsPerFrame;
    sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    // Open the WAV file
    SndfileHandle sndFile = SndfileHandle("audio_staged.wav", SFM_WRITE, sfInfo.format, sfInfo.channels, sfInfo.samplerate);
    if (!sndFile) {
        std::cerr << "Error opening audio_staged.wav" << std::endl;
        return;
    }

    // Create a buffer to hold the audio data
    std::vector<float> buffer(globalStreamFormat.mSampleRate * globalStreamFormat.mChannelsPerFrame * 10); // Buffer size to hold 10 seconds of audio

    // Read data from the ring buffer and write it to the file in chunks
    globalRingBuffer.read(buffer.data(), buffer.size());

    
    // Log the first few sample values
    for (size_t i = 0; i < std::min<size_t>(10, buffer.size()); ++i) {
        std::cout << "Sample " << i << ": " << buffer[i] << std::endl;
    }

    sf_count_t count = sndFile.writef(buffer.data(), buffer.size() / globalStreamFormat.mChannelsPerFrame);
    
    if (count <= 0) {
        std::cerr << "Failed to write data to file" << std::endl;
    } else {
        std::cout << "Successfully wrote " << count << " frames to file." << std::endl;
    }

    // Close the file (this happens automatically when sndFile goes out of scope)
}





int main(int argc, const char * argv[]) {
    setupGlobalStreamFormat();
    if (!checkAccessibilityPermissions()) {
        // Inform the user that your app requires accessibility permissions to function correctly
        std::cerr <<  "Please enable accessibility permissions for this app.";
    }
    
    if (!checkAudioPermissions()) {
        // Inform the user that your app requires accessibility permissions to function correctly
        std::cerr <<  "Please enable audio permissions for this app.";
    }
    AudioDeviceID outputDevice = getDefaultOutputDevice();
    if (outputDevice != kAudioObjectUnknown) {
        std::cout << "Successfully obtained device.\n";
    } else {
        std::cout << "Failed to obtain device.\n";
    }
    
    AudioStreamBasicDescription deviceStreamFormat;
    AudioObjectPropertyAddress propertyAddress = {
        kAudioDevicePropertyStreamFormat,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMaster
    };


    UInt32 propertySize = sizeof(AudioStreamBasicDescription);
    OSStatus status = AudioObjectGetPropertyData(outputDevice, &propertyAddress, 0, NULL, &propertySize, &deviceStreamFormat);
    if (status != noErr) {
        std::cerr << "Error getting device stream format: " << status << "\n";
        return -1;
    }

    bool isMatch = (deviceStreamFormat.mSampleRate == globalStreamFormat.mSampleRate) &&
                   (deviceStreamFormat.mFormatID == globalStreamFormat.mFormatID) &&
                   (deviceStreamFormat.mFormatFlags == globalStreamFormat.mFormatFlags) &&
                   (deviceStreamFormat.mBytesPerPacket == globalStreamFormat.mBytesPerPacket) &&
                   (deviceStreamFormat.mFramesPerPacket == globalStreamFormat.mFramesPerPacket) &&
                   (deviceStreamFormat.mBytesPerFrame == globalStreamFormat.mBytesPerFrame) &&
                   (deviceStreamFormat.mChannelsPerFrame == globalStreamFormat.mChannelsPerFrame) &&
                   (deviceStreamFormat.mBitsPerChannel == globalStreamFormat.mBitsPerChannel) &&
                   (deviceStreamFormat.mReserved == globalStreamFormat.mReserved);

    if (isMatch) {
        std::cout << "The stream formats match.\n";
    } else {
        std::cout << "The stream formats do not match.\n";
    }



    // Now deviceStreamFormat holds the current stream format of the device

    
    AudioComponentDescription desc = {0};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (component == NULL) {
        std::cerr << "Can't find a component\n";
        return -1;
    }

    AudioUnit audioUnit;
    status = AudioComponentInstanceNew(component, &audioUnit);
    if (status != noErr) {
        std::cerr << "Error creating audio unit: " << status << "\n";
        return -1;
    }

    status = AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &globalStreamFormat, sizeof(globalStreamFormat));
    if (status != noErr) {
        std::cerr << "Error setting stream format: " << status << "\n";
        return -1;
    }

    UInt32 isInput = 1;
    // Instead of setting the current device property, set the InputScope to receive audio data
    status = AudioUnitSetProperty(audioUnit,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Input,
                                  1, // 1 for input
                                  &isInput, // should be a variable set to 1 (true)
                                  sizeof(isInput));

    if (status != noErr) {
        std::cerr << "Error enabling IO for input: " << status << "\n";
        return -1;
    }


    // Your existing setup code here...

    // Create a context struct
    MyContextStruct context = { audioUnit, globalRingBuffer, globalStreamFormat };

    // Create a callback struct and set the callback function and context struct
    AURenderCallbackStruct callbackStruct = {0};
    callbackStruct.inputProc = MyAURenderCallback;
    callbackStruct.inputProcRefCon = &context;

    // Add render notify instead of setting a render callback
    status = AudioUnitAddRenderNotify(audioUnit, MyAURenderCallback, &context);
    if (status != noErr) {
        std::cerr << "Error setting render notify: " << status << "\n";
        return -1;
    }

    // Your existing initialization and start code here...



    // Initialize the audio unit
    status = AudioUnitInitialize(audioUnit);
    if (status != noErr) {
        std::cerr << "Error initializing audio unit: " << status << "\n";
        return -1;
    }

    // Start the audio unit
    status = AudioOutputUnitStart(audioUnit);
    if (status != noErr) {
        std::cerr << "Error starting audio unit: " << status << "\n";
        return -1;
    }

    // Check the current status of the audio unit
    UInt32 currStatus;
    UInt32 dataSize = sizeof(currStatus);
    status = AudioUnitGetProperty(audioUnit, kAudioUnitProperty_LastRenderError, kAudioUnitScope_Input, 0, &currStatus, &dataSize);

    if (currStatus != noErr) {
        std::cerr << "Current audio unit error status: " << currStatus << "\n";
    } else {
        std::cout << "Audio unit status: OK\n";
    }




    std::thread keyMonitoringThread([]{
        CGEventMask eventMask = (1 << kCGEventKeyDown);
        CFMachPortRef eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, eventMask, myCGEventCallback, NULL);

        if (!eventTap) {
            std::cerr << "Failed to create event tap" << std::endl;
            exit(1);
        }

        CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);
        CGEventTapEnable(eventTap, true);
        
        CFRunLoopRun();
    });

    // Detach the thread as we don't need to join it later
    //keyMonitoringThread.detach();
    // Keep the program running indefinitely, using a run loop.
    CFRunLoopRun();

    // Clean up and stop the audio unit before exiting (if ever needed)
    status = AudioOutputUnitStop(audioUnit);
    if (status != noErr) {
        std::cerr << "Error stopping audio unit: " << status << "\n";
    }

    status = AudioUnitUninitialize(audioUnit);
    if (status != noErr) {
        std::cerr << "Error uninitializing audio unit: " << status << "\n";
    }

    return 0;
}
