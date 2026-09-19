#import "MacMicrophoneAccess.h"

#if defined(__APPLE__)
 #import <AVFoundation/AVFoundation.h>
 #import <AppKit/AppKit.h>
 #import <dispatch/dispatch.h>
#endif

void requestMacMicrophoneAccess(std::function<void(bool granted)> onResult)
{
#if defined(__APPLE__)
    if (@available(macOS 10.14, *))
    {
        const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        if (status == AVAuthorizationStatusAuthorized)
        {
            if (onResult) onResult(true);
            return;
        }
        if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted)
        {
            if (onResult) onResult(false);
            return;
        }

        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                  completionHandler:^(BOOL granted)
        {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (onResult) onResult((bool) granted);
            });
        }];
        return;
    }
#endif
    if (onResult) onResult(true);
}

void openMacMicrophonePrivacySettings()
{
#if defined(__APPLE__)
    NSArray<NSString*>* urls = @[
        @"x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_Microphone",
        @"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"
    ];
    for (NSString* s in urls)
    {
        NSURL* url = [NSURL URLWithString:s];
        if (url != nil && [[NSWorkspace sharedWorkspace] openURL:url])
            return;
    }
#endif
}
