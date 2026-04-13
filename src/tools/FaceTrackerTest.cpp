#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

#include "tracking/FaceTracker.h"
#include "capture/CameraCapture.h"

int main() {
    nohcam::CameraCapture camera;

    nohcam::FaceTracker tracker;
    std::string error_message;
    if (!tracker.Initialize(&error_message)) {
        std::cerr << "FaceTracker initialization failed: " << error_message << std::endl;
        return 1;
    }

    std::cout << "FaceTracker initialized successfully." << std::endl;

    if (!camera.Start(0)) {
        std::cerr << "Failed to start camera capture." << std::endl;
        return 1;
    }

    std::cout << "Camera capture started. Waiting for frame..." << std::endl;

    for (int attempt = 0; attempt < 100; ++attempt) {
        auto frame = camera.GetLatestFrame();
        if (!frame.valid) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::cout << "Got frame: " << frame.width << "x" << frame.height << std::endl;

        auto result = tracker.Track(frame);

        if (result.detected) {
            std::cout << "Face detected!" << std::endl;
            std::cout << "Center: (" << result.x << ", " << result.y << ")" << std::endl;
            std::cout << "Yaw: " << result.yaw << ", Pitch: " << result.pitch << ", Roll: " << result.roll << std::endl;
            std::cout << "Landmarks: " << result.landmarks.size() << std::endl;
            std::cout << "Blendshapes: " << result.blendshapes.size() << std::endl;
            
            if (result.blendshapes.size() == 52) {
                std::cout << "Blendshape count is correct (52)." << std::endl;
            } else {
                std::cerr << "Unexpected blendshape count: " << result.blendshapes.size() << std::endl;
            }
            
            camera.Stop();
            std::cout << "FaceTracker test passed." << std::endl;
            return 0;
        } else {
            std::cout << "No face detected in frame " << attempt + 1 << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    camera.Stop();
    std::cerr << "No face detected after 100 attempts." << std::endl;
    return 1;
}
