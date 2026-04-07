#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace nohcam {

class OnnxSession {
public:
    enum class ExecutionProvider {
        Cpu,
        DirectML,
    };

    struct TensorMetadata {
        std::string name;
        ONNXTensorElementDataType element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
        std::vector<int64_t> shape;
    };

    struct TensorData {
        std::string name;
        std::vector<int64_t> shape;
        std::vector<float> values;
    };

    struct SessionMetadata {
        std::wstring model_path;
        ExecutionProvider execution_provider = ExecutionProvider::Cpu;
        bool used_cpu_fallback = false;
        std::string provider_error;
        std::vector<std::string> available_providers;
        std::vector<TensorMetadata> inputs;
        std::vector<TensorMetadata> outputs;
    };

    OnnxSession();
    ~OnnxSession() = default;

    OnnxSession(const OnnxSession&) = delete;
    OnnxSession& operator=(const OnnxSession&) = delete;

    bool LoadModel(const std::wstring& model_path, std::string* error_message = nullptr);
    bool IsLoaded() const;

    const SessionMetadata& GetMetadata() const;
    std::vector<TensorData> Run(const std::vector<TensorData>& inputs);

    static const wchar_t* GetDefaultModelDirectory();

private:
    bool TryLoadWithDirectMl(const std::wstring& model_path, std::string* error_message);
    bool LoadWithCpu(const std::wstring& model_path, std::string* error_message);
    void Reset();
    void PopulateMetadata();
    static std::string Narrow(const std::wstring& value);
    static std::string DescribeException(const Ort::Exception& exception);

    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::optional<Ort::Session> session_;
    SessionMetadata metadata_;
    std::vector<const char*> input_name_ptrs_;
    std::vector<const char*> output_name_ptrs_;
};

}  // namespace nohcam
