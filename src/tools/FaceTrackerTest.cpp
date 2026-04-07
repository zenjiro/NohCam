#include <iostream>
#include <vector>
#include <string>

#include "tracking/FaceTracker.h"
#include "capture/CameraCapture.h"

int main() {
    nohcam::FaceTracker tracker;
    std::string error_message;
    if (!tracker.Initialize(&error_message)) {
        std::cerr << "FaceTracker initialization failed: " << error_message << std::endl;
        return 1;
    }

    std::cout << "FaceTracker initialized successfully." << std::endl;

    // Create a dummy capture frame (192x192, RGBA)
    nohcam::CameraCapture::CaptureFrame frame;
    frame.valid = true;
    frame.width = 192;
    frame.height = 192;
    frame.stride = 192 * 4;
    frame.pixels.assign(frame.stride * frame.height, 128); // Grey image

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
            return 1;
        }
    } else {
        std::cerr << "Face NOT detected (using dummy models)." << std::endl;
        return 1;
    }

    std::cout << "FaceTracker test passed." << std::endl;
    return 0;
}
