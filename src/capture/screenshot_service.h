#pragma once

#include "capture/screencopy_capture.h"
#include "capture/screenshot_region_overlay.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ClipboardService;
class ConfigService;
class IpcService;
class NotificationManager;
struct Config;
struct WidgetConfig;
class RenderContext;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;

class ScreenshotService {
public:
  struct OutputOptions {
    bool saveToFile = true;
    bool copyToClipboard = false;
    bool pipeToCommand = false;
    bool freezeScreen = false;
    std::string pipeCommand;
    std::string directory;
    std::string filenamePattern;
  };

  ScreenshotService(
      WaylandConnection& wayland, NotificationManager& notifications, ClipboardService* clipboard = nullptr
  );
  ~ScreenshotService();

  [[nodiscard]] bool available() const noexcept;

  void captureFullscreen(const OutputOptions& options);
  void beginRegionCapture(RenderContext& renderContext, const OutputOptions& options);

  void onOutputChange();

  [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
  [[nodiscard]] bool onKeyboardEvent(const KeyboardEvent& event);

  [[nodiscard]] static OutputOptions outputOptionsFromWidget(const WidgetConfig& widget);
  [[nodiscard]] static OutputOptions outputOptionsFromConfig(const Config& config, std::string_view widgetName = {});
  [[nodiscard]] static std::optional<OutputOptions>
  tryOutputOptionsFromConfig(const Config& config, std::string_view widgetNameArg, std::string& error);

  void registerIpc(IpcService& ipc, const ConfigService& configService);

private:
  struct PendingCapture {
    wl_output* output = nullptr;
    std::optional<LogicalRect> region;
    OutputOptions outputOptions{};
    std::optional<std::filesystem::path> destPath;
  };

  void captureOutput(
      wl_output* output, std::optional<LogicalRect> region, const std::string& labelBase, const OutputOptions& options,
      int pathSuffix = 0
  );
  void ensureRegionOverlay();
  void startRegionOverlay(RenderContext& renderContext);
  void beginFreezeCapture();
  void finishFreezeCapture();
  void abortFreezeCapture(const std::string& message);
  void cancelRegionCapture();
  void deliverFrozenRegion(LogicalRect region, wl_output* output, const OutputOptions& options);
  void startNextQueuedCapture();
  void captureAllOutputs(const OutputOptions& options);
  void onCaptureComplete(
      std::optional<ScreencopyImage> image, const std::string& error, OutputOptions options,
      std::optional<std::filesystem::path> destPath
  );
  [[nodiscard]] std::filesystem::path defaultPicturesDirectory() const;
  [[nodiscard]] std::filesystem::path outputDirectory(const OutputOptions& options) const;
  [[nodiscard]] std::filesystem::path
  makeScreenshotPath(const OutputOptions& options, const std::string& labelBase, int suffix = 0) const;
  void notifySaved(const std::filesystem::path& path);
  void notifyError(const std::string& message);

  WaylandConnection& m_wayland;
  NotificationManager& m_notifications;
  ClipboardService* m_clipboard = nullptr;
  ScreencopyCapture m_capture;
  std::unique_ptr<capture::ScreenshotRegionOverlay> m_regionOverlay;
  std::vector<PendingCapture> m_captureQueue;
  OutputOptions m_regionOutputOptions{};
  RenderContext* m_regionRenderContext = nullptr;
  std::vector<capture::FrozenScreenshot> m_frozenScreenshots;
  std::vector<wl_output*> m_pendingFreezeOutputs;
  bool m_freezeCaptureActive = false;
};
