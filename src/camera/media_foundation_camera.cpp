#include "fnk/camera/media_foundation_camera.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <stop_token>
#include <thread>
#include <utility>

namespace fnk::camera {
namespace {

using Microsoft::WRL::ComPtr;

std::string utf8(const wchar_t* value, std::size_t length) {
  if (value == nullptr || length == 0) {
    return {};
  }
  const int required = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length),
                                            nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length), result.data(), required,
                      nullptr, nullptr);
  return result;
}

std::string hresult_message(HRESULT result, const char* operation) {
  std::ostringstream stream;
  stream << operation << " failed (HRESULT 0x" << std::uppercase << std::hex
         << static_cast<std::uint32_t>(result) << ')';
  return stream.str();
}

class MediaFoundationRuntime {
 public:
  bool initialize(std::string& error) {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
      error = hresult_message(com_result, "CoInitializeEx");
      return false;
    }
    uninitialize_com_ = SUCCEEDED(com_result);

    const HRESULT media_result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(media_result)) {
      error = hresult_message(media_result, "MFStartup");
      if (uninitialize_com_) {
        CoUninitialize();
        uninitialize_com_ = false;
      }
      return false;
    }
    media_started_ = true;
    return true;
  }

  ~MediaFoundationRuntime() {
    if (media_started_) {
      MFShutdown();
    }
    if (uninitialize_com_) {
      CoUninitialize();
    }
  }

 private:
  bool uninitialize_com_{};
  bool media_started_{};
};

struct ActivationList {
  IMFActivate** values{};
  UINT32 count{};

  ~ActivationList() {
    for (UINT32 index = 0; index < count; ++index) {
      if (values[index] != nullptr) {
        values[index]->Release();
      }
    }
    CoTaskMemFree(values);
  }
};

bool enumerate_activations(ActivationList& result, std::string& error) {
  ComPtr<IMFAttributes> attributes;
  HRESULT hr = MFCreateAttributes(&attributes, 1);
  if (FAILED(hr)) {
    error = hresult_message(hr, "MFCreateAttributes");
    return false;
  }
  hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) {
    error = hresult_message(hr, "IMFAttributes::SetGUID");
    return false;
  }
  hr = MFEnumDeviceSources(attributes.Get(), &result.values, &result.count);
  if (FAILED(hr)) {
    error = hresult_message(hr, "MFEnumDeviceSources");
    return false;
  }
  return true;
}

std::string allocated_attribute(IMFActivate* activation, const GUID& key) {
  wchar_t* value = nullptr;
  UINT32 length = 0;
  if (FAILED(activation->GetAllocatedString(key, &value, &length))) {
    return {};
  }
  const std::string result = utf8(value, length);
  CoTaskMemFree(value);
  return result;
}

struct NativeTypeChoice {
  ComPtr<IMFMediaType> type;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t fps_numerator{};
  std::uint32_t fps_denominator{1};
};

bool choose_native_type(IMFSourceReader* reader, const CameraOptions& options,
                        NativeTypeChoice& choice, std::string& error) {
  std::uint64_t best_score = std::numeric_limits<std::uint64_t>::max();
  for (DWORD index = 0;; ++index) {
    ComPtr<IMFMediaType> type;
    const HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                   index, &type);
    if (hr == MF_E_NO_MORE_TYPES) {
      break;
    }
    if (FAILED(hr)) {
      error = hresult_message(hr, "IMFSourceReader::GetNativeMediaType");
      return false;
    }

    GUID major{};
    if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || major != MFMediaType_Video) {
      continue;
    }
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 fps_numerator = 0;
    UINT32 fps_denominator = 1;
    if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height))) {
      continue;
    }
    if (FAILED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &fps_numerator,
                                   &fps_denominator)) ||
        fps_denominator == 0) {
      fps_numerator = options.requested_fps;
      fps_denominator = 1;
    }

    const std::int64_t width_delta = static_cast<std::int64_t>(width) - options.requested_width;
    const std::int64_t height_delta = static_cast<std::int64_t>(height) - options.requested_height;
    const std::uint32_t fps = fps_numerator / fps_denominator;
    const std::int64_t fps_delta = static_cast<std::int64_t>(fps) - options.requested_fps;
    const std::uint64_t score = static_cast<std::uint64_t>(width_delta * width_delta) +
                                static_cast<std::uint64_t>(height_delta * height_delta) +
                                static_cast<std::uint64_t>(fps_delta * fps_delta * 100);
    if (score < best_score) {
      best_score = score;
      choice = {type, width, height, fps_numerator, fps_denominator};
    }
  }

  if (!choice.type) {
    error = "The selected camera exposes no video media type.";
    return false;
  }
  return true;
}

bool configure_reader(IMFSourceReader* reader, const CameraOptions& options,
                      std::uint32_t& width, std::uint32_t& height, std::string& error) {
  NativeTypeChoice native;
  if (!choose_native_type(reader, options, native, error)) {
    return false;
  }

  HRESULT hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,
                                            native.type.Get());
  if (FAILED(hr)) {
    error = hresult_message(hr, "SetCurrentMediaType(native)");
    return false;
  }

  ComPtr<IMFMediaType> output;
  hr = MFCreateMediaType(&output);
  if (SUCCEEDED(hr)) {
    hr = output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  }
  if (SUCCEEDED(hr)) {
    hr = output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  }
  if (SUCCEEDED(hr)) {
    hr = MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, native.width, native.height);
  }
  if (SUCCEEDED(hr)) {
    hr = MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, native.fps_numerator,
                             native.fps_denominator);
  }
  if (SUCCEEDED(hr)) {
    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,
                                      output.Get());
  }
  if (FAILED(hr)) {
    error = hresult_message(hr, "SetCurrentMediaType(RGB32)");
    return false;
  }

  ComPtr<IMFMediaType> current;
  hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current);
  if (FAILED(hr) || FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height))) {
    error = hresult_message(hr, "GetCurrentMediaType");
    return false;
  }
  return true;
}

bool copy_sample(IMFSample* sample, std::uint32_t width, std::uint32_t height,
                 CameraFrame& frame, std::string& error) {
  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
  if (FAILED(hr)) {
    error = hresult_message(hr, "IMFSample::ConvertToContiguousBuffer");
    return false;
  }

  frame.width = width;
  frame.height = height;
  frame.bgra.resize(static_cast<std::size_t>(width) * height * 4U);
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;

  ComPtr<IMF2DBuffer> buffer_2d;
  if (SUCCEEDED(buffer.As(&buffer_2d))) {
    BYTE* scanline = nullptr;
    LONG pitch = 0;
    hr = buffer_2d->Lock2D(&scanline, &pitch);
    if (FAILED(hr)) {
      error = hresult_message(hr, "IMF2DBuffer::Lock2D");
      return false;
    }
    for (std::uint32_t row = 0; row < height; ++row) {
      std::memcpy(frame.bgra.data() + static_cast<std::size_t>(row) * row_bytes,
                  scanline + static_cast<std::ptrdiff_t>(row) * pitch, row_bytes);
    }
    buffer_2d->Unlock2D();
  } else {
    BYTE* data = nullptr;
    DWORD maximum_length = 0;
    DWORD current_length = 0;
    hr = buffer->Lock(&data, &maximum_length, &current_length);
    if (FAILED(hr)) {
      error = hresult_message(hr, "IMFMediaBuffer::Lock");
      return false;
    }
    const std::size_t required = row_bytes * height;
    if (current_length < required) {
      buffer->Unlock();
      error = "Media Foundation returned an undersized RGB32 buffer.";
      return false;
    }
    std::memcpy(frame.bgra.data(), data, required);
    buffer->Unlock();
  }

  for (std::size_t index = 3; index < frame.bgra.size(); index += 4) {
    frame.bgra[index] = 255;
  }
  return true;
}

}  // namespace

struct MediaFoundationCamera::Impl {
  mutable std::mutex mutex;
  std::condition_variable startup_condition;
  std::jthread worker;
  ComPtr<IMFSourceReader> active_reader;
  CameraFrame latest;
  CameraStats statistics;
  std::string error;
  bool startup_complete{};
  bool startup_success{};
  std::atomic_bool is_running{};

  void set_error(std::string message) {
    std::lock_guard lock(mutex);
    error = std::move(message);
  }

  void signal_startup(bool success, std::string message = {}) {
    {
      std::lock_guard lock(mutex);
      startup_complete = true;
      startup_success = success;
      if (!message.empty()) {
        error = std::move(message);
      }
    }
    startup_condition.notify_all();
  }

  void capture(std::stop_token stop_token, CameraOptions options) {
    std::string local_error;
    MediaFoundationRuntime runtime;
    if (!runtime.initialize(local_error)) {
      signal_startup(false, std::move(local_error));
      return;
    }

    ActivationList devices;
    if (!enumerate_activations(devices, local_error)) {
      signal_startup(false, std::move(local_error));
      return;
    }
    if (options.device_index >= devices.count) {
      signal_startup(false, "Camera device index is out of range.");
      return;
    }

    ComPtr<IMFMediaSource> source;
    HRESULT hr = devices.values[options.device_index]->ActivateObject(IID_PPV_ARGS(&source));
    if (FAILED(hr)) {
      signal_startup(false, hresult_message(hr, "IMFActivate::ActivateObject"));
      return;
    }

    ComPtr<IMFAttributes> reader_attributes;
    hr = MFCreateAttributes(&reader_attributes, 2);
    if (SUCCEEDED(hr)) {
      hr = reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }
    ComPtr<IMFSourceReader> reader;
    if (SUCCEEDED(hr)) {
      hr = MFCreateSourceReaderFromMediaSource(source.Get(), reader_attributes.Get(), &reader);
    }
    if (FAILED(hr)) {
      source->Shutdown();
      signal_startup(false, hresult_message(hr, "MFCreateSourceReaderFromMediaSource"));
      return;
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!configure_reader(reader.Get(), options, width, height, local_error)) {
      source->Shutdown();
      signal_startup(false, std::move(local_error));
      return;
    }

    {
      std::lock_guard lock(mutex);
      active_reader = reader;
      statistics.width = width;
      statistics.height = height;
    }
    is_running.store(true, std::memory_order_release);
    signal_startup(true);

    const auto capture_start = std::chrono::steady_clock::now();
    while (!stop_token.stop_requested()) {
      DWORD stream_index = 0;
      DWORD flags = 0;
      LONGLONG timestamp_100ns = 0;
      ComPtr<IMFSample> sample;
      hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream_index, &flags,
                              &timestamp_100ns, &sample);
      if (FAILED(hr)) {
        if (!stop_token.stop_requested()) {
          set_error(hresult_message(hr, "IMFSourceReader::ReadSample"));
        }
        break;
      }
      if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
        set_error("Camera stream ended.");
        break;
      }
      if (!sample) {
        continue;
      }

      CameraFrame frame;
      if (!copy_sample(sample.Get(), width, height, frame, local_error)) {
        set_error(std::move(local_error));
        break;
      }

      frame.timestamp_us = timestamp_100ns > 0
                               ? static_cast<std::uint64_t>(timestamp_100ns / 10)
                               : static_cast<std::uint64_t>(
                                     std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - capture_start)
                                         .count());

      std::lock_guard lock(mutex);
      frame.sequence = ++statistics.frames_captured;
      const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                            capture_start)
                                 .count();
      if (elapsed > 0.0) {
        statistics.capture_fps = static_cast<double>(statistics.frames_captured) / elapsed;
      }
      latest = std::move(frame);
    }

    {
      std::lock_guard lock(mutex);
      active_reader.Reset();
    }
    source->Shutdown();
    is_running.store(false, std::memory_order_release);
  }
};

MediaFoundationCamera::MediaFoundationCamera() : impl_(std::make_unique<Impl>()) {}

MediaFoundationCamera::~MediaFoundationCamera() {
  stop();
}

std::vector<CameraDeviceInfo> MediaFoundationCamera::enumerate(std::string* error) {
  std::string local_error;
  MediaFoundationRuntime runtime;
  if (!runtime.initialize(local_error)) {
    if (error != nullptr) {
      *error = std::move(local_error);
    }
    return {};
  }

  ActivationList activations;
  if (!enumerate_activations(activations, local_error)) {
    if (error != nullptr) {
      *error = std::move(local_error);
    }
    return {};
  }

  std::vector<CameraDeviceInfo> devices;
  devices.reserve(activations.count);
  for (UINT32 index = 0; index < activations.count; ++index) {
    devices.push_back({index,
                       allocated_attribute(
                           activations.values[index],
                           MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME),
                       allocated_attribute(
                           activations.values[index],
                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)});
  }
  if (error != nullptr) {
    error->clear();
  }
  return devices;
}

bool MediaFoundationCamera::start(const CameraOptions& options) {
  stop();
  {
    std::lock_guard lock(impl_->mutex);
    impl_->startup_complete = false;
    impl_->startup_success = false;
    impl_->error.clear();
    impl_->latest = {};
    impl_->statistics = {};
  }

  impl_->worker = std::jthread(
      [this, options](std::stop_token token) { impl_->capture(token, options); });

  std::unique_lock lock(impl_->mutex);
  const bool signaled = impl_->startup_condition.wait_for(
      lock, std::chrono::seconds(10), [this] { return impl_->startup_complete; });
  if (!signaled) {
    impl_->error = "Timed out while starting the camera.";
    lock.unlock();
    stop();
    return false;
  }
  return impl_->startup_success;
}

void MediaFoundationCamera::stop() noexcept {
  if (!impl_->worker.joinable()) {
    return;
  }
  impl_->worker.request_stop();
  ComPtr<IMFSourceReader> reader;
  {
    std::lock_guard lock(impl_->mutex);
    reader = impl_->active_reader;
  }
  if (reader) {
    reader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
  }
  impl_->worker.join();
  impl_->is_running.store(false, std::memory_order_release);
}

bool MediaFoundationCamera::running() const noexcept {
  return impl_->is_running.load(std::memory_order_acquire);
}

bool MediaFoundationCamera::latest_frame(CameraFrame& destination) const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->latest.sequence == 0) {
    return false;
  }
  destination = impl_->latest;
  return true;
}

CameraStats MediaFoundationCamera::stats() const noexcept {
  std::lock_guard lock(impl_->mutex);
  return impl_->statistics;
}

std::string MediaFoundationCamera::last_error() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->error;
}

}  // namespace fnk::camera
