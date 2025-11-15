#include "macros.h"

#include "window/window.h"

void fpx3d_wnd_set_size_callback(Fpx3d_Wnd_Context *ctx,
                                 Fpx3d_Wnd_WindowSizeCallback callback) {
  NULL_CHECK(ctx, );

  ctx->sizeCallback = callback;
}

void fpx3d_wnd_set_window_pointer(Fpx3d_Wnd_Context *ctx, void *window) {
  NULL_CHECK(ctx, );

  ctx->pointer = window;
}
