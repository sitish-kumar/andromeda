#include "picker/previews.h"

#include "loop/loop.h"
#include "wayland/wayland.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <drm_fourcc.h>
#include <mutex>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <wayland-client.h>

namespace xdpu {
  namespace {
    struct Capture {
      std::unique_ptr<WaylandContext::CaptureSession> session;
      std::unique_ptr<WaylandContext::CaptureFrame> frame;
      wl_buffer* buffer = nullptr;
      void* mapping = MAP_FAILED;
      size_t size = 0;
      int fd = -1;

      ~Capture() {
        frame.reset();
        session.reset();
        if (buffer != nullptr) {
          wl_buffer_destroy(buffer);
        }
        if (mapping != MAP_FAILED) {
          munmap(mapping, size);
        }
        if (fd >= 0) {
          close(fd);
        }
      }
    };

    GdkPixbuf* thumbnail(const Capture& capture, uint32_t width, uint32_t height, uint32_t format, uint32_t transform) {
      // Downsample directly from SHM so a second full-resolution copy is never needed.
      const double scale = std::min(1.0, 512.0 / std::max(width, height));
      const int w = std::max(1, static_cast<int>(width * scale));
      const int h = std::max(1, static_cast<int>(height * scale));
      GdkPixbuf* image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
      if (image == nullptr) {
        return nullptr;
      }
      const bool bgr = format == DRM_FORMAT_XBGR8888 || format == DRM_FORMAT_ABGR8888;
      auto* pixels = static_cast<const uint32_t*>(capture.mapping);
      auto* target = gdk_pixbuf_get_pixels(image);
      const int stride = gdk_pixbuf_get_rowstride(image);
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          const uint32_t pixel =
              pixels[(static_cast<size_t>(y) * height / h) * width + static_cast<size_t>(x) * width / w];
          auto* rgb = target + y * stride + x * 3;
          rgb[0] = (pixel >> (bgr ? 0 : 16)) & 0xff;
          rgb[1] = (pixel >> 8) & 0xff;
          rgb[2] = (pixel >> (bgr ? 16 : 0)) & 0xff;
        }
      }
      // Undo the buffer transform: inverse rotation, then the optional horizontal flip.
      const GdkPixbufRotation rotations[] = {
          GDK_PIXBUF_ROTATE_NONE,
          GDK_PIXBUF_ROTATE_CLOCKWISE,
          GDK_PIXBUF_ROTATE_UPSIDEDOWN,
          GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE,
      };
      if ((transform & 3U) != 0) {
        GdkPixbuf* rotated = gdk_pixbuf_rotate_simple(image, rotations[transform & 3U]);
        g_object_unref(image);
        image = rotated;
      }
      if (image != nullptr && (transform & 4U) != 0) {
        GdkPixbuf* flipped = gdk_pixbuf_flip(image, TRUE);
        g_object_unref(image);
        image = flipped;
      }
      return image;
    }

    bool finishCaptureCleanup(Loop& loop, WaylandContext& wayland) {
      // Capture destruction only queues protocol requests. Keep dispatching until
      // the compositor has processed them, rather than parking an active capture
      // session on an idle connection until the picker closes.
      struct Sync {
        Loop& loop;
        bool done = false;
      } sync{loop};
      static constexpr wl_callback_listener listener = {
          .done = +[](void* data, wl_callback*, uint32_t) {
            auto& sync = *static_cast<Sync*>(data);
            sync.done = true;
            sync.loop.quit();
          },
      };
      wl_callback* callback = wl_display_sync(wayland.display());
      if (callback == nullptr) {
        return false;
      }
      wl_callback_add_listener(callback, &listener, &sync);
      // Never wait indefinitely on a compositor that stopped responding. The
      // caller closes the private connection if cleanup cannot be acknowledged.
      const int timeout = loop.addTimer(250, [&loop] { loop.quit(); });
      wayland.flush();
      if (timeout != 0 && wayland.connected()) {
        loop.run();
      }
      loop.removeTimer(timeout);
      wl_callback_destroy(callback);
      return sync.done;
    }

    GdkPixbuf* captureSource(Loop& loop, WaylandContext& wayland, const PreviewSource& source, std::stop_token stop) {
      Capture capture;
      GdkPixbuf* result = nullptr;
      bool done = false;
      auto finish = [&] {
        done = true;
        loop.quit();
      };
      auto constraints = [&](const CaptureConstraints& info) {
        // Resizes or unsupported formats leave a usable source with a fallback thumbnail.
        if (done || capture.buffer != nullptr) {
          finish();
          return;
        }
        const uint32_t format = WaylandContext::preferredShmFormat(info.shmFormats);
        if ((format != DRM_FORMAT_XRGB8888
             && format != DRM_FORMAT_ARGB8888
             && format != DRM_FORMAT_XBGR8888
             && format != DRM_FORMAT_ABGR8888)
            || info.bufferWidth == 0
            || info.bufferHeight == 0
            || info.bufferWidth > 16384
            || info.bufferHeight > 16384) {
          finish();
          return;
        }
        const uint32_t stride = info.bufferWidth * 4;
        capture.size = static_cast<size_t>(stride) * info.bufferHeight;
        if (capture.size > 128 * 1024 * 1024) {
          finish();
          return;
        }
        capture.fd = memfd_create("umbriel-preview", MFD_CLOEXEC);
        if (capture.fd < 0 || ftruncate(capture.fd, static_cast<off_t>(capture.size)) < 0) {
          finish();
          return;
        }
        capture.mapping = mmap(nullptr, capture.size, PROT_READ | PROT_WRITE, MAP_SHARED, capture.fd, 0);
        if (capture.mapping == MAP_FAILED) {
          finish();
          return;
        }
        capture.buffer =
            wayland.createShmBuffer(info.bufferWidth, info.bufferHeight, format, stride, capture.fd, capture.size);
        capture.frame = wayland.captureFrame(
            *capture.session, capture.buffer,
            [&, width = info.bufferWidth, height = info.bufferHeight,
             format](CaptureBuffer& buffer, uint64_t, uint32_t) {
              result = thumbnail(capture, width, height, format, buffer.transform);
              finish();
            },
            [&](CaptureFailureReason) { finish(); }
        );
      };
      capture.session = source.monitor ? wayland.createOutputCapture(source.identifier, false, constraints)
                                       : wayland.createToplevelCapture(source.identifier, false, constraints);
      if (!capture.session) {
        return nullptr;
      }
      capture.session->stoppedCb = finish;
      // Check cancellation frequently, and bound sources that never produce a frame.
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
      int timer = 0;
      std::function<void()> tick = [&] {
        timer = 0;
        if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
          finish();
        } else {
          timer = loop.addTimer(50, tick);
        }
      };
      timer = loop.addTimer(50, tick);
      if (!done && !stop.stop_requested() && wayland.connected()) {
        loop.run();
      }
      if (timer != 0) {
        loop.removeTimer(timer);
      }
      return result;
    }
  } // namespace

  struct Previews::Impl {
    const std::vector<PreviewSource> sources;
    std::vector<bool> started;
    std::deque<size_t> pending;
    std::mutex mutex;
    std::condition_variable_any workAvailable;
    std::vector<PreviewResult> results;
    std::jthread worker;

    explicit Impl(std::vector<PreviewSource> sources)
        : sources(std::move(sources)), started(this->sources.size(), false),
          worker([this](std::stop_token stop) { run(stop); }) {}

    void run(std::stop_token stop) {
      std::unique_ptr<Loop> loop;
      std::unique_ptr<WaylandContext> wayland;
      while (!stop.stop_requested()) {
        size_t index = 0;
        {
          std::unique_lock lock(mutex);
          if (!workAvailable.wait(lock, stop, [this] { return !pending.empty(); })) {
            break;
          }
          index = pending.front();
          pending.pop_front();
          started[index] = true;
        }
        GdkPixbuf* image = nullptr;
        try {
          // Even the Wayland connection is deferred until a card is visible.
          if (!wayland) {
            loop = std::make_unique<Loop>();
            wayland = std::make_unique<WaylandContext>(*loop);
          }
          if (!stop.stop_requested() && wayland->connected()) {
            image = captureSource(*loop, *wayland, sources[index], stop);
            // All Capture members have now been destroyed. Flush and drain
            // their destruction before publishing the thumbnail or idling.
            if (!wayland->connected() || !finishCaptureCleanup(*loop, *wayland)) {
              wayland.reset();
            }
          }
        } catch (const std::exception& error) {
          g_warning("umbriel-share-picker: preview unavailable: %s", error.what());
        }
        std::scoped_lock lock(mutex);
        results.push_back({index, image});
      }
    }

    ~Impl() {
      worker.request_stop();
      worker.join();
      for (auto& result : results) {
        g_clear_object(&result.image);
      }
    }
  };

  Previews::Previews(std::vector<PreviewSource> sources) : m_impl(std::make_unique<Impl>(std::move(sources))) {}
  Previews::~Previews() = default;

  void Previews::request(std::vector<size_t> indices) {
    std::scoped_lock lock(m_impl->mutex);
    m_impl->pending.clear();
    for (size_t index : indices) {
      if (index < m_impl->sources.size()
          && !m_impl->started[index]
          && std::ranges::find(m_impl->pending, index) == m_impl->pending.end()) {
        m_impl->pending.push_back(index);
      }
    }
    if (!m_impl->pending.empty()) {
      m_impl->workAvailable.notify_one();
    }
  }

  void Previews::stop() { m_impl->worker.request_stop(); }

  std::vector<PreviewResult> Previews::takeResults() {
    std::scoped_lock lock(m_impl->mutex);
    std::vector<PreviewResult> results;
    results.swap(m_impl->results);
    return results;
  }
} // namespace xdpu
