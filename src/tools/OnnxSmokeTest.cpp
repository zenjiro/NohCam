#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "tracking/OnnxSession.h"

namespace {

std::wstring ResolveModelPath(int argc, char** argv) {
    if (argc > 1) {
        return std::filesystem::path(argv[1]).wstring();
    }

    return std::filesystem::path(nohcam::OnnxSession::GetDefaultModelDirectory() ) / L"smoke_add.onnx";
}

const char* ProviderName(nohcam::OnnxSession::ExecutionProvider provider) {
    switch (provider) {
    case nohcam::OnnxSession::ExecutionProvider::DirectML:
        return "DirectML";
    case nohcam::OnnxSession::ExecutionProvider::Cpu:
    default:
        return "CPU";
    }
}

bool NearlyEqual(float lhs, float rhs) {
    return std::fabs(lhs - rhs) < 0.0001f;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::wstring model_path = ResolveModelPath(argc, argv);
        nohcam::OnnxSession session;
        std::string error_message;
        if (!session.LoadModel(model_path, &error_message)) {
            std::cerr << "Failed to load model: " << error_message << '\n';
            return 1;
        }

        const nohcam::OnnxSession::SessionMetadata& metadata = session.GetMetadata();
        std::cout << "Loaded model: " << std::filesystem::path(metadata.model_path).string() << '\n';
        std::cout << "Execution provider: " << ProviderName(metadata.execution_provider);
        if (metadata.used_cpu_fallback) {
            std::cout << " (fallback)";
        }
        std::cout << '\n';

        if (!metadata.provider_error.empty()) {
            std::cout << "Provider note: " << metadata.provider_error << '\n';
        }

        if (metadata.inputs.size() != 1 || metadata.outputs.size() != 1) {
            std::cerr << "Smoke test expects a one-input / one-output model.\n";
            return 1;
        }

        nohcam::OnnxSession::TensorData input;
        input.name = metadata.inputs.front().name;
        input.shape = {1, 3};
        input.values = {1.0f, 2.5f, -3.0f};

        const auto outputs = session.Run({input});
        if (outputs.size() != 1) {
            std::cerr << "Expected a single output tensor.\n";
            return 1;
        }

        const std::vector<float> expected = {2.0f, 3.5f, -2.0f};
        const auto& output = outputs.front();
        if (output.values.size() != expected.size()) {
            std::cerr << "Unexpected output size: " << output.values.size() << '\n';
            return 1;
        }

        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (!NearlyEqual(output.values[index], expected[index])) {
                std::cerr << "Mismatch at index " << index << ": expected " << expected[index]
                          << ", got " << output.values[index] << '\n';
                return 1;
            }
        }

        std::cout << "Smoke test inference passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Unhandled exception: " << exception.what() << '\n';
        return 1;
    }
}
