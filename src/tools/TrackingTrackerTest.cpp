#include <chrono>
#include <iostream>
#include <thread>

#include "capture/CameraCapture.h"
#include "tracking/TrackingTracker.h"

int main() {
    nohcam::CameraCapture camera;

    nohcam::TrackingTracker tracker;
    std::string error_message;
    if (!tracker.Initialize(&error_message)) {
        std::cerr << "TrackingTracker initialization failed: " << error_message << std::endl;
        return 1;
    }

    if (!camera.Start("assets/test_videos/9019476-uhd_2160_3840_24fps.mp4")) {
        std::cerr << "Failed to open video file." << std::endl;
        return 1;
    }

    for (int attempt = 0; attempt < 120; ++attempt) {
        const auto frame = camera.GetLatestFrame();
        if (!frame.valid) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        const auto result = tracker.Track(frame);
        std::cout << "Frame " << attempt + 1
                  << " face=" << (result.face.detected ? "Y" : "N")
                  << " pose=" << (result.pose.detected ? "Y" : "N")
                  << " score=" << result.pose.score
                  << " left=" << (result.left_hand.detected ? "Y" : "N")
                  << " right=" << (result.right_hand.detected ? "Y" : "N")
                  << std::endl;

        if (result.pose.detected) {
            camera.Stop();
            std::cout << "TrackingTracker test passed (Pose detected)." << std::endl;
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    camera.Stop();
    std::cerr << "No hands detected after 120 attempts." << std::endl;
    return 1;
}
