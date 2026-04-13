#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

#include "tracking/OnnxSession.h"

void PrintMetadata(const std::wstring& model_name) {
    std::wstring model_path = std::filesystem::path(nohcam::OnnxSession::GetDefaultModelDirectory()) / model_name;
    nohcam::OnnxSession session;
    std::string error;
    if (!session.LoadModel(model_path, &error)) {
        std::cerr << "Failed to load " << std::filesystem::path(model_name).string() << ": " << error << std::endl;
        return;
    }
    const auto& meta = session.GetMetadata();
    std::cout << "Model: " << std::filesystem::path(model_name).string() << std::endl;
    for (const auto& in : meta.inputs) {
        std::cout << "  Input: " << in.name << " [";
        for (auto s : in.shape) std::cout << s << ",";
        std::cout << "]" << std::endl;
    }
    for (const auto& out : meta.outputs) {
        std::cout << "  Output: " << out.name << " [";
        for (auto s : out.shape) std::cout << s << ",";
        std::cout << "]" << std::endl;
    }
}

int main() {
    PrintMetadata(L"face_landmarks.onnx");
    PrintMetadata(L"face_blendshapes.onnx");
    PrintMetadata(L"palm_detection.onnx");
    PrintMetadata(L"palm_detection_full_Nx3x192x192_post.onnx");
    PrintMetadata(L"hand_landmarks.onnx");
    PrintMetadata(L"hand_landmark_sparse_Nx3x224x224.onnx");
    return 0;
}
