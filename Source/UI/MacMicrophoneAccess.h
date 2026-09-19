#pragma once
#include <functional>

// macOS: System Sound prefs can read the mic without app permission.
// This app gets silence until TCC microphone access is granted.
void requestMacMicrophoneAccess(std::function<void(bool granted)> onResult = {});
void openMacMicrophonePrivacySettings();
