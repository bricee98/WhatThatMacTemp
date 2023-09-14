// HotKeyHandler.mm
#include "GlobalVars.h"
#include "FunctionsAndVars.h"

#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import "GlobalHotkeySetup.h"
#include <iostream>

bool checkAccessibilityPermissions() {
    NSDictionary *options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
    bool accessibilityEnabled = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
    return accessibilityEnabled;
}

bool checkAudioPermissions() {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
        case AVAuthorizationStatusAuthorized:
            return true;
        case AVAuthorizationStatusNotDetermined:
            // The user has not yet been asked to grant permission. You can prompt them to ask for permission.
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                if (!granted) {
                    std::cerr << "Permission not granted.\n";
                }
            }];
            return false;
        case AVAuthorizationStatusRestricted:
        case AVAuthorizationStatusDenied:
            // The user has previously denied access. You might want to open the app settings using
            // [[UIApplication sharedApplication] openURL:[NSURL URLWithString:UIApplicationOpenSettingsURLString]];
            return false;
        default:
            return false;
    }
}
