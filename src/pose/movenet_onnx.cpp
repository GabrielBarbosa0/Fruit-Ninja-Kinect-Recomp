#include "fnk/pose/movenet_onnx.hpp"

#include "fnk/nui/movenet_adapter.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace fnk::pose {
namespace {

struct Letterbox {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
};

Letterbox calculate_letterbox(std::uint32_t source_width, std::uint32_t source_height,
                              std::uint32_t target_width, std::uint32_t target_height) {
  const double scale = std::min(static_cast<double>(target_width) / source_width,
                                static_cast<double>(target_height) / source_height);
  const auto width = std::max(1U, static_cast<std::uint32_t>(std::lround(source_width * scale)));
  const auto height =
      std::max(1U, static_cast<std::uint32_t>(std::lround(source_height * scale)));
  return {(target_width - width) / 2U, (target_height - height) / 2U, width, height};
}

template <typename T>
std::vector<T> make_input(const camera::CameraFrame& frame, std::uint32_t target_width,
                          std::uint32_t target_height, const Letterbox& box) {
  std::vector<T> result(static_cast<std::size_t>(target_width) * target_height * 3U, T{});
  for (std::uint32_t y = 0; y < box.height; ++y) {
    const std::uint32_t source_y = std::min(
        frame.height - 1U, static_cast<std::uint32_t>((static_cast<std::uint64_t>(y) *
                                                       frame.height) /
                                                      box.height));
    for (std::uint32_t x = 0; x < box.width; ++x) {
      const std::uint32_t source_x = std::min(
          frame.width - 1U, static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) *
                                                        frame.width) /
                                                       box.width));
      const std::size_t source =
          (static_cast<std::size_t>(source_y) * frame.width + source_x) * 4U;
      const std::size_t target =
          (static_cast<std::size_t>(y + box.y) * target_width + x + box.x) * 3U;
      result[target] = static_cast<T>(frame.bgra[source + 2U]);
      result[target + 1U] = static_cast<T>(frame.bgra[source + 1U]);
      result[target + 2U] = static_cast<T>(frame.bgra[source]);
    }
  }
  return result;
}

}  // namespace

struct MoveNetOnnx::Impl {
  Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "fnk_pose"};
  Ort::SessionOptions session_options;
  Ort::Session session{nullptr};
  Ort::AllocatorWithDefaultOptions allocator;
  std::string input_name;
  std::string output_name;
  ONNXTensorElementDataType input_type{ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED};
  std::uint32_t input_width{};
  std::uint32_t input_height{};
  bool initialized{};
  std::string error;
  PoseInferenceStats statistics;

  explicit Impl(const std::filesystem::path& model_path) {
    try {
      session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
      session_options.SetIntraOpNumThreads(2);
      session = Ort::Session(environment, model_path.c_str(), session_options);
      if (session.GetInputCount() != 1 || session.GetOutputCount() < 1) {
        error = "MoveNet model must expose one input and at least one output.";
        return;
      }

      auto allocated_input = session.GetInputNameAllocated(0, allocator);
      auto allocated_output = session.GetOutputNameAllocated(0, allocator);
      input_name = allocated_input.get();
      output_name = allocated_output.get();

      const auto input_info = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
      input_type = input_info.GetElementType();
      const auto shape = input_info.GetShape();
      if (shape.size() != 4 || shape[0] != 1 || shape[1] <= 0 || shape[2] <= 0 ||
          shape[3] != 3) {
        error = "MoveNet input must have static NHWC shape [1,H,W,3].";
        return;
      }
      if (input_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8 &&
          input_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 &&
          input_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        error = "MoveNet input type must be uint8, int32, or float32.";
        return;
      }
      input_height = static_cast<std::uint32_t>(shape[1]);
      input_width = static_cast<std::uint32_t>(shape[2]);
      statistics.input_width = input_width;
      statistics.input_height = input_height;
      initialized = true;
    } catch (const Ort::Exception& exception) {
      error = exception.what();
    } catch (const std::exception& exception) {
      error = exception.what();
    }
  }

  template <typename T>
  std::vector<Ort::Value> run(std::vector<T>& input, const std::array<std::int64_t, 4>& shape) {
    const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<T>(memory, input.data(), input.size(), shape.data(),
                                               shape.size());
    const char* input_names[] = {input_name.c_str()};
    const char* output_names[] = {output_name.c_str()};
    return session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
  }

  bool infer(const camera::CameraFrame& frame, nui::PoseFrame& pose) {
    if (!initialized) {
      return false;
    }
    if (frame.width == 0 || frame.height == 0 ||
        frame.bgra.size() != static_cast<std::size_t>(frame.width) * frame.height * 4U) {
      error = "Camera frame is not tightly packed BGRA8.";
      return false;
    }

    try {
      const auto start = std::chrono::steady_clock::now();
      const Letterbox box =
          calculate_letterbox(frame.width, frame.height, input_width, input_height);
      const std::array<std::int64_t, 4> shape = {
          1, static_cast<std::int64_t>(input_height), static_cast<std::int64_t>(input_width), 3};
      std::vector<Ort::Value> outputs;
      if (input_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) {
        auto input = make_input<std::uint8_t>(frame, input_width, input_height, box);
        outputs = run(input, shape);
      } else if (input_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
        auto input = make_input<std::int32_t>(frame, input_width, input_height, box);
        outputs = run(input, shape);
      } else {
        auto input = make_input<float>(frame, input_width, input_height, box);
        outputs = run(input, shape);
      }

      if (outputs.empty() || !outputs[0].IsTensor()) {
        error = "MoveNet returned no tensor output.";
        return false;
      }
      const auto output_info = outputs[0].GetTensorTypeAndShapeInfo();
      if (output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
          output_info.GetElementCount() < nui::kMoveNetKeypointCount * 3U) {
        error = "MoveNet output is not a float32 tensor containing 17 keypoints.";
        return false;
      }

      const float* output = outputs[0].GetTensorData<float>();
      nui::MoveNetPoseFrame detected{};
      detected.timestamp_us = frame.timestamp_us;
      for (std::size_t index = 0; index < nui::kMoveNetKeypointCount; ++index) {
        const float model_y = output[index * 3U];
        const float model_x = output[index * 3U + 1U];
        auto& point = detected.keypoints[index];
        point.x = std::clamp((model_x * static_cast<float>(input_width) - box.x) /
                                 static_cast<float>(box.width),
                             0.0F, 1.0F);
        point.y = std::clamp((model_y * static_cast<float>(input_height) - box.y) /
                                 static_cast<float>(box.height),
                             0.0F, 1.0F);
        point.confidence = std::clamp(output[index * 3U + 2U], 0.0F, 1.0F);
      }
      pose = nui::MoveNetAdapter{}.convert(detected);

      const double elapsed_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      ++statistics.frames_processed;
      statistics.last_inference_ms = elapsed_ms;
      statistics.average_inference_ms +=
          (elapsed_ms - statistics.average_inference_ms) /
          static_cast<double>(statistics.frames_processed);
      error.clear();
      return true;
    } catch (const Ort::Exception& exception) {
      error = exception.what();
      return false;
    } catch (const std::exception& exception) {
      error = exception.what();
      return false;
    }
  }
};

MoveNetOnnx::MoveNetOnnx(const std::filesystem::path& model_path)
    : impl_(std::make_unique<Impl>(model_path)) {}

MoveNetOnnx::~MoveNetOnnx() = default;

bool MoveNetOnnx::ready() const noexcept {
  return impl_->initialized;
}

bool MoveNetOnnx::infer(const camera::CameraFrame& frame, nui::PoseFrame& pose) {
  return impl_->infer(frame, pose);
}

PoseInferenceStats MoveNetOnnx::stats() const noexcept {
  return impl_->statistics;
}

std::string MoveNetOnnx::last_error() const {
  return impl_->error;
}

}  // namespace fnk::pose
