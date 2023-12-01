/*
 * ffvademo.c - FFmpeg/vaapi demo program
 *
 * Copyright (C) 2014 Intel Corporation
 *   Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301
 */

#define _GNU_SOURCE 1
extern "C"
{
#include "sysdeps.h"
#include <getopt.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <va/va_drmcommon.h>
#include "ffvadisplay.h"
#include "ffvafilter.h"
#include "ffvarenderer.h"
#include "ffmpeg_utils.h"
#include "vaapi_utils.h"
#include "ffvadecoder.h"
#if USE_DRM
# include "ffvarenderer_drm.h"
#endif
#if USE_X11
# include "ffvarenderer_x11.h"
#endif
#if USE_EGL
# include "ffvarenderer_egl.h"
#endif
};

// Default window size
#define DEFAULT_WIDTH  640
#define DEFAULT_HEIGHT 480

// Default renderer
#if USE_DRM
#define DEFAULT_RENDERER FFVA_RENDERER_TYPE_DRM
#endif
#if USE_X11
#define DEFAULT_RENDERER FFVA_RENDERER_TYPE_X11
#endif
#ifndef DEFAULT_RENDERER
#define DEFAULT_RENDERER FFVA_RENDERER_TYPE_EGL
#endif

#define DEFAULT_RENDERER FFVA_RENDERER_TYPE_EGL

// Default memory type
#define DEFAULT_MEM_TYPE MEM_TYPE_DMA_BUF

typedef enum {
    MEM_TYPE_DMA_BUF = 1,
    MEM_TYPE_GEM_BUF,
    MEM_TYPE_MESA_IMAGE,
    MEM_TYPE_MESA_TEXTURE,
} MemType;

typedef struct {
    char *filename;
    char *minor_filename;
    FFVARendererType renderer_type;
    uint32_t mem_type;
    enum AVPixelFormat pix_fmt;
    int list_pix_fmts;
    uint32_t window_width;
    uint32_t window_height;
} Options;

typedef struct {
    const void *klass;
    Options options;
    FFVADisplay *display;
    VADisplay va_display;
    FFVADecoder *decoder;
    FFVAFilter *filter;
    uint32_t filter_chroma;
    uint32_t filter_fourcc;
    FFVASurface filter_surface;
    FFVARenderer *renderer;
    uint32_t renderer_width;
    uint32_t renderer_height;
} App;

#define OFFSET(x) offsetof(App, options.x)
static const AVOption app_options[] = {
    { "filename", "path to video file to decode", OFFSET(filename),
      AV_OPT_TYPE_STRING, },
    { "minor_filename", "path to minor video file to decode", OFFSET(minor_filename),
      AV_OPT_TYPE_STRING, }, 
    { "window_width", "window width", OFFSET(window_width),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 4096 },
    { "window_height", "window height", OFFSET(window_height),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 4096 },
    { "renderer", "renderer type to use", OFFSET(renderer_type),
      AV_OPT_TYPE_FLAGS, { .i64 = DEFAULT_RENDERER }, 0, INT_MAX, 0,
      "renderer" },
    { "drm", "DRM", 0, AV_OPT_TYPE_CONST, { .i64 = FFVA_RENDERER_TYPE_DRM },
      0, 0, 0, "renderer" },
    { "x11", "X11", 0, AV_OPT_TYPE_CONST, { .i64 = FFVA_RENDERER_TYPE_X11 },
      0, 0, 0, "renderer" },
    { "egl", "EGL", 0, AV_OPT_TYPE_CONST, { .i64 = FFVA_RENDERER_TYPE_EGL },
      0, 0, 0, "renderer" },
    { "mem_type", "memory type for VA buffer exports", OFFSET(mem_type),
      AV_OPT_TYPE_FLAGS, { .i64 = 0}, 0, UINT_MAX, 0, "mem_type" },
    { "dma_buf", "DMA buffer handle", 0, AV_OPT_TYPE_CONST,
      { .i64 = MEM_TYPE_DMA_BUF }, 0, 0, 0, "mem_type" },
    { "gem_buf", "GEM buffer handle", 0, AV_OPT_TYPE_CONST,
      { .i64 = MEM_TYPE_GEM_BUF }, 0, 0, 0, "mem_type" },
    { "mesa_image", "EGLImage exported from Mesa", 0, AV_OPT_TYPE_CONST,
      { .i64 = MEM_TYPE_MESA_IMAGE }, 0, 0, 0, "mem_type" },
    { "mesa_texture", "GL texture exported from Mesa", 0, AV_OPT_TYPE_CONST,
      { .i64 = MEM_TYPE_MESA_TEXTURE }, 0, 0, 0, "mem_type" },
    { "pix_fmt", "output pixel format", OFFSET(pix_fmt),
      AV_OPT_TYPE_PIXEL_FMT, { .i64 = AV_PIX_FMT_NONE }, -1, AV_PIX_FMT_NB-1, },
    { "list_pix_fmts", "list output pixel formats", OFFSET(list_pix_fmts),
      AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, },
    { NULL, }
};

static void
app_free(App *app);

static const char *
get_basename(const char *filename)
{
    const char * const s = strrchr(filename, '/');

    return s ? s + 1 : filename;
}

static void
print_help(const char *prog)
{
    printf("Usage: %s <video1>\n", get_basename(prog));
    printf("\n");
    printf("Options:\n");
    printf("  %-28s  display this help and exit\n",
           "-h, --help");
    printf("  %-28s  window width (int) [default=0]\n",
           "-x, --window-width=WIDTH");
    printf("  %-28s  window height (int) [default=0]\n",
           "-y, --window-height=HEIGHT");
    printf("  %-28s  select a particular renderer (string) [default='x11']\n",
           "-r, --renderer=TYPE");
    printf("  %-28s  VA buffer export memory type (string) [default='auto']\n",
           "-m, --mem-type=TYPE");
    printf("  %-28s  output pixel format (AVPixelFormat) [default=none]\n",
           "-f, --format=FORMAT");
    printf("  %-28s  minor video filename\n", "-s --minor=<video2>");
    printf("  %-28s  list output pixel formats\n",
           "    --list-formats");
}

static const AVClass *
app_class(void)
{
    static const AVClass g_class = {
        .class_name     = "FFVADemo",
        .item_name      = av_default_item_name,
        .option         = app_options,
        .version        = LIBAVUTIL_VERSION_INT,
    };
    return &g_class;
}

static App *
app_new(void)
{
    App *app;

    // app = (App *)calloc(1, sizeof(*app));
    app = new App;
    if (!app)
        return NULL;

    app->klass = app_class();
    av_opt_set_defaults(app);
    ffva_surface_init_defaults(&app->filter_surface);
    return app;
}

static void
app_free(App *app)
{
    if (!app)
        return;

    ffva_renderer_freep(&app->renderer);
    va_destroy_surface(app->va_display, &app->filter_surface.id);
    ffva_filter_freep(&app->filter);
    ffva_decoder_freep(&app->decoder);
    ffva_display_freep(&app->display);
    av_opt_free(app);
    // free(app);
    delete app;
}

static bool
app_ensure_display(App *app)
{
    if (!app->display) {
        app->display = ffva_display_new(NULL);
        if (!app->display)
            goto error_create_display;
        app->va_display = ffva_display_get_va_display(app->display);
        av_log(NULL, AV_LOG_ERROR, "app_ensure_display display  : %x    \n", app->va_display);
    }
    return true;

    /* ERRORS */
error_create_display:
    av_log(app, AV_LOG_ERROR, "failed to create VA display\n");
    return false;
}

static bool
app_ensure_decoder(App *app)
{
    if (!app->decoder) {
        app->decoder = ffva_decoder_new(app->display);
        if (!app->decoder)
            goto error_create_decoder;
    }
    return true;

    /* ERRORS */
error_create_decoder:
    av_log(app, AV_LOG_ERROR, "failed to create FFmpeg/vaapi decoder\n");
    return false;
}

static bool
app_ensure_filter(App *app)
{
    const Options * const options = &app->options;
    const int *formats, *format = NULL;

    if (!app->filter) {
        app->filter = ffva_filter_new(app->display);
        if (!app->filter)
            goto error_create_filter;
    }

    if (options->pix_fmt == AV_PIX_FMT_NONE)
        return true;

    formats = ffva_filter_get_formats(app->filter);
    if (formats) {
        for (format = formats; *format != AV_PIX_FMT_NONE; format++) {
            if (*format == options->pix_fmt)
                break;
        }
    }
    if (!format || *format == AV_PIX_FMT_NONE)
        goto error_unsupported_format;

    if (!ffmpeg_to_vaapi_pix_fmt(options->pix_fmt, &app->filter_fourcc,
            &app->filter_chroma))
        goto error_unsupported_format;
    return true;

    /* ERRORS */
error_create_filter:
    av_log(app, AV_LOG_ERROR, "failed to create video processing pipeline\n");
    return false;
error_unsupported_format:
    av_log(app, AV_LOG_ERROR, "unsupported output format %s   , options->pix_fmt : %d  \n",
        av_get_pix_fmt_name(options->pix_fmt), options->pix_fmt);
    return false;
}

static bool
app_ensure_filter_surface(App *app, uint32_t width, uint32_t height)
{
    FFVASurface * const s = &app->filter_surface;
    VASurfaceID va_surface;
    VASurfaceAttrib attrib;
    VAStatus va_status;

    if (!app->filter)
        return true; // VPP not needed (checked in app_ensure_filter())

    if (width == s->width && height == s->height)
        return true;

    attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrib.type = VASurfaceAttribPixelFormat;
    attrib.value.type = VAGenericValueTypeInteger;
    attrib.value.value.i = app->filter_fourcc;
    
    va_destroy_surface(app->va_display, &s->id);
    av_log(NULL, AV_LOG_ERROR, "filter vaCreateSurfaces  : %x    \n", app->va_display);
    va_status = vaCreateSurfaces(app->va_display, app->filter_chroma,
        width, height, &va_surface, 1, &attrib, 1);
    av_log(NULL, AV_LOG_ERROR, "app_ensure_filter_surface va_surface : %d    \n", va_surface);
    if (!va_check_status(va_status, "vaCreateSurfaces()"))
        return false;

    ffva_surface_init(s, va_surface, app->filter_chroma, width, height);
    s->fourcc = app->filter_fourcc;
    return true;
}

static bool
app_ensure_renderer(App *app)
{
    const Options * const options = &app->options;
    uint32_t flags = 0;

    if (!app->renderer) {
        switch (options->renderer_type) {
#if USE_DRM
        case FFVA_RENDERER_TYPE_DRM:
            app->renderer = ffva_renderer_drm_new(app->display, flags);
            break;
#endif
#if USE_X11
        case FFVA_RENDERER_TYPE_X11:
            app->renderer = ffva_renderer_x11_new(app->display, flags);
            break;
#endif
#if USE_EGL
        case FFVA_RENDERER_TYPE_EGL:
            switch (options->mem_type) {
            case MEM_TYPE_DMA_BUF:
                flags |= FFVA_RENDERER_EGL_MEM_TYPE_DMA_BUFFER;
                break;
            case MEM_TYPE_GEM_BUF:
                flags |= FFVA_RENDERER_EGL_MEM_TYPE_GEM_BUFFER;
                break;
            case MEM_TYPE_MESA_IMAGE:
                flags |= FFVA_RENDERER_EGL_MEM_TYPE_MESA_IMAGE;
                break;
            case MEM_TYPE_MESA_TEXTURE:
                flags |= FFVA_RENDERER_EGL_MEM_TYPE_MESA_TEXTURE;
                break;
            }
            app->renderer = ffva_renderer_egl_new(app->display, flags);
            break;
#endif
        default:
            break;
        }
        if (!app->renderer)
            goto error_create_renderer;
    }
    return true;

    /* ERRORS */
error_create_renderer:
    av_log(app, AV_LOG_ERROR, "failed to create renderer\n");
    return false;
}

static bool
app_ensure_renderer_size(App *app, uint32_t width, uint32_t height)
{
    if (!app_ensure_renderer(app))
        return false;

    if (app->renderer_width != width || app->renderer_height != height) {
        if (!ffva_renderer_set_size(app->renderer, width, height))
            return false;
        app->renderer_width = width;
        app->renderer_height = height;
    }
    return true;
}

static bool
app_process_surface(App *app, FFVASurface *s, const VARectangle *rect,
    uint32_t flags)
{
    FFVASurface * const d = &app->filter_surface;

    if (!app_ensure_filter_surface(app, s->width, s->height))
        return false;

    if (ffva_filter_set_cropping_rectangle(app->filter, rect) < 0)
        return false;
    // av_log(NULL, AV_LOG_ERROR, "app_process_surface-----------------\n");
    if (ffva_filter_process(app->filter, s, d, flags) < 0)
        return false;
    return true;
}

static bool
app_render_surface(App *app, FFVASurface *s, const VARectangle *rect,
    uint32_t flags)
{
    const Options * const options = &app->options;
    uint32_t renderer_width, renderer_height;

    renderer_width = options->window_width ? options->window_width :
        rect->width;
    renderer_height = options->window_height ? options->window_height :
        rect->height;
    if (!app_ensure_renderer_size(app, renderer_width, renderer_height))
        return false;

    if (app->filter) {
        if (!app_process_surface(app, s, rect, flags))
            return false;

        // drop deinterlacing, color standard and scaling flags
        flags &= ~(VA_TOP_FIELD|VA_BOTTOM_FIELD|0xf0|VA_FILTER_SCALING_MASK);

        return ffva_renderer_put_surface(app->renderer, &app->filter_surface,
            NULL, NULL, flags);
    }
    return ffva_renderer_put_surface(app->renderer, s, rect, NULL, flags);
}

static int
app_render_frame(App *app, FFVADecoderFrame *dec_frame)
{
    FFVASurface * const s = dec_frame->surface;
    AVFrame * const frame = dec_frame->frame;
    const VARectangle *rect;
    VARectangle tmp_rect;
    uint32_t i, flags;

    if (dec_frame->has_crop_rect)
        rect = &dec_frame->crop_rect;
    else {
        tmp_rect.x = 0;
        tmp_rect.y = 0;
        tmp_rect.width = s->width;
        tmp_rect.height = s->height;
        rect = &tmp_rect;
    }

    flags = 0;
    for (i = 0; i < 1 + !!frame->interlaced_frame; i++) {
        flags &= ~(VA_TOP_FIELD|VA_BOTTOM_FIELD);
        if (frame->interlaced_frame) {
            flags |= ((i == 0) ^ !!frame->top_field_first) == 0 ?
                VA_TOP_FIELD : VA_BOTTOM_FIELD;
        }
        if (!app_render_surface(app, s, rect, flags))
            return AVERROR_UNKNOWN;
    }
    return 0;
}

static int
app_decode_frame(App *app)
{
    FFVADecoderFrame *dec_frame;
    int ret;

    ret = ffva_decoder_get_frame(app->decoder, &dec_frame);
    if (ret == 0) {
        ret = app_render_frame(app, dec_frame);
        ffva_decoder_put_frame(app->decoder, dec_frame);
		/*show too fast, use 25fps instead*/
		// usleep(40000);
    }
    return ret;
}

static bool
app_list_formats(App *app)
{
    const int *formats;
    int i;

    if (!app_ensure_display(app))
        return false;
    if (!app_ensure_filter(app))
        return false;

    formats = ffva_filter_get_formats(app->filter);
    if (!formats)
        return false;

    printf("List of supported output pixel formats:");
    for (i = 0; formats[i] != AV_PIX_FMT_NONE; i++) {
        if (i > 0)
            printf(",");
        printf(" %s", av_get_pix_fmt_name((AVPixelFormat)formats[i]));
    }
    printf("\n");
    return true;
}

static bool
app_list_info(App *app)
{
    const Options * const options = &app->options;
    bool list_info = false;

    if (options->list_pix_fmts) {
        app_list_formats(app);
        list_info = true;
    }
    return list_info;
}

static bool
app_run(App *major_app, App *minor_app)
{
    const Options * const options = &major_app->options;
    const Options * const minor_options = &minor_app->options;
    FFVADecoderInfo info;
    FFVADecoderInfo minor_info;
    bool need_filter;
    char errbuf[BUFSIZ];
    int ret;
    bool minor_flag = false;

    if (app_list_info(major_app))
        return true;

    if (!options->filename)
        goto error_no_filename;

    if (minor_options->minor_filename)
    {
        minor_flag = true;
    }

    need_filter = options->pix_fmt != AV_PIX_FMT_NONE;

    if (!app_ensure_display(major_app))
        return false;
    if (need_filter && !app_ensure_filter(major_app))
        return false;
    if (!app_ensure_renderer(major_app))
        return false;
    if (!app_ensure_decoder(major_app))
        return false;
    if (!app_ensure_renderer(major_app))
        return false;

    if (ffva_decoder_open(major_app->decoder, options->filename) < 0)
        return false;
    if (ffva_decoder_start(major_app->decoder) < 0)
        return false;

    if (!ffva_decoder_get_info(major_app->decoder, &info))
        return false;

    do {
        ret = app_decode_frame(major_app);
        ret = 0;
    } while (ret == 0 || ret == AVERROR(EAGAIN));
    if (ret != AVERROR_EOF)
        goto error_decode_frame;
    ffva_decoder_stop(major_app->decoder);
    ffva_decoder_close(major_app->decoder);
    return true;

    /* ERRORS */
error_no_filename:
    av_log(major_app, AV_LOG_ERROR, "no video file specified on command line\n");
    return false;
error_decode_frame:
    av_log(major_app, AV_LOG_ERROR, "failed to decode frame: %s\n",
        ffmpeg_strerror(ret, errbuf));
    return false;
}

static bool
app_parse_options(App *app_major, App *app_minor, int argc, char *argv[])
{
    char errbuf[BUFSIZ];
    int ret, v, o = -1;

    enum {
        OPT_LIST_FORMATS = 1000,
    };

    static const struct option long_options[] = {
        { "help",           no_argument,        NULL, 'h'                   },
        { "window-width",   required_argument,  NULL, 'x'                   },
        { "window-height",  required_argument,  NULL, 'y'                   },
        { "renderer",       required_argument,  NULL, 'r'                   },
        { "mem-type",       required_argument,  NULL, 'm'                   },
        { "format",         required_argument,  NULL, 'f'                   },
        { "list-formats",   no_argument,        NULL, OPT_LIST_FORMATS      },
        { NULL, }
    };

    for (;;) {
        v = getopt_long(argc, argv, "-hx:y:r:m:f:", long_options, &o);
        if (v < 0)
            break;

        switch (v) {
        case '?':
            return false;
        case 'h':
            print_help(argv[0]);
            return false;
        case 'x':
            ret = av_opt_set(app_major, "window_width", optarg, 0);
            break;
        case 'y':
            ret = av_opt_set(app_major, "window_height", optarg, 0);
            break;
        case 'r':
            ret = av_opt_set(app_major, "renderer", optarg, 0);
            break;
        case 'm':
            ret = av_opt_set(app_major, "mem_type", optarg, 0);
            break;
        case 'f':
            ret = av_opt_set(app_major, "pix_fmt", optarg, 0);
            break;
        case 's':
            ret = av_opt_set(app_minor, "minor_filename", optarg, 0);
        case OPT_LIST_FORMATS:
            ret = av_opt_set_int(app_major, "list_pix_fmts", 1, 0);
            break;
        case '\1':
            ret = av_opt_set(app_major, "filename", optarg, 0);
            break;
        default:
            ret = 0;
            break;
        }
        if (ret != 0)
            goto error_set_option;
    }
    return true;

    /* ERRORS */
error_set_option:
    if (o < 0) {
        av_log(app_major, AV_LOG_ERROR, "failed to set short option -%c: %s\n",
            v, ffmpeg_strerror(ret, errbuf));
    }
    else {
        av_log(app_major, AV_LOG_ERROR, "failed to set long option --%s: %s\n",
            long_options[o].name, ffmpeg_strerror(ret, errbuf));
    }
    return false;
}

int
main(int argc, char *argv[])
{
    App *app_major;
    App *app_minor;
    int ret = EXIT_FAILURE;

    if (argc == 1) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    app_major = app_new();
    app_minor = app_new();
    if (!app_major || !app_minor)
        goto cleanup;

    if (!app_parse_options(app_major, app_minor, argc, argv) || !app_run(app_major, app_minor))
        goto cleanup;
    ret = EXIT_SUCCESS;

cleanup:
    app_free(app_major);
    app_free(app_minor);
    return ret;
}
