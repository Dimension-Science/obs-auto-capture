#include "source-preview.hpp"

#include <obs-module.h>

#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>

SourcePreview::SourcePreview(obs_source_t *source, QWidget *parent) : QWidget(parent)
{
  weak_source_ = obs_source_get_weak_source(source);

  // A source only produces a picture while something is showing it. The
  // properties dialog of OBS does the same thing for its own preview; without
  // it a source that is not in the active scene stays black.
  obs_source_inc_showing(source);

  // A native window OBS can render into, with Qt kept away from the surface.
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAttribute(Qt::WA_NoSystemBackground);
  setMinimumHeight(260);
}

SourcePreview::~SourcePreview()
{
  // Order matters: the callback must be gone before the display, and the
  // display before the window handle it draws into.
  if (display_ != nullptr) {
    obs_display_remove_draw_callback(display_, DrawCallback, this);
    obs_display_destroy(display_);
    display_ = nullptr;
  }
  if (weak_source_ != nullptr) {
    if (obs_source_t *source = obs_weak_source_get_source(weak_source_)) {
      obs_source_dec_showing(source);
      obs_source_release(source);
    }
    obs_weak_source_release(weak_source_);
    weak_source_ = nullptr;
  }
}

void SourcePreview::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  CreateDisplay();
}

void SourcePreview::CreateDisplay()
{
  if (display_ != nullptr || !isVisible()) {
    return;
  }

  const qreal dpr = devicePixelRatioF();
  const uint32_t cx = static_cast<uint32_t>(width() * dpr);
  const uint32_t cy = static_cast<uint32_t>(height() * dpr);
  if (cx == 0 || cy == 0) {
    return;
  }

  gs_init_data info = {};
  info.cx = cx;
  info.cy = cy;
  info.format = GS_BGRA;
  info.zsformat = GS_ZS_NONE;
  info.window.hwnd = reinterpret_cast<HWND>(winId());

  display_ = obs_display_create(&info, 0x000000);
  if (display_ == nullptr) {
    blog(LOG_WARNING, "[obs-auto-capture] Could not create the preview display.");
    return;
  }
  obs_display_add_draw_callback(display_, DrawCallback, this);
}

void SourcePreview::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  if (display_ == nullptr) {
    CreateDisplay();
    return;
  }
  const qreal dpr = devicePixelRatioF();
  obs_display_resize(display_, static_cast<uint32_t>(width() * dpr), static_cast<uint32_t>(height() * dpr));
}

// Graphics thread. No Qt, no member access beyond the weak reference, which is
// only ever written in the constructor and read here.
void SourcePreview::DrawCallback(void *data, uint32_t cx, uint32_t cy)
{
  auto *preview = static_cast<SourcePreview *>(data);
  obs_source_t *source = obs_weak_source_get_source(preview->weak_source_);
  if (source == nullptr) {
    return;
  }

  const uint32_t source_cx = obs_source_get_width(source);
  const uint32_t source_cy = obs_source_get_height(source);
  if (source_cx > 0 && source_cy > 0 && cx > 0 && cy > 0) {
    // Letterboxed, so the blur zone stays where it is relative to the frame.
    const float scale = std::min(static_cast<float>(cx) / static_cast<float>(source_cx),
                                 static_cast<float>(cy) / static_cast<float>(source_cy));
    const int draw_cx = static_cast<int>(static_cast<float>(source_cx) * scale);
    const int draw_cy = static_cast<int>(static_cast<float>(source_cy) * scale);

    gs_viewport_push();
    gs_projection_push();
    gs_ortho(0.0f, static_cast<float>(source_cx), 0.0f, static_cast<float>(source_cy), -100.0f, 100.0f);
    gs_set_viewport((static_cast<int>(cx) - draw_cx) / 2, (static_cast<int>(cy) - draw_cy) / 2, draw_cx, draw_cy);
    obs_source_video_render(source);
    gs_projection_pop();
    gs_viewport_pop();
  }

  obs_source_release(source);
}
