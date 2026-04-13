#include <chrono>
#include <iostream>
#include <thread>

#include "capture/CameraCapture.h"
#include "tracking/TrackingTracker.h"

int main() {
    nohcam::CameraCapture camera;
    if (!camera.Initialize()) {
        std::cerr << "Camera initialization failed." << std::endl;
        return 1;
    }

    nohcam::TrackingTracker tracker;
    std::string error_message;
    if (!tracker.Initialize(&error_message)) {
        std::cerr << "TrackingTracker initialization failed: " << error_message << std::endl;
        return 1;
    }

    if (!camera.StartDefaultDevice()) {
        std::cerr << "Failed to start camera capture." << std::endl;
        return 1;
    }

    for (int attempt = 0; attempt < 120; ++attempt) {
        const auto frame = camera.GetLatestCaptureFrame();
        if (!frame.has_value() || !frame->valid) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        const auto result = tracker.Track(*frame);
        std::cout << "Frame " << attempt + 1
                  << " face=" << (result.face.detected ? "Y" : "N")
                  << " left=" << (result.left_hand.detected ? "Y" : "N")
                  << " right=" << (result.right_hand.detected ? "Y" : "N")
                  << std::endl;

        if (result.left_hand.detected || result.right_hand.detected) {
            camera.Shutdown();
            std::cout << "TrackingTracker test passed." << std::endl;
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    camera.Shutdown();
    std::cerr << "No hands detected after 120 attempts." << std::endl;
    return 1;
}
