#include "ScenePainter.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

namespace react_native_linux {

void paintScene(SkCanvas& canvas, const SceneSnapshot& scene) {
    canvas.clear(kSceneBackgroundColor);

    SkPaint fillPaint;
    fillPaint.setAntiAlias(true);

    for (const SceneRectangle& rectangle : scene) {
        fillPaint.setColor(rectangle.colorArgb);
        canvas.drawRect(SkRect::MakeXYWH(rectangle.frame.origin.x, rectangle.frame.origin.y, rectangle.frame.size.width,
                                         rectangle.frame.size.height),
                        fillPaint);
    }
}

} // namespace react_native_linux
