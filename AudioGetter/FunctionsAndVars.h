#ifndef FunctionsAndVars_h
#define FunctionsAndVars_h

#include <CoreAudio/CoreAudio.h>
#include "GlobalVars.h"
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>


void listAudioDevices();
AudioDeviceID getDefaultOutputDevice();
OSStatus MyAURenderCallback(
    void *inRefCon,
    AudioUnitRenderActionFlags *ioActionFlags,
    const AudioTimeStamp *inTimeStamp,
    UInt32 inBusNumber,
    UInt32 inNumberFrames,
    AudioBufferList *ioData
);
void writeRingBufferToFile();

#endif /* FunctionsAndVars_h */
