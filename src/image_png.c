/* vi:set ts=4: */
/*
 * Copyright (C) 2021-2025 Tetsuya Isaki
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

//
// PNG 読み込み
//

#include "common.h"
#include "image_priv.h"
#include <png.h>

static const char *colortype2str(int type);

bool
image_png_match(FILE *fp, const struct diag *diag)
{
	uint8 magic[4];

	size_t n = fread(&magic[0], 1, sizeof(magic), fp);
	if (n < sizeof(magic)) {
		Debug(diag, "%s: fread failed: %s", __func__, strerrno());
		return false;
	}

	// マジックを確認。
	if (png_sig_cmp(magic, 0, sizeof(magic)) != 0) {
		return false;
	}

	return true;
}

struct image *
image_png_read(FILE *fp, const image_read_hint *dummy, const struct diag *diag)
{
	volatile png_structp png;
	volatile png_infop info;
	png_uint_32 width;
	png_uint_32 height;
	int bitdepth;
	int color_type;
	int interlace_type;
	int compression_type;
	int filter_type;
	int channels;
	uint stride;
	volatile uint8 **lines;
	volatile struct image *img;

	lines = NULL;
	img = NULL;

	png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
		NULL, NULL, NULL);
	if (__predict_false(png == NULL)) {
		return NULL;
	}

	info = png_create_info_struct(png);
	if (__predict_false(info == NULL)) {
		goto done;
	}

	// libpng 内のエラーからは大域ジャンプで戻ってくるらしい…
	if (setjmp(png_jmpbuf(png))) {
		image_free(UNVOLATILE(img));
		img = NULL;
		goto done;
	}

	png_init_io(png, fp);

	// ヘッダを読み込む。
	png_read_info(png, info);
	png_get_IHDR(png, info, &width, &height, &bitdepth,
		&color_type, &interlace_type, &compression_type, &filter_type);
	Debug(diag,
		"%s: IHDR width=%d height=%d interlace=%d compress=%d filter=%d",
		__func__, width, height, interlace_type, compression_type, filter_type);
	Debug(diag, "%s: IHDR colortype=%s bitdepth=%d%s",
		__func__, colortype2str(color_type), bitdepth,
		png_get_valid(png, info, PNG_INFO_tRNS) ? " tRNS" : "");

	// color_type によっていろいろ設定が必要。
	// see libpng(4) だけどなんだこれ…。
	//
	// Idx(<8)--------+
	// Gray(<8)---+   |   Gray        RGB         PaletteRGB GrayAl  RGBA
	//     FROM  01  31   0  0T  0O   2  2T  2O   3  3T  3O  4A  4O  6A  6O
	//     TO
	//      2    C   P   C   C   C   +  (.)  .   C  (-) (-)(CB)(CB) (B) (B)
	//     6A   (CA)(PA)(CA) C   C  (A)  T  tT  (PA) P   P   C  CBA  +   BA

	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png);
	} else if (color_type == PNG_COLOR_TYPE_GRAY) {
		if (bitdepth < 8) {
			png_set_expand_gray_1_2_4_to_8(png);
		}
		png_set_gray_to_rgb(png);
	}
	if (png_get_valid(png, info, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(png);
	}
	if (bitdepth > 8) {
		png_set_strip_16(png);
	}

	// 状態を更新してからチャンネル数を取得。bitdepth は 8 のはず?
	png_read_update_info(png, info);
	color_type = png_get_color_type(png, info);
	bitdepth = png_get_bit_depth(png, info);
	channels = png_get_channels(png, info);

	Debug(diag, "%s: Filt colortype=%s bitdepth=%d",
		__func__, colortype2str(color_type), bitdepth);

	// スキャンラインメモリのポインタ配列。
	lines = malloc(sizeof(char *) * height);
	if (lines == NULL) {
		goto done;
	}

	uint fmt;
	if (channels == 3) {
		fmt = IMAGE_FMT_RGB24;
	} else {
		fmt = IMAGE_FMT_ARGB32;
	}
	img = image_create(width, height, fmt);
	if (img == NULL) {
		goto done;
	}

	stride = image_get_stride(UNVOLATILE(img));
	for (int y = 0; y < height; y++) {
		lines[y] = img->buf + y * stride;
	}

	png_read_image(png, UNVOLATILE(lines));
	png_read_end(png, info);
 done:
	free(lines);
	png_destroy_read_struct(UNVOLATILE(&png), UNVOLATILE(&info), NULL);
	return UNVOLATILE(img);
}

// PNG の color type のデバッグ表示用。
static const char *
colortype2str(int type)
{
	static char buf[16];

	switch (type) {
	 case PNG_COLOR_TYPE_GRAY:
		return "Gray";
	 case PNG_COLOR_TYPE_PALETTE:
		return "Palette";
	 case PNG_COLOR_TYPE_RGB:
		return "RGB";
	 case PNG_COLOR_TYPE_RGBA:
		return "RGBA";
	 case PNG_COLOR_TYPE_GRAY_ALPHA:
		return "GrayA";
	 default:
		snprintf(buf, sizeof(buf), "%d(?)", type);
		return buf;
	}
}
