#!/usr/bin/env python3
"""Prepara dependências privadas: nunca altera o xsdk/vulkan4xenon compartilhado."""
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parent.parent / ".work/native-display"
root.mkdir(parents=True, exist_ok=True)


def replace(path, before, after):
    text = path.read_text()
    if text.count(before) != 1:
        raise RuntimeError(f"Fonte incompatível: {path}: {before[:70]!r}")
    path.write_text(text.replace(before, after))


sdk, v4x = root / "xsdk", root / "vulkan4xenon"
if not (root / "patched-v4").exists():
    for source, destination in zip(map(Path, sys.argv[1:]), (sdk, v4x)):
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(source, destination, ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
    # Preserva os valores do enum usados pelo SDL já compilado.
    replace(sdk / "include/xsdk/display.h", "XSDK_DISPLAY_KERNEL,", "XSDK_DISPLAY_KERNEL,\n\tXSDK_DISPLAY_1080P,\n\tXSDK_DISPLAY_480P,\n\tXSDK_DISPLAY_480W,")
    display = sdk / "src/display.c"
    replace(display, "static uint32_t mode_height;", "static uint32_t mode_width, mode_height;")
    replace(display, "mode_height = *height;", "mode_width = *width;\n\tmode_height = *height;")
    replace(display, ".width = WIDTH_720P, .height = mode_height", ".width = mode_width, .height = mode_height")
    replace(display, "static bool plan_scaler(void)", "static bool plan_scaler(uint32_t width, uint32_t height)")
    replace(display, "video.is_interlaced || !video.display_width", "(video.is_interlaced && height > 480) || !video.display_width")
    replace(display, "letterboxed = !video.is_widescreen || letterbox_forced;", "letterboxed = height == 720 && (!video.is_widescreen || letterbox_forced);")
    replace(display, "plan_values(WIDTH_720P, HEIGHT_720P, new_scaler);", "plan_values(width, height, new_scaler);")
    replace(display, 'xsdk_log_write("display: 1280x720 %s to the %ux%u output",', 'xsdk_log_write("display: %ux%u %s to the %ux%u output", width, height,')
    replace(display, "scaling = mode == XSDK_DISPLAY_720P && plan_scaler();", """VIDEO_MODE video;
\tVdQueryVideoMode(&video);
\tif (mode == XSDK_DISPLAY_1080P && (video.is_interlaced || video.display_width != 1920 || video.display_height != 1080))
\t\treturn STATUS_NOT_SUPPORTED;
\tconst uint32_t target_width = mode == XSDK_DISPLAY_1080P ? 1920 : mode == XSDK_DISPLAY_480P ? 640 : mode == XSDK_DISPLAY_480W ? 848 : WIDTH_720P;
\tconst uint32_t target_height = mode == XSDK_DISPLAY_1080P ? 1080 : (mode == XSDK_DISPLAY_480P || mode == XSDK_DISPLAY_480W) ? 480 : HEIGHT_720P;
\tscaling = mode != XSDK_DISPLAY_KERNEL && plan_scaler(target_width, target_height);""")
    replace(display, "*width = scaling ? WIDTH_720P : viewport >> 16;", "*width = scaling ? target_width : viewport >> 16;")
    replace(display, "*height = scaling ? HEIGHT_720P : viewport & 0xFFFF;", "*height = scaling ? target_height : viewport & 0xFFFF;")
    present = sdk / "src/present.c"
    replace(present, '#include "xsdk/present.h"', '#include "xsdk/present.h"\n#include "xsdk/splash.h"')
    replace(present, "const bool splash_held = mode == XSDK_DISPLAY_720P", "if (mode != XSDK_DISPLAY_720P) xsdk_splash_end();\n\tconst bool splash_held = mode == XSDK_DISPLAY_720P")
    objects = v4x / "src/objects.h"
    replace(objects, "enum {\n\tV4X_MIN_SWAPCHAIN_IMAGES", "extern uint32_t tube360_display_width, tube360_display_height;\n#define V4X_DISPLAY_WIDTH tube360_display_width\n#define V4X_DISPLAY_HEIGHT tube360_display_height\n\nenum {\n\tV4X_MIN_SWAPCHAIN_IMAGES")
    replace(objects, "\tV4X_DISPLAY_WIDTH = 1280,\n\tV4X_DISPLAY_HEIGHT = 720,\n", "")
    display = v4x / "src/display.c"
    replace(display, '#include "xsdk/kernel.h"', '#include "xsdk/kernel.h"\n#include "xsdk/log.h"')
    replace(display, "static const VkExtent2D display_extent = { V4X_DISPLAY_WIDTH, V4X_DISPLAY_HEIGHT };", """uint32_t tube360_display_width = 1280, tube360_display_height = 720;
#define display_extent ((VkExtent2D){ V4X_DISPLAY_WIDTH, V4X_DISPLAY_HEIGHT })
int tube360_set_output(int height)
{
    VIDEO_MODE video;
    VdQueryVideoMode(&video);
    bool native = height == 1080 && !video.is_interlaced && video.display_width == 1920 && video.display_height == 1080;
    bool sd = height == 480 || video.display_height <= 576 || !video.is_widescreen;
    tube360_display_width = sd ? (video.is_widescreen ? 848 : 640) : native ? 1920 : 1280;
    tube360_display_height = sd ? 480 : native ? 1080 : 720;
    xsdk_log_write("display: dashboard %ux%u %s %s; framebuffer %ux%u", video.display_width, video.display_height,
        video.is_interlaced ? "interlaced" : "progressive", video.is_widescreen ? "16:9" : "4:3",
        tube360_display_width, tube360_display_height);
    return (int)tube360_display_height;
}""")
    replace(v4x / "src/swapchain.c", "xsdk_present_open(XSDK_DISPLAY_720P,", "xsdk_present_open(V4X_DISPLAY_HEIGHT == 480 ? (V4X_DISPLAY_WIDTH == 640 ? XSDK_DISPLAY_480P : XSDK_DISPLAY_480W) : V4X_DISPLAY_HEIGHT == 1080 ? XSDK_DISPLAY_1080P : XSDK_DISPLAY_720P,")
    (root / "patched-v4").write_text("native SD/HD framebuffer; signal timings remain selected by the dashboard\n")
print(root)
