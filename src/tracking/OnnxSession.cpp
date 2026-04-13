#include "tracking/OnnxSession.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <Windows.h>

#include <cpu_provider_factory.h>

#ifdef NOHCAM_HAS_DIRECTML
#include <dml_provider_factory.h>
#endif

namespace nohcam {

namespace {

std::vector<int64_t> ReadShape(const Ort::TypeInfo& type_info) {
    const auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
    return tensor_info.GetShape();
}

ONNXTensorElementDataType ReadElementType(const Ort::TypeInfo& type_info) {
    const auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
    return tensor_info.GetElementType();
}

std::string JoinStrings(const std::vector<std::string>& values) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << values[index];
    }
    return stream.str();
}

}  // namespace

OnnxSession::OnnxSession()
    : env_(ORT_LOGGING_LEVEL_WARNING, "NohCam"),
      session_options_{nullptr} {}

bool OnnxSession::LoadModel(const std::wstring& model_path, std::string* error_message) {
    Reset();
    metadata_.model_path = model_path;
    metadata_.available_providers = Ort::GetAvailableProviders();

    if (!std::filesystem::exists(model_path)) {
        const std::string message = "Model file not found: " + Narrow(model_path);
        metadata_.provider_error = message;
        if (error_message != nullptr) {
            *error_message = message;
        }
        return false;
    }

    // Skip DirectML for now - use CPU only to avoid crashes
    metadata_.used_cpu_fallback = true;
    return LoadWithCpu(model_path, error_message);
}

bool OnnxSession::IsLoaded() const {
    return session_.has_value();
}

const OnnxSession::SessionMetadata& OnnxSession::GetMetadata() const {
    return metadata_;
}

std::vector<OnnxSession::TensorData> OnnxSession::Run(const std::vector<TensorData>& inputs) {
    if (!session_.has_value()) {
        throw std::runtime_error("ONNX session has not been loaded.");
    }

    if (inputs.size() != metadata_.inputs.size()) {
        throw std::runtime_error("Input count does not match the loaded model.");
    }

    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<std::vector<float>> owned_input_buffers;
    std::vector<Ort::Value> input_values;
    owned_input_buffers.reserve(inputs.size());
    input_values.reserve(inputs.size());

    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const TensorData& input = inputs[index];
        const TensorMetadata& expected = metadata_.inputs[index];
        if (input.name != expected.name) {
            throw std::runtime_error("Input name mismatch for tensor " + expected.name + ".");
        }

        std::size_t expected_value_count = 1;
        for (const std::int64_t dimension : input.shape) {
            if (dimension <= 0) {
                throw std::runtime_error("All runtime input dimensions must be positive.");
            }
            expected_value_count *= static_cast<std::size_t>(dimension);
        }

        if (expected_value_count != input.values.size()) {
            throw std::runtime_error("Input value count does not match the supplied shape for tensor " + input.name + ".");
        }

        owned_input_buffers.push_back(input.values);
        std::vector<float>& owned_buffer = owned_input_buffers.back();
        input_values.push_back(Ort::Value::CreateTensor<float>(
            memory_info,
            owned_buffer.data(),
            owned_buffer.size(),
            input.shape.data(),
            input.shape.size()));
    }

    auto output_values = session_->Run(
        Ort::RunOptions{nullptr},
        input_name_ptrs_.data(),
        input_values.data(),
        input_values.size(),
        output_name_ptrs_.data(),
        output_name_ptrs_.size());

    std::vector<TensorData> outputs;
    outputs.reserve(output_values.size());
    for (std::size_t index = 0; index < output_values.size(); ++index) {
        const Ort::Value& output_value = output_values[index];
        if (!output_value.IsTensor()) {
            throw std::runtime_error("Output tensor " + metadata_.outputs[index].name + " is not a tensor.");
        }

        const auto tensor_info = output_value.GetTensorTypeAndShapeInfo();
        const auto element_type = tensor_info.GetElementType();
        const std::vector<int64_t> output_shape = tensor_info.GetShape();
        std::size_t value_count = tensor_info.GetElementCount();

        TensorData output;
        output.name = metadata_.outputs[index].name;
        output.shape = output_shape;
        output.values.resize(value_count);

        if (element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            const float* output_data = output_value.GetTensorData<float>();
            output.values.assign(output_data, output_data + value_count);
        } else if (element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
            const std::int64_t* output_data = output_value.GetTensorData<std::int64_t>();
            for (std::size_t i = 0; i < value_count; ++i) {
                output.values[i] = static_cast<float>(output_data[i]);
            }
        } else if (element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
            const std::int32_t* output_data = output_value.GetTensorData<std::int32_t>();
            for (std::size_t i = 0; i < value_count; ++i) {
                output.values[i] = static_cast<float>(output_data[i]);
            }
        } else {
            throw std::runtime_error("Unsupported output tensor type in OnnxSession.");
        }
        outputs.push_back(std::move(output));
    }

    return outputs;
}

const wchar_t* OnnxSession::GetDefaultModelDirectory() {
    return NOHCAM_ONNX_MODEL_DIR;
}

bool OnnxSession::TryLoadWithDirectMl(const std::wstring& model_path, std::string* error_message) {
#ifdef NOHCAM_HAS_DIRECTML
    try {
        session_options_ = Ort::SessionOptions{};
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        session_options_.SetIntraOpNumThreads(1);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(session_options_, 0));
        session_.emplace(env_, model_path.c_str(), session_options_);
        metadata_.execution_provider = ExecutionProvider::DirectML;
        metadata_.used_cpu_fallback = false;
        PopulateMetadata();
        return true;
    } catch (const Ort::Exception& exception) {
        metadata_.provider_error = DescribeException(exception);
        if (error_message != nullptr) {
            *error_message = metadata_.provider_error;
        }
        Reset();
        metadata_.model_path = model_path;
        metadata_.available_providers = Ort::GetAvailableProviders();
        metadata_.provider_error = DescribeException(exception);
    }
#else
    (void)model_path;
    (void)error_message;
#endif

    return false;
}

bool OnnxSession::LoadWithCpu(const std::wstring& model_path, std::string* error_message) {
    try {
        session_options_ = Ort::SessionOptions{};
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        session_options_.SetIntraOpNumThreads(1);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CPU(session_options_, 1));
        session_.emplace(env_, model_path.c_str(), session_options_);
        metadata_.execution_provider = ExecutionProvider::Cpu;
        PopulateMetadata();
        return true;
    } catch (const Ort::Exception& exception) {
        metadata_.provider_error = DescribeException(exception);
        if (error_message != nullptr) {
            *error_message = metadata_.provider_error;
        }
        Reset();
        metadata_.model_path = model_path;
        metadata_.available_providers = Ort::GetAvailableProviders();
        metadata_.provider_error = DescribeException(exception);
        return false;
    }
}

void OnnxSession::Reset() {
    session_.reset();
    session_options_ = Ort::SessionOptions{nullptr};
    metadata_ = SessionMetadata{};
    input_name_ptrs_.clear();
    output_name_ptrs_.clear();
}

void OnnxSession::PopulateMetadata() {
    if (!session_.has_value()) {
        return;
    }

    metadata_.inputs.clear();
    metadata_.outputs.clear();
    input_name_ptrs_.clear();
    output_name_ptrs_.clear();

    Ort::AllocatorWithDefaultOptions allocator;
    const std::size_t input_count = session_->GetInputCount();
    const std::size_t output_count = session_->GetOutputCount();

    metadata_.inputs.reserve(input_count);
    for (std::size_t index = 0; index < input_count; ++index) {
        auto name = session_->GetInputNameAllocated(index, allocator);
        const Ort::TypeInfo type_info = session_->GetInputTypeInfo(index);

        TensorMetadata tensor;
        tensor.name = name.get();
        tensor.element_type = ReadElementType(type_info);
        tensor.shape = ReadShape(type_info);
        
        std::string shape_str = "[";
        for (auto s : tensor.shape) shape_str += std::to_string(s) + ",";
        shape_str += "]";
        spdlog::info("OnnxSession: Input {} name: '{}', shape: {}", index, tensor.name, shape_str);
        
        metadata_.inputs.push_back(std::move(tensor));
    }

    metadata_.outputs.reserve(output_count);
    for (std::size_t index = 0; index < output_count; ++index) {
        auto name = session_->GetOutputNameAllocated(index, allocator);
        const Ort::TypeInfo type_info = session_->GetOutputTypeInfo(index);

        TensorMetadata tensor;
        tensor.name = name.get();
        tensor.element_type = ReadElementType(type_info);
        tensor.shape = ReadShape(type_info);

        std::string shape_str = "[";
        for (auto s : tensor.shape) shape_str += std::to_string(s) + ",";
        shape_str += "]";
        spdlog::info("OnnxSession: Output {} name: '{}', shape: {}", index, tensor.name, shape_str);

        metadata_.outputs.push_back(std::move(tensor));
    }

    input_name_ptrs_.reserve(metadata_.inputs.size());
    for (const TensorMetadata& input : metadata_.inputs) {
        input_name_ptrs_.push_back(input.name.c_str());
    }

    output_name_ptrs_.reserve(metadata_.outputs.size());
    for (const TensorMetadata& output : metadata_.outputs) {
        output_name_ptrs_.push_back(output.name.c_str());
    }
}

std::string OnnxSession::Narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size_needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    std::string result(size_needed, '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        size_needed,
        nullptr,
        nullptr);
    return result;
}

std::string OnnxSession::DescribeException(const Ort::Exception& exception) {
    std::ostringstream stream;
    stream << exception.what() << " (ORT code " << exception.GetOrtErrorCode() << ")";
    const std::vector<std::string> providers = Ort::GetAvailableProviders();
    if (!providers.empty()) {
        stream << "; available providers: " << JoinStrings(providers);
    }
    return stream.str();
}

}  // namespace nohcam
