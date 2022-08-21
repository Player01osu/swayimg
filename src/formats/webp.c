// SPDX-License-Identifier: MIT
// WebP format decoder.
// Copyright (C) 2020 Artem Senichev <artemsen@gmail.com>

#include "../exif.h"
#include "buildcfg.h"
#include "loader.h"

#include <string.h>
#include <webp/demux.h>

// WebP signature
static const uint8_t signature[] = { 'R', 'I', 'F', 'F' };

// WebP loader implementation
enum loader_status decode_webp(struct image* ctx, const uint8_t* data,
                               size_t size)
{
    const WebPData raw = { .bytes = data, .size = size };
    WebPAnimDecoderOptions webp_opts;
    WebPAnimDecoder* webp_dec = NULL;
    WebPAnimInfo webp_info;
    WebPBitstreamFeatures prop;
    int prev_timestamp = 0;

    // check signature
    if (size < sizeof(signature) ||
        memcmp(data, signature, sizeof(signature))) {
        return ldr_unsupported;
    }

    // get image properties
    if (WebPGetFeatures(data, size, &prop) != VP8_STATUS_OK) {
        image_print_error(ctx, "unable to get webp properties");
        return ldr_fmterror;
    }

    // open decoder
    WebPAnimDecoderOptionsInit(&webp_opts);
    webp_opts.color_mode = MODE_BGRA;
    webp_dec = WebPAnimDecoderNew(&raw, &webp_opts);
    if (!webp_dec) {
        image_print_error(ctx, "unable to decode webp image");
        goto fail;
    }
    if (!WebPAnimDecoderGetInfo(webp_dec, &webp_info)) {
        image_print_error(ctx, "unable to get webp info");
        goto fail;
    }

    // allocate frame sequence
    if (!image_create_frames(ctx, webp_info.frame_count)) {
        goto fail;
    }

    // decode every frame
    for (size_t i = 0; i < ctx->num_frames; ++i) {
        uint8_t* buffer;
        int timestamp;
        struct image_frame* frame = &ctx->frames[i];

        if (!image_frame_allocate(frame, webp_info.canvas_width,
                                  webp_info.canvas_height)) {
            goto fail;
        }
        if (!WebPAnimDecoderGetNext(webp_dec, &buffer, &timestamp)) {
            image_print_error(ctx, "failed to decode webp frame");
            goto fail;
        }
        memcpy(frame->data, buffer,
               frame->width * frame->height * sizeof(argb_t));

        if (ctx->num_frames > 1) {
            frame->duration = timestamp - prev_timestamp;
            prev_timestamp = timestamp;
            if (frame->duration <= 0) {
                frame->duration = 100;
            }
        }
    }

#ifdef HAVE_LIBEXIF
    const WebPDemuxer* webp_dmx = WebPAnimDecoderGetDemuxer(webp_dec);
    if (WebPDemuxGetI(webp_dmx, WEBP_FF_FORMAT_FLAGS) & EXIF_FLAG) {
        WebPChunkIterator it;
        if (WebPDemuxGetChunk(webp_dmx, "EXIF", 1, &it)) {
            process_exif(ctx, it.chunk.bytes, it.chunk.size);
            WebPDemuxReleaseChunkIterator(&it);
        }
    }
#endif // HAVE_LIBEXIF

    WebPAnimDecoderDelete(webp_dec);

    image_set_format(
        ctx, "WebP %s %s%s", prop.format == 1 ? "lossy" : "lossless",
        prop.has_alpha ? "+alpha" : "", prop.has_animation ? "+animation" : "");
    ctx->alpha = prop.has_alpha;

    return ldr_success;

fail:
    if (webp_dec) {
        WebPAnimDecoderDelete(webp_dec);
    }
    return ldr_fmterror;
}
