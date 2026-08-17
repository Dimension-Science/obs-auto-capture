#pragma once

#include <obs.h>

#include <QWidget>

// Live picture of the source, rendered by OBS itself into a native child
// window, the same way the preview in the properties dialog works.
//
// The draw callback runs on the graphics thread. It must never touch Qt, and
// nothing here may assume the widget is alive when it runs: the display is torn
// down before the window handle goes away.
class SourcePreview : public QWidget {
public:
  SourcePreview(obs_source_t *source, QWidget *parent);
  ~SourcePreview() override;

protected:
  void showEvent(QShowEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  // Qt must not paint over a surface OBS owns.
  QPaintEngine *paintEngine() const override { return nullptr; }

private:
  void CreateDisplay();
  static void DrawCallback(void *data, uint32_t cx, uint32_t cy);

  obs_display_t *display_ = nullptr;
  obs_weak_source_t *weak_source_ = nullptr;
};
