#include "fnk/nui/game_space_calibration.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace fnk::nui {
namespace {

float distance(const PoseJoint& first, const PoseJoint& second) noexcept {
  const float x = first.image.x - second.image.x;
  const float y = first.image.y - second.image.y;
  return std::sqrt(x * x + y * y);
}

float median(std::vector<float> values) {
  if (values.empty()) {
    return 0.0F;
  }
  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2U);
  std::nth_element(values.begin(), middle, values.end());
  if ((values.size() & 1U) != 0U) {
    return *middle;
  }
  const float upper = *middle;
  const float lower = *std::max_element(values.begin(), middle);
  return (lower + upper) * 0.5F;
}

const char* quality_name(CalibrationQuality quality) noexcept {
  switch (quality) {
    case CalibrationQuality::TorsoOnly:
      return "torso_only";
    case CalibrationQuality::FullBody:
      return "full_body";
    default:
      return "invalid";
  }
}

}  // namespace

bool GameSpaceProfile::valid() const noexcept {
  return quality != CalibrationQuality::Invalid && reference_torso_normalized > 0.01F &&
         reference_torso_length_m > 0.0F && reference_depth_m > 0.0F &&
         game_reference_depth_m > 0.0F &&
         image_aspect_ratio > 0.0F;
}

struct GameSpaceCalibrator::Impl {
  std::vector<float> centers;
  std::vector<float> hips;
  std::vector<float> torsos;
  std::vector<float> observed_floors;
  std::size_t rejected{};
  GameSpaceProfile result;
};

GameSpaceCalibrator::GameSpaceCalibrator(GameSpaceCalibrationOptions options)
    : options_(options), impl_(std::make_unique<Impl>()) {
  if (options_.required_samples == 0) {
    options_.required_samples = 1;
  }
}

GameSpaceCalibrator::~GameSpaceCalibrator() = default;

bool GameSpaceCalibrator::add_sample(const PoseFrame& pose, std::uint32_t image_width,
                                     std::uint32_t image_height) {
  if (complete()) {
    return true;
  }
  if (image_width == 0 || image_height == 0) {
    ++impl_->rejected;
    return false;
  }

  const auto& shoulder_left = pose.joints[joint_index(Joint::ShoulderLeft)];
  const auto& shoulder_right = pose.joints[joint_index(Joint::ShoulderRight)];
  const auto& hip_left = pose.joints[joint_index(Joint::HipLeft)];
  const auto& hip_right = pose.joints[joint_index(Joint::HipRight)];
  const float torso_confidence =
      std::min({shoulder_left.confidence, shoulder_right.confidence, hip_left.confidence,
                hip_right.confidence});
  if (torso_confidence < options_.minimum_confidence) {
    ++impl_->rejected;
    return false;
  }

  PoseJoint shoulders = pose.joints[joint_index(Joint::ShoulderCenter)];
  PoseJoint hips = pose.joints[joint_index(Joint::HipCenter)];
  // Use the adapter's synthesized centers so collection and runtime depth use
  // precisely the same torso definition. Limb confidence still gates them.
  shoulders.confidence = torso_confidence;
  hips.confidence = torso_confidence;
  const float torso = distance(shoulders, hips);
  if (torso < 0.06F || torso > 0.55F || shoulders.image.y >= hips.image.y ||
      hips.image.x < -0.1F || hips.image.x > 1.1F) {
    ++impl_->rejected;
    return false;
  }

  impl_->centers.push_back(hips.image.x);
  impl_->hips.push_back(hips.image.y);
  impl_->torsos.push_back(torso);

  const auto& ankle_left = pose.joints[joint_index(Joint::AnkleLeft)];
  const auto& ankle_right = pose.joints[joint_index(Joint::AnkleRight)];
  if (ankle_left.confidence >= options_.minimum_floor_confidence &&
      ankle_right.confidence >= options_.minimum_floor_confidence &&
      ankle_left.image.y > hips.image.y && ankle_right.image.y > hips.image.y) {
    impl_->observed_floors.push_back((ankle_left.image.y + ankle_right.image.y) * 0.5F);
  }

  if (impl_->torsos.size() < options_.required_samples) {
    return true;
  }

  impl_->result.center_x = median(impl_->centers);
  const float hip_y = median(impl_->hips);
  impl_->result.reference_torso_normalized = median(impl_->torsos);
  impl_->result.reference_torso_length_m = options_.reference_torso_length_m;
  impl_->result.reference_depth_m = options_.reference_depth_m;
  impl_->result.game_reference_depth_m = options_.game_reference_depth_m;
  impl_->result.image_aspect_ratio =
      static_cast<float>(image_width) / static_cast<float>(image_height);
  impl_->result.sample_count = static_cast<std::uint32_t>(impl_->torsos.size());

  if (impl_->observed_floors.size() >= (options_.required_samples + 1U) / 2U) {
    impl_->result.floor_y = median(impl_->observed_floors);
    impl_->result.quality = CalibrationQuality::FullBody;
  } else {
    impl_->result.floor_y =
        hip_y + impl_->result.reference_torso_normalized *
                    options_.estimated_hip_to_floor_in_torsos;
    impl_->result.quality = CalibrationQuality::TorsoOnly;
  }
  return true;
}

void GameSpaceCalibrator::reset() noexcept {
  impl_ = std::make_unique<Impl>();
}

bool GameSpaceCalibrator::complete() const noexcept {
  return impl_->result.valid();
}

float GameSpaceCalibrator::progress() const noexcept {
  return std::min(1.0F, static_cast<float>(impl_->torsos.size()) /
                            static_cast<float>(options_.required_samples));
}

std::size_t GameSpaceCalibrator::accepted_samples() const noexcept {
  return impl_->torsos.size();
}

std::size_t GameSpaceCalibrator::rejected_samples() const noexcept {
  return impl_->rejected;
}

const GameSpaceProfile& GameSpaceCalibrator::profile() const noexcept {
  return impl_->result;
}

bool save_game_space_profile(const GameSpaceProfile& profile,
                             const std::filesystem::path& path, std::string* error) {
  if (!profile.valid()) {
    if (error != nullptr) {
      *error = "Cannot save an invalid game-space calibration profile.";
    }
    return false;
  }
  std::error_code filesystem_error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), filesystem_error);
  }
  std::ofstream output(path);
  if (!output) {
    if (error != nullptr) {
      *error = "Could not create calibration profile: " + path.string();
    }
    return false;
  }
  output << "version = 2\n"
         << "quality = \"" << quality_name(profile.quality) << "\"\n"
         << "center_x = " << profile.center_x << '\n'
         << "floor_y = " << profile.floor_y << '\n'
         << "reference_torso_normalized = " << profile.reference_torso_normalized << '\n'
         << "reference_torso_length_m = " << profile.reference_torso_length_m << '\n'
         << "reference_depth_m = " << profile.reference_depth_m << '\n'
         << "game_reference_depth_m = " << profile.game_reference_depth_m << '\n'
         << "image_aspect_ratio = " << profile.image_aspect_ratio << '\n'
         << "sample_count = " << profile.sample_count << '\n';
  if (!output.good()) {
    if (error != nullptr) {
      *error = "Failed while writing calibration profile: " + path.string();
    }
    return false;
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool load_game_space_profile(const std::filesystem::path& path, GameSpaceProfile& profile,
                             std::string* error) {
  std::ifstream input(path);
  if (!input) {
    if (error != nullptr) {
      *error = "Could not open calibration profile: " + path.string();
    }
    return false;
  }

  GameSpaceProfile loaded{};
  std::string line;
  try {
    while (std::getline(input, line)) {
      const std::size_t equals = line.find('=');
      if (equals == std::string::npos) {
        continue;
      }
      std::string key = line.substr(0, equals);
      std::string value = line.substr(equals + 1U);
      key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char character) {
                  return std::isspace(character) != 0;
                }),
                key.end());
      value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
                    return std::isspace(character) != 0;
                  }),
                  value.end());
      if (key == "quality") {
        loaded.quality = value.find("full_body") != std::string::npos
                             ? CalibrationQuality::FullBody
                             : CalibrationQuality::TorsoOnly;
      } else if (key == "center_x") {
        loaded.center_x = std::stof(value);
      } else if (key == "floor_y") {
        loaded.floor_y = std::stof(value);
      } else if (key == "reference_torso_normalized") {
        loaded.reference_torso_normalized = std::stof(value);
      } else if (key == "reference_torso_length_m") {
        loaded.reference_torso_length_m = std::stof(value);
      } else if (key == "reference_depth_m") {
        loaded.reference_depth_m = std::stof(value);
      } else if (key == "game_reference_depth_m") {
        loaded.game_reference_depth_m = std::stof(value);
      } else if (key == "image_aspect_ratio") {
        loaded.image_aspect_ratio = std::stof(value);
      } else if (key == "sample_count") {
        loaded.sample_count = static_cast<std::uint32_t>(std::stoul(value));
      }
    }
  } catch (const std::exception& exception) {
    if (error != nullptr) {
      *error = std::string("Invalid calibration profile: ") + exception.what();
    }
    return false;
  }
  if (!loaded.valid()) {
    if (error != nullptr) {
      *error = "Calibration profile is incomplete or invalid.";
    }
    return false;
  }
  profile = loaded;
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

}  // namespace fnk::nui
