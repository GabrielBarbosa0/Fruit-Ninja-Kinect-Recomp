#include "fnk/camera/media_foundation_camera.hpp"
#include "fnk/nui/types.hpp"
#include "fnk/pose/movenet_onnx.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Arguments {
  std::uint32_t device{};
  std::uint32_t width{640};
  std::uint32_t height{480};
  std::uint32_t fps{30};
  float threshold{0.25F};
  std::filesystem::path model{"tools/pose/models/movenet-singlepose-lightning.onnx"};
};

struct PreviewState {
  fnk::camera::CameraFrame frame;
  bool running{true};
};

bool parse_arguments(int argc, char** argv, Arguments& arguments) {
  for (int index = 1; index < argc; ++index) {
    if (index + 1 >= argc) {
      return false;
    }
    const std::string_view option(argv[index++]);
    const std::string value(argv[index]);
    try {
      if (option == "--device") {
        arguments.device = static_cast<std::uint32_t>(std::stoul(value));
      } else if (option == "--width") {
        arguments.width = static_cast<std::uint32_t>(std::stoul(value));
      } else if (option == "--height") {
        arguments.height = static_cast<std::uint32_t>(std::stoul(value));
      } else if (option == "--fps") {
        arguments.fps = static_cast<std::uint32_t>(std::stoul(value));
      } else if (option == "--threshold") {
        arguments.threshold = std::stof(value);
      } else if (option == "--model") {
        arguments.model = value;
      } else {
        return false;
      }
    } catch (...) {
      return false;
    }
  }
  return arguments.width > 0 && arguments.height > 0 && arguments.fps > 0 &&
         arguments.threshold >= 0.0F && arguments.threshold <= 1.0F;
}

void set_pixel(fnk::camera::CameraFrame& frame, int x, int y, std::uint8_t red,
               std::uint8_t green, std::uint8_t blue) {
  if (x < 0 || y < 0 || x >= static_cast<int>(frame.width) ||
      y >= static_cast<int>(frame.height)) {
    return;
  }
  const std::size_t offset =
      (static_cast<std::size_t>(y) * frame.width + static_cast<std::uint32_t>(x)) * 4U;
  frame.bgra[offset] = blue;
  frame.bgra[offset + 1U] = green;
  frame.bgra[offset + 2U] = red;
  frame.bgra[offset + 3U] = 255;
}

void draw_line(fnk::camera::CameraFrame& frame, int x0, int y0, int x1, int y1,
               std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  const int delta_x = std::abs(x1 - x0);
  const int step_x = x0 < x1 ? 1 : -1;
  const int delta_y = -std::abs(y1 - y0);
  const int step_y = y0 < y1 ? 1 : -1;
  int error = delta_x + delta_y;
  for (;;) {
    for (int offset = -1; offset <= 1; ++offset) {
      set_pixel(frame, x0 + offset, y0, red, green, blue);
      set_pixel(frame, x0, y0 + offset, red, green, blue);
    }
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int doubled = error * 2;
    if (doubled >= delta_y) {
      error += delta_y;
      x0 += step_x;
    }
    if (doubled <= delta_x) {
      error += delta_x;
      y0 += step_y;
    }
  }
}

void draw_marker(fnk::camera::CameraFrame& frame, const fnk::nui::PoseJoint& joint,
                 std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  const int center_x = static_cast<int>(joint.image.x * (frame.width - 1U));
  const int center_y = static_cast<int>(joint.image.y * (frame.height - 1U));
  for (int y = -7; y <= 7; ++y) {
    for (int x = -7; x <= 7; ++x) {
      if (x * x + y * y <= 49) {
        set_pixel(frame, center_x + x, center_y + y, red, green, blue);
      }
    }
  }
}

void draw_pose(fnk::camera::CameraFrame& frame, const fnk::nui::PoseFrame& pose,
               float threshold) {
  using fnk::nui::Joint;
  constexpr std::array<std::array<Joint, 2>, 19> bones{{
      {Joint::Head, Joint::ShoulderCenter}, {Joint::ShoulderCenter, Joint::Spine},
      {Joint::Spine, Joint::HipCenter}, {Joint::ShoulderCenter, Joint::ShoulderLeft},
      {Joint::ShoulderLeft, Joint::ElbowLeft}, {Joint::ElbowLeft, Joint::WristLeft},
      {Joint::WristLeft, Joint::HandLeft}, {Joint::ShoulderCenter, Joint::ShoulderRight},
      {Joint::ShoulderRight, Joint::ElbowRight}, {Joint::ElbowRight, Joint::WristRight},
      {Joint::WristRight, Joint::HandRight}, {Joint::HipCenter, Joint::HipLeft},
      {Joint::HipLeft, Joint::KneeLeft}, {Joint::KneeLeft, Joint::AnkleLeft},
      {Joint::AnkleLeft, Joint::FootLeft}, {Joint::HipCenter, Joint::HipRight},
      {Joint::HipRight, Joint::KneeRight}, {Joint::KneeRight, Joint::AnkleRight},
      {Joint::AnkleRight, Joint::FootRight},
  }};

  for (const auto& bone : bones) {
    const auto& first = pose.joints[fnk::nui::joint_index(bone[0])];
    const auto& second = pose.joints[fnk::nui::joint_index(bone[1])];
    if (first.confidence < threshold || second.confidence < threshold) {
      continue;
    }
    draw_line(frame, static_cast<int>(first.image.x * (frame.width - 1U)),
              static_cast<int>(first.image.y * (frame.height - 1U)),
              static_cast<int>(second.image.x * (frame.width - 1U)),
              static_cast<int>(second.image.y * (frame.height - 1U)), 40, 255, 90);
  }

  const auto& left = pose.joints[fnk::nui::joint_index(Joint::HandLeft)];
  const auto& right = pose.joints[fnk::nui::joint_index(Joint::HandRight)];
  if (left.confidence >= threshold) {
    draw_marker(frame, left, 255, 190, 30);
  }
  if (right.confidence >= threshold) {
    draw_marker(frame, right, 255, 80, 40);
  }
}

LRESULT CALLBACK preview_window_proc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam) {
  auto* state = reinterpret_cast<PreviewState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    state = static_cast<PreviewState*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }

  switch (message) {
    case WM_ERASEBKGND:
      return 1;
    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE) {
        DestroyWindow(window);
        return 0;
      }
      break;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
      if (state != nullptr && !state->frame.bgra.empty()) {
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = static_cast<LONG>(state->frame.width);
        bitmap.bmiHeader.biHeight = -static_cast<LONG>(state->frame.height);
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0,
                      static_cast<int>(state->frame.width),
                      static_cast<int>(state->frame.height), state->frame.bgra.data(),
                      &bitmap, DIB_RGB_COLORS, SRCCOPY);
      }
      EndPaint(window, &paint);
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      if (state != nullptr) {
        state->running = false;
      }
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int main(int argc, char** argv) {
  Arguments arguments;
  if (!parse_arguments(argc, argv, arguments)) {
    MessageBoxW(nullptr, L"Argumentos invalidos para a pre-visualizacao.",
                L"Fruit Ninja Kinect", MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }

  fnk::pose::MoveNetOnnx estimator(arguments.model);
  if (!estimator.ready()) {
    MessageBoxA(nullptr, estimator.last_error().c_str(), "MoveNet indisponivel",
                MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }

  fnk::camera::MediaFoundationCamera camera;
  if (!camera.start({arguments.device, arguments.width, arguments.height, arguments.fps})) {
    MessageBoxA(nullptr, camera.last_error().c_str(), "Camera indisponivel",
                MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }

  constexpr wchar_t kWindowClass[] = L"FruitNinjaKinectPosePreview";
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = preview_window_proc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  window_class.lpszClassName = kWindowClass;
  RegisterClassW(&window_class);

  PreviewState preview;
  RECT size{0, 0, 960, 720};
  AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, FALSE);
  HWND window = CreateWindowExW(
      0, kWindowClass, L"Fruit Ninja Kinect - Captura de movimento",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
      size.right - size.left, size.bottom - size.top, nullptr, nullptr,
      window_class.hInstance, &preview);
  if (window == nullptr) {
    camera.stop();
    return EXIT_FAILURE;
  }

  fnk::camera::CameraFrame captured;
  fnk::nui::PoseFrame pose;
  std::uint64_t previous_sequence = 0;
  std::uint64_t displayed_frames = 0;
  MSG message{};
  while (preview.running && camera.running()) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        preview.running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!preview.running) {
      break;
    }
    if (!camera.latest_frame(captured) || captured.sequence == previous_sequence) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    previous_sequence = captured.sequence;
    if (!estimator.infer(captured, pose)) {
      continue;
    }
    preview.frame = captured;
    draw_pose(preview.frame, pose, arguments.threshold);
    InvalidateRect(window, nullptr, FALSE);

    if (++displayed_frames % 30U == 0U) {
      const auto stats = estimator.stats();
      const auto& left = pose.joints[fnk::nui::joint_index(fnk::nui::Joint::HandLeft)];
      const auto& right = pose.joints[fnk::nui::joint_index(fnk::nui::Joint::HandRight)];
      std::wostringstream title;
      title << L"Fruit Ninja Kinect - Pose | " << stats.last_inference_ms
            << L" ms | maos " << left.confidence << L" / " << right.confidence
            << L" | ESC para fechar";
      SetWindowTextW(window, title.str().c_str());
    }
  }

  camera.stop();
  if (IsWindow(window)) {
    DestroyWindow(window);
  }
  return EXIT_SUCCESS;
}
