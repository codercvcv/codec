#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INBUF_SIZE 4096

int main(int argc, char *argv[]) {
  const char *input_file, *output_file;
  AVCodecParserContext *parser_ctx;
  AVCodecContext *codec_ctx;
  AVCodec *codec;
  FILE *f, *hevc_f;
  uint8_t buffer[INBUF_SIZE + AV_INPUT_BUFFER_MIN_SIZE];
  uint8_t *data;
  size_t data_size;
  int ret;
  int eof;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();

  if (argc <= 2) {
    fprintf(stderr,
            "Usage: %s <input file> <output file>\n"
            "And check your input file is encoded by mpeg1video please.\n",
            argv[0]);
    exit(0);
  }
  input_file = argv[1];
  output_file = argv[2];

  memset(buffer + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

  codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) {
    fprintf(stderr, "Codec not found\n");
    exit(1);
  }

  parser_ctx = av_parser_init(codec->id);
  if (!parser_ctx) {
    fprintf(stderr, "parser not found\n");
    exit(1);
  }

  codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    fprintf(stderr, "Could not allocate video codec context\n");
    exit(1);
  }

  if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
    fprintf(stderr, "Could not open codec\n");
    exit(1);
  }

  f = fopen(input_file, "rb");
  if (!f) {
    fprintf(stderr, "Could not allocate video frame\n");
    exit(1);
  }

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

  av_dict_set(&params, "preset", "ultrafast", 0);
  av_dict_set(&params, "tune", "zerolatency", 0);
  ret = avcodec_open2(hevc_codec_ctx, hevc_codec, &params);
  if (ret < 0) {
    fprintf(stderr, "Failed to open %s codec\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
    exit(1);
  }

  hevc_f = fopen(output_file, "wb+");
  if (!hevc_f) {
    fprintf(stderr, "Could not write video frame\n");
    exit(1);
  }

  // ============================================== //

  while (!feof(f)) {
    data_size = fread(buffer, 1, INBUF_SIZE, f);
    if (!data_size) break;

    data = buffer;
    while (data_size > 0) {
      ret =
          av_parser_parse2(parser_ctx, codec_ctx, &pkt->data, &pkt->size, data,
                           data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if (ret < 0) {
        fprintf(stderr, "Error while parsing\n");
        exit(1);
      }
      data += ret;
      data_size -= ret;
      if (pkt->size) {
        ret = avcodec_send_packet(codec_ctx, pkt);
        if (ret < 0) {
          fprintf(stderr, "Error sending a packet for decoding\n");
          exit(1);
        }
        while (ret >= 0) {
          ret = avcodec_receive_frame(codec_ctx, frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
          }
          printf("saving frame %3d\n", codec_ctx->frame_number);
          fflush(stdout);

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
            printf("write packet %3" PRId64 " (size=%5d)\n", pkt->pts,
                   pkt->size);
            fwrite(pkt->data, 1, pkt->size, hevc_f);
          }
          av_frame_unref(frame);
        }
      }
      av_packet_unref(pkt);
    }
  }

  return 0;
}