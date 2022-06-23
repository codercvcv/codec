#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_FILE_NAME "demo.mp4"
#define OUTPUT_FILE_NAME "out.hevc"

#define INBUF_SIZE 4096

int main() {
  AVFormatContext *fmt_ctx = NULL;
  AVCodecContext *codec_ctx = NULL;
  AVCodec *codec = NULL;
  FILE *f;
  uint8_t buffer[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
  uint8_t *data[4] = {NULL};
  int data_linesize[4];
  int bufsize;
  int ret;
  int video_idx = -1;
  int video_frame_count = 0;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();

  ret = avformat_open_input(&fmt_ctx, INPUT_FILE_NAME, NULL, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not open source file %s\n", INPUT_FILE_NAME);
    exit(1);
  }

  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not find stream information\n");
    exit(1);
  }

  ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (ret < 0) {
    fprintf(stderr, "Could not find %s stream in input file '%s'\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO), INPUT_FILE_NAME);
  }

  video_idx = ret;
  codec = avcodec_find_decoder(fmt_ctx->streams[video_idx]->codecpar->codec_id);
  if (!codec) {
    fprintf(stderr, "Failed to find %s codec\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
    exit(1);
  }

  codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    fprintf(stderr, "Failed to allocate the %s codec context\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
    exit(1);
  }

  ret = avcodec_parameters_to_context(codec_ctx,
                                      fmt_ctx->streams[video_idx]->codecpar);
  if (ret < 0) {
    fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
    exit(1);
  }

  ret = avcodec_open2(codec_ctx, codec, NULL);
  if (ret < 0) {
    fprintf(stderr, "Failed to open %s codec\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
    exit(1);
  }

  f = fopen(OUTPUT_FILE_NAME, "wb+");
  if (!f) {
    fprintf(stderr, "Could not open destination file %s\n", OUTPUT_FILE_NAME);
    exit(-1);
  }

  bufsize = av_image_alloc(data, data_linesize, codec_ctx->width,
                           codec_ctx->height, codec_ctx->pix_fmt, 1);
  if (bufsize < 0) {
    fprintf(stderr, "Could not allocate raw video buffer\n");
    exit(1);
  }

  av_dump_format(fmt_ctx, 0, INPUT_FILE_NAME, 0);

  // ============================================== //
  AVCodec *hevc_codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
  AVCodecContext *hevc_codec_ctx = avcodec_alloc_context3(hevc_codec);
  AVDictionary *params = NULL;
  hevc_codec_ctx->codec_id = AV_CODEC_ID_HEVC;
  hevc_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  hevc_codec_ctx->bit_rate = 400000;
  hevc_codec_ctx->width = 1920;
  hevc_codec_ctx->height = 1080;
  hevc_codec_ctx->time_base = (AVRational){1, 25};
  hevc_codec_ctx->framerate = (AVRational){25, 1};
  hevc_codec_ctx->gop_size = 10;
  hevc_codec_ctx->max_b_frames = 0;

  if (hevc_codec_ctx->flags & AVFMT_GLOBALHEADER) {
    hevc_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  av_dict_set(&params, "preset", "ultrafast", 0);
  av_dict_set(&params, "tune", "zerolatency", 0);
  ret = avcodec_open2(hevc_codec_ctx, hevc_codec, &params);
  if (ret < 0) {
    fprintf(stderr, "Failed to open %s codec\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
    exit(1);
  }

  // ============================================== //

  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == video_idx) {
      ret = avcodec_send_packet(codec_ctx, pkt);
      if (ret < 0) {
        fprintf(stderr, "Error submitting a packet for decoding (%d)\n", ret);
        break;
      }

      while (ret >= 0) {
        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret < 0) {
          if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            break;
          }
          fprintf(stderr, "Error during decoding (%d)\n", ret);
          exit(1);
        }

        printf("video_frame n:%d coded_n:%d\n", video_frame_count++,
               frame->coded_picture_number);
        // save yuv data
        // av_image_copy(data, data_linesize, (const uint8_t **)(frame->data),
        //               frame->linesize, codec_ctx->pix_fmt, codec_ctx->width,
        //               codec_ctx->height);
        // fwrite(data[0], 1, bufsize, f);

        ret = avcodec_send_frame(hevc_codec_ctx, frame);
        if (ret < 0) {
          fprintf(stderr, "Error sending a frame for encoding\n");
          exit(1);
        }

        while (ret >= 0) {
          ret = avcodec_receive_packet(hevc_codec_ctx, pkt);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
          }
          printf("Write packet %3" PRId64 " (size=%5d)\n", pkt->pts, pkt->size);
          fwrite(pkt->data, 1, pkt->size, f);
        }
        av_frame_unref(frame);
      }
    }
    av_packet_unref(pkt);
  }

  fclose(f);
  avcodec_free_context(&codec_ctx);
  avcodec_free_context(&hevc_codec_ctx);
  avformat_close_input(&fmt_ctx);
  av_packet_free(&pkt);
  av_frame_free(&frame);
  av_free(data[0]);

  return 0;
}