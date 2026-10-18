// fruit_ninja_kinect_recomp - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#include "rex_nui_bridge.h"

class FruitNinjaKinectRecompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<FruitNinjaKinectRecompApp>(new FruitNinjaKinectRecompApp(ctx, "fruit_ninja_kinect_recomp",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  void OnLoadXexImage(std::string& xex_image) override {
    xex_image = "game:\\default.uncompressed.xex";
  }
  void OnPostSetup() override {
    fnk::rexbridge::initialize(runtime()->memory(), runtime()->kernel_state());
  }
  void OnShutdown() override {
    fnk::rexbridge::shutdown();
  }
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
