#pragma once

#include "fnk/nui/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fnk::nui {

enum class CalibrationQuality : std::uint32_t {
  Invalid = 0,
  TorsoOnly = 1,
  FullBody = 2,
};

struct GameSpaceProfile {
  CalibrationQuality quality{CalibrationQuality::Invalid};
  float center_x{0.5F};
  float floor_y{1.0F};
  float reference_torso_normalized{};
  float reference_torso_length_m{0.55F};
  float reference_depth_m{2.0F};
  float image_aspect_ratio{4.0F / 3.0F};
  std::uint32_t sample_count{};

  bool valid() const noexcept;
};

struct GameSpaceCalibrationOptions {
  std::size_t required_samples{30};
  float minimum_confidence{0.35F};
  float minimum_floor_confidence{0.55F};
  float reference_torso_length_m{0.55F};
  float reference_depth_m{2.0F};
  // Used only when the legs are outside the camera frame.
  float estimated_hip_to_floor_in_torsos{2.15F};
};

class GameSpaceCalibrator {
 public:
  explicit GameSpaceCalibrator(GameSpaceCalibrationOptions options = {});
  ~GameSpaceCalibrator();

  GameSpaceCalibrator(const GameSpaceCalibrator&) = delete;
  GameSpaceCalibrator& operator=(const GameSpaceCalibrator&) = delete;

  bool add_sample(const PoseFrame& pose, std::uint32_t image_width,
                  std::uint32_t image_height);
  void reset() noexcept;

  bool complete() const noexcept;
  float progress() const noexcept;
  std::size_t accepted_samples() const noexcept;
  std::size_t rejected_samples() const noexcept;
  const GameSpaceProfile& profile() const noexcept;

 private:
  struct Impl;
  GameSpaceCalibrationOptions options_{};
  std::unique_ptr<Impl> impl_;
};

bool save_game_space_profile(const GameSpaceProfile& profile,
                             const std::filesystem::path& path,
                             std::string* error = nullptr);
bool load_game_space_profile(const std::filesystem::path& path, GameSpaceProfile& profile,
                             std::string* error = nullptr);

}  // namespace fnk::nui
