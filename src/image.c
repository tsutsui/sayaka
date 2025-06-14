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
// 画像処理
//

#include "common.h"
#include "image_priv.h"
#include <string.h>

//#define IMAGE_PROFILE

#if defined(IMAGE_PROFILE)
#include <sys/time.h>
#define PROF(x)	gettimeofday(&x, NULL);
#define PROF_RESULT(msg, x)	do {	\
	struct timeval x##_res;	\
	timersub(&x##_end, &x##_start, &x##_res);	\
	printf("%-12s %u.%06u sec\n", msg,	\
		(uint)x##_res.tv_sec, (uint)x##_res.tv_usec);	\
} while (0)
#else
#define PROF(x)	/**/
#define PROF_RESULT(msg, x)	/**/
#endif

struct image_reductor_handle_;
typedef uint (*finder_t)(struct image_reductor_handle_ *, ColorRGB);

typedef struct {
	int16 r;
	int16 g;
	int16 b;
} ColorRGBint16;

typedef struct {
	int32 r;
	int32 g;
	int32 b;
} ColorRGBint32;

typedef struct image_reductor_handle_
{
	// 画像 (所有はしていない)
	struct image *dstimg;
	const struct image *srcimg;

	bool is_gray;

	// ゲイン。256 を 1.0 とする。負数なら適用しない (1.0 のまま)。
	int gain;

	// 色からパレット番号を検索する関数。
	finder_t finder;

	// RGB555 から適応パレットのカラーコードを引くハッシュ。
	uint8 *colorhash;
} image_reductor_handle;

static uint finder_gray(image_reductor_handle *, ColorRGB);
static uint finder_fixed8(image_reductor_handle *, ColorRGB);
static uint finder_vga16(image_reductor_handle *, ColorRGB);
static uint finder_fixed256(image_reductor_handle *, ColorRGB);
#if defined(SIXELV)
static uint finder_xterm256(image_reductor_handle *, ColorRGB);
static inline uint8 finder_xterm256_channel(uint8);
static uint finder_adaptive256(image_reductor_handle *, ColorRGB);
#endif
static void colorcvt_gray(ColorRGBint32 *);
static ColorRGB *image_alloc_gray_palette(uint);
static ColorRGB *image_alloc_fixed256_palette(void);
#if defined(SIXELV)
static ColorRGB *image_alloc_xterm256_palette(void);
static bool image_calc_adaptive256_palette(image_reductor_handle *);
#endif

#if defined(SIXELV)
static bool image_reduct_simple(image_reductor_handle *,
	const struct image_opt *, const struct diag *);
#endif
static bool image_reduct_highquality(image_reductor_handle *,
	const struct image_opt *, const struct diag *);
#if defined(SIXELV)
static void set_err(ColorRGBint16 *, int, const ColorRGBint32 *, int);
#endif
static inline void set_err_asr(ColorRGBint16 *, int, const ColorRGBint32 *,
	int);
static inline uint8 saturate_uint8(int);
static inline int16 saturate_adderr(int16, int);

static const ColorRGB palette_fixed8[];
static const ColorRGB palette_vga16[];

// opt を初期化する。
void
image_opt_init(struct image_opt *opt)
{
	opt->method  = REDUCT_HIGH_QUALITY;
	opt->diffuse = DIFFUSE_SFL;
	opt->color   = COLOR_FMT_256_RGB332;
	opt->cdm     = 0;
	opt->gain    = -1;
	opt->output_ormode = false;
	opt->output_transbg = false;
	opt->suppress_palette = false;
}

// width_ x height_ で形式が format_ の image を作成する。
// (バッファは未初期化)
struct image *
image_create(uint width_, uint height_, uint format_)
{
	struct image *img = calloc(1, sizeof(*img));
	if (img == NULL) {
		return NULL;
	}

	img->width = width_;
	img->height = height_;
	img->format = format_;
	img->buf = malloc(image_get_stride(img) * img->height);
	if (img->buf == NULL) {
		free(img);
		return NULL;
	}

	return img;
}

// image を解放する。NULL なら何もしない。
void
image_free(struct image *img)
{
	if (img != NULL) {
		free(img->buf);
		free(img->palette_buf);
		free(img);
	}
}

// 1ピクセルあたりのバイト数を返す。
uint
image_get_bytepp(const struct image *img)
{
	switch (img->format) {
	 case IMAGE_FMT_ARGB16:	return 2;
	 case IMAGE_FMT_AIDX16:	return 2;
	 case IMAGE_FMT_RGB24:	return 3;
	 case IMAGE_FMT_ARGB32:	return 4;
	 default:
		assert(0);
	}
}

// ストライドを返す。
uint
image_get_stride(const struct image *img)
{
	return img->width * image_get_bytepp(img);
}

// いい感じにリサイズした時の幅と高さを求める。
void
image_get_preferred_size(
	uint current_width,			// 現在の幅
	uint current_height,		// 現在の高さ
	ResizeAxis axis,			// リサイズの基準
	uint request_width,			// 要求するリサイズ後の幅 (optional)
	uint request_height,		// 要求するリサイズ後の高さ (optional)
	uint *preferred_width,		// 求めた幅を格納する先
	uint *preferred_height)		// 求めた高さを格納する先
{
#if !defined(SIXELV)
	assert(axis == RESIZE_AXIS_SCALEDOWN_LONG);
#endif

	// 条件を丸めていく
	switch (axis) {
	 default:
	 case RESIZE_AXIS_LONG:
	 case RESIZE_AXIS_SCALEDOWN_LONG:
		if (current_width >= current_height) {
			axis = RESIZE_AXIS_WIDTH;
		} else {
			axis = RESIZE_AXIS_HEIGHT;
		}
		break;

#if defined(SIXELV)
	 case RESIZE_AXIS_BOTH:
	 case RESIZE_AXIS_SCALEDOWN_BOTH:
		if (request_width == 0) {
			axis = RESIZE_AXIS_HEIGHT;
		} else if (request_height == 0) {
			axis = RESIZE_AXIS_WIDTH;
		} else {
			axis = RESIZE_AXIS_BOTH;
		}
		break;

	 case RESIZE_AXIS_SHORT:
	 case RESIZE_AXIS_SCALEDOWN_SHORT:
		if (current_width <= current_height) {
			axis = RESIZE_AXIS_WIDTH;
		} else {
			axis = RESIZE_AXIS_HEIGHT;
		}
		break;

	 case RESIZE_AXIS_SCALEDOWN_WIDTH:
		axis = RESIZE_AXIS_WIDTH;
		break;

	 case RESIZE_AXIS_SCALEDOWN_HEIGHT:
		axis = RESIZE_AXIS_HEIGHT;
		break;
#endif
	}

	if (request_width < 1) {
		request_width = current_width;
	}
	if (request_height < 1) {
		request_height = current_height;
	}

	// 縮小のみ指示。
#if defined(SIXELV)
	if ((axis & RESIZE_AXIS_SCALEDOWN_BIT))
#endif
	{
		if (request_width > current_width) {
			request_width = current_width;
		}
		if (request_height > current_height) {
			request_height = current_height;
		}
	}

	// 確定したので計算。
	uint width;
	uint height;
	switch (axis) {
	 case RESIZE_AXIS_WIDTH:
		width  = request_width;
		height = current_height * width / current_width;
		break;

	 case RESIZE_AXIS_HEIGHT:
		height = request_height;
		width  = current_width * height / current_height;
		break;

#if defined(SIXELV)
	 case RESIZE_AXIS_BOTH:
		width  = request_width;
		height = request_height;
		break;
#endif

	 default:
		__unreachable();
	}

	// 代入。
	if (preferred_width) {
		*preferred_width  = width;
	}
	if (preferred_height) {
		*preferred_height = height;
	}
}

// ローダごとにサポートしているファイル形式をビットマップフラグにしたもの。
#define LOADERMAP_gif	(1U << IMAGE_LOADER_GIF)
#define LOADERMAP_jpeg	(1U << IMAGE_LOADER_JPEG)
#define LOADERMAP_png	(1U << IMAGE_LOADER_PNG)
#define LOADERMAP_webp	(1U << IMAGE_LOADER_WEBP)
#define LOADERMAP_stb	((1U << IMAGE_LOADER_BMP)	| \
						 (1U << IMAGE_LOADER_GIF)	| \
						 (1U << IMAGE_LOADER_JPEG)	| \
						 (1U << IMAGE_LOADER_PNG)	| \
						 (1U << IMAGE_LOADER_PNM))

// サポートしているローダ。処理順に並べること。
static const struct {
	image_match_t match;
	image_read_t  read;
	const char *libname;	// image_get_loaderinfo() で表示する名前
	const char *name;		// マクロ展開用とデバッグログとかで使う短縮名
	uint32 supported;		// このローダがサポートしている画像形式
} loader[] = {
#define ENTRY(name, libname, map)	\
	{ image_##name##_match, image_##name##_read, \
	  #libname, #name, map }
#if defined(USE_LIBWEBP)
	ENTRY(webp, libwebp, LOADERMAP_webp),
#endif
#if defined(USE_LIBJPEG)
	ENTRY(jpeg, libjpeg, LOADERMAP_jpeg),
#endif
#if defined(USE_LIBPNG)
	ENTRY(png, libpng, LOADERMAP_png),
#endif
#if defined(USE_GIFLIB)
	ENTRY(gif, giflib, LOADERMAP_gif),
#endif
#if defined(USE_STB_IMAGE)
	ENTRY(stb, stb_image, LOADERMAP_stb),
#endif
#undef ENTRY
};

// サポートしているローダの一覧を返す。
// 戻り値は char * の配列で、{ name1, loader1, name2, loader2, ... }
// のように IMAGE_LOADER_* の画像形式とそのローダ名がペアで並んでおり、
// NULL で終端。
// 戻り値は使い終わったら free() すること。
char **
image_get_loaderinfo(void)
{
	static const char *names[] = { IMAGE_LOADER_NAMES };
	char **dst;
	char **d;

	dst = calloc(IMAGE_LOADER_MAX * 2 + 1, sizeof(char *));
	if (dst == NULL) {
		return NULL;
	}
	d = dst;

	for (uint n = 0; n < IMAGE_LOADER_MAX; n++) {
		uint32 filetype = (1U << n);
		int i;
		for (i = 0; i < countof(loader); i++) {
			if ((loader[i].supported & filetype)) {
				break;
			}
		}
		const char *lib = NULL;
		if (i < countof(loader)) {
			lib = UNCONST(loader[i].libname);
		} else if (n == IMAGE_LOADER_BLURHASH) {
			// Blurhash は loader[] テーブルには出てこないので手動処理。
			lib = "builtin";
		} else {
			continue;
		}
		*d++ = UNCONST(names[n]);
		*d++ = UNCONST(lib);
	}

	// 終端は1つ。
	*d = NULL;

	return dst;
}

// pstream の画像形式を判定する。
// 判定出来れば、image_read() に渡すための非負のインデックスを返す。
// 判定出来なければ -1 を返す。
// ここでは Blurhash は扱わない。
int
image_match(struct pstream *ps, const struct diag *diag)
{
	FILE *fp;
	int idx = -1;

	fp = pstream_open_for_peek(ps);
	if (fp == NULL) {
		Debug(diag, "pstream_open_for_peek() failed");
		return -1;
	}

	if (countof(loader) == 0) {
		Debug(diag, "%s: no decoders available", __func__);
		goto done;
	}

	for (uint i = 0; i < countof(loader); i++) {
		int ok = loader[i].match(fp, diag);
		Trace(diag, "Checking %-4s.. %s",
			loader[i].name, (ok ? "matched" : "no"));
		fseek(fp, 0, SEEK_SET);
		if (ok) {
			idx = i;
			goto done;
		}
	}
	Trace(diag, "%s: unsupported image format", __func__);

 done:
	fclose(fp);
	return idx;
}

// pstream から画像を読み込んで image を作成して返す。
// idx は image_match() で返された非負の値を指定すること。
// axis, width, height はリサイズ用のヒントで、これを使うかどうかは
// 画像ローダによる (今の所 jpeg のみ)。
// デコードに失敗すると NULL を返す。
struct image *
image_read(struct pstream *ps, int idx, const image_read_hint *hint,
	const struct diag *diag)
{
	FILE *fp;

	fp = pstream_open_for_read(ps);
	if (fp == NULL) {
		Debug(diag, "%s: pstream_open_for_read() failed", __func__);
		return NULL;
	}
	struct image *img = loader[idx].read(fp, hint, diag);
	fclose(fp);

	return img;
}

// 入力画像を 16bit 内部形式にインプレース変換する。
void
image_convert_to16(struct image *img)
{
	const uint8 *s8 = img->buf;
	uint16 *d16 = (uint16 *)img->buf;
	uint count = img->width * img->height;

	if (img->format == IMAGE_FMT_RGB24) {
		for (uint i = 0; i < count; i++) {
			uint r, g, b, v;
			r = (*s8++) >> 3;
			g = (*s8++) >> 3;
			b = (*s8++) >> 3;
			v = (r << 10) | (g << 5) | b;
			*d16++ = v;
		}
	} else if (img->format == IMAGE_FMT_ARGB32) {
		for (uint i = 0; i < count; i++) {
			uint r, g, b, a, v;
			r = (*s8++) >> 3;
			g = (*s8++) >> 3;
			b = (*s8++) >> 3;
			a = (*s8++);
			v = (r << 10) | (g << 5) | b;
			// A(不透明度)が半分以下なら透明(0x8000)とする。
			if (a < 0x80) {
				v |= 0x8000;
			}
			*d16++ = v;
		}

		// 実際にあるかどうかは調べてない。
		img->has_alpha = true;
	}

	img->format = IMAGE_FMT_ARGB16;
}

// src 画像を (dst_width, dst_height) にリサイズしながら同時に
// colormode に減色した新しい image を作成して返す。
struct image *
image_reduct(
	const struct image *src,	// 元画像
	uint dst_width,				// リサイズ後の幅
	uint dst_height,			// リサイズ後の高さ
	const struct image_opt *opt,	// パラメータ
	const struct diag *diag)
{
	struct image *dst;
	image_reductor_handle irbuf, *ir;
	bool ok = false;

	ir = &irbuf;
	memset(ir, 0, sizeof(*ir));
	ir->gain = opt->gain;

	dst = image_create(dst_width, dst_height, IMAGE_FMT_AIDX16);
	if (dst == NULL) {
		return NULL;
	}
	dst->has_alpha = src->has_alpha;

	ir->dstimg = dst;
	ir->srcimg = src;

	// 減色モードからパレットオペレーションを用意。
	switch (opt->color & COLOR_FMT_MASK) {
	 case COLOR_FMT_GRAY:
	 {
		uint graycount = GET_COLOR_COUNT(opt->color);
		dst->palette_buf = image_alloc_gray_palette(graycount);
		if (dst->palette_buf == NULL) {
			goto abort;
		}
		dst->palette = dst->palette_buf;
		dst->palette_count = graycount;
		ir->finder = finder_gray;
		ir->is_gray = true;
		break;
	 }

	 case COLOR_FMT_8_RGB:
		ir->finder  = finder_fixed8;
		dst->palette = palette_fixed8;
		dst->palette_count = 8;
		break;

	 case COLOR_FMT_16_VGA:
		ir->finder  = finder_vga16;
		dst->palette = palette_vga16;
		dst->palette_count = 16;
		break;

	 case COLOR_FMT_256_RGB332:
		dst->palette_buf = image_alloc_fixed256_palette();
		if (dst->palette_buf == NULL) {
			goto abort;
		}
		dst->palette = dst->palette_buf;
		dst->palette_count = 256;
		ir->finder = finder_fixed256;
		break;

#if defined(SIXELV)
	 case COLOR_FMT_256_XTERM:
		dst->palette_buf = image_alloc_xterm256_palette();
		if (dst->palette_buf == NULL) {
			goto abort;
		}
		dst->palette = dst->palette_buf;
		dst->palette_count = 256;
		ir->finder = finder_xterm256;
		break;

	 case COLOR_FMT_256_ADAPTIVE:
		dst->palette_buf = calloc(256, sizeof(ColorRGB));
		if (dst->palette_buf == NULL) {
			goto abort;
		}
		dst->palette = dst->palette_buf;
		dst->palette_count = 256;	// このあと変更する。
		ir->finder = finder_adaptive256;
		break;
#endif

	 default:
		Debug(diag, "%s: Unsupported color 0x%x", __func__, opt->color);
		goto abort;
	}

#if defined(SIXELV)
	if (opt->method == REDUCT_SIMPLE) {
		ok = image_reduct_simple(ir, opt, diag);

	} else if (opt->method != REDUCT_HIGH_QUALITY) {
		Debug(diag, "%s: Unknown method %u", __func__, opt->method);
		ok = false;

	} else
#endif
	{
		ok = image_reduct_highquality(ir, opt, diag);
	}

 abort:
#if defined(SIXELV)
	free(ir->colorhash);
#endif
	if (!ok) {
		image_free(dst);
		dst = NULL;
	}
	return dst;
}


//
// 分数計算機
//

typedef struct {
	int I;	// 整数項
	int N;	// 分子
	int D;	// 分母
} Rational;

static void
rational_init(Rational *sr, int i, int n, int d)
{
	sr->I = i;
	if (n < d) {
		sr->N = n;
	} else {
		sr->I += n / d;
		sr->N = n % d;
	}
	sr->D = d;
}

// sr += x
static void
rational_add(Rational *sr, const Rational *x)
{
	sr->I += x->I;
	sr->N += x->N;
	if (sr->N < 0) {
		sr->I--;
		sr->N += sr->D;
	} else if (sr->N >= sr->D) {
		sr->I++;
		sr->N -= sr->D;
	}
}


//
// 減色 & リサイズ
//

#if defined(SIXELV)
// 単純間引き
static bool
image_reduct_simple(image_reductor_handle *ir,
	const struct image_opt *opt, const struct diag *diag)
{
	struct image *dstimg = ir->dstimg;
	const struct image *srcimg = ir->srcimg;
	uint16 *d = (uint16 *)dstimg->buf;
	const uint16 *src = (const uint16 *)srcimg->buf;
	uint dstwidth  = dstimg->width;
	uint dstheight = dstimg->height;
	Rational ry;
	Rational rx;
	Rational ystep;
	Rational xstep;

	// 適応パレットならここでパレットを作成。
	if ((opt->color & COLOR_FMT_MASK) == COLOR_FMT_256_ADAPTIVE) {
		if (image_calc_adaptive256_palette(ir) == false) {
			return false;
		}
	}

	// 水平、垂直方向ともスキップサンプリング。

	rational_init(&ry, 0, 0, dstheight);
	rational_init(&ystep, 0, srcimg->height, dstheight);
	rational_init(&rx, 0, 0, dstwidth);
	rational_init(&xstep, 0, srcimg->width, dstwidth);

	for (uint y = 0; y < dstheight; y++) {
		rx.I = 0;
		rx.N = 0;
		const uint16 *s0 = &src[ry.I * srcimg->width];
		for (uint x = 0; x < dstwidth; x++) {
			ColorRGBint32 col;
			int a;
			uint16 v = s0[rx.I];
			a     =  (v >> 15);
			col.r = ((v >> 10) & 0x1f) << 3;
			col.g = ((v >>  5) & 0x1f) << 3;
			col.b = ( v        & 0x1f) << 3;

			if (ir->gain >= 0) {
				col.r = saturate_uint8((uint32)col.r * ir->gain / 256);
				col.g = saturate_uint8((uint32)col.g * ir->gain / 256);
				col.b = saturate_uint8((uint32)col.b * ir->gain / 256);
			}
			if (ir->is_gray) {
				colorcvt_gray(&col);
			}
			ColorRGB c8;
			c8.r = col.r;
			c8.g = col.g;
			c8.b = col.b;
			uint colorcode = ir->finder(ir, c8);
			if (a) {
				colorcode |= 0x8000;
			}
			*d++ = colorcode;

			rational_add(&rx, &xstep);
		}
		rational_add(&ry, &ystep);
	}

	return true;
}
#endif

// 二次元誤差分散法を使用して、出来る限り高品質に変換する。
static bool
image_reduct_highquality(image_reductor_handle *ir,
	const struct image_opt *opt, const struct diag *diag)
{
	struct image *dstimg = ir->dstimg;
	const struct image *srcimg = ir->srcimg;
	uint16 *d = (uint16 *)dstimg->buf;
	const uint16 *src = (const uint16 *)srcimg->buf;
	uint dstwidth  = dstimg->width;
	uint dstheight = dstimg->height;
	Rational ry;
	Rational rx;
	Rational ystep;
	Rational xstep;
	ColorRGBint32 cp;
	uint cdm = 256;

#if !defined(SIXELV)
	// sayaka では選択出来ないようにしてある。
	assert(opt->diffuse == DIFFUSE_SFL);
#endif

#if defined(SIXELV)
	// 適応パレットならここでパレットを作成。
	if ((opt->color & COLOR_FMT_MASK) == COLOR_FMT_256_ADAPTIVE) {
		if (image_calc_adaptive256_palette(ir) == false) {
			return false;
		}
	}
#endif

	// 水平、垂直ともピクセルを平均。
	// 真に高品質にするには補間法を適用するべきだがそこまではしない。

	rational_init(&ry, 0, 0, dstheight);
	rational_init(&ystep, 0, srcimg->height, dstheight);
	rational_init(&rx, 0, 0, dstwidth);
	rational_init(&xstep, 0, srcimg->width, dstwidth);

	// 誤差バッファ
	const int errbuf_count = 3;
	const int errbuf_left  = 2;
	const int errbuf_right = 2;
	int errbuf_width = dstwidth + errbuf_left + errbuf_right;
	int errbuf_len = errbuf_width * sizeof(ColorRGBint16);
	ColorRGBint16 *errbuf[errbuf_count];
	ColorRGBint16 *errbuf_mem = calloc(errbuf_count, errbuf_len);
	if (errbuf_mem == NULL) {
		return false;
	}
	for (int i = 0; i < errbuf_count; i++) {
		errbuf[i] = errbuf_mem + errbuf_left + errbuf_width * i;
	}

	memset(&cp, 0, sizeof(cp));

	for (uint y = 0; y < dstheight; y++) {
		uint sy0 = ry.I;
		rational_add(&ry, &ystep);
		uint sy1 = ry.I;
		if (sy0 == sy1) {
			sy1 += 1;
		}

		rx.I = 0;
		rx.N = 0;
		for (uint x = 0; x < dstwidth; x++) {
			uint sx0 = rx.I;
			rational_add(&rx, &xstep);
			uint sx1 = rx.I;
			if (sx0 == sx1) {
				sx1 += 1;
			}

			// 画素の平均を求める。
			ColorRGBint32 col;
			memset(&col, 0, sizeof(col));
			uint a = 0;
			for (uint sy = sy0; sy < sy1; sy++) {
				const uint16 *s = &src[sy * srcimg->width + sx0];
				for (uint sx = sx0; sx < sx1; sx++) {
					uint16 v = *s++;
					a     +=  (v >> 15);
					col.r += ((v >> 10) & 0x1f) << 3;
					col.g += ((v >>  5) & 0x1f) << 3;
					col.b += ( v        & 0x1f) << 3;
				}
			}
			uint area = (sy1 - sy0) * (sx1 - sx0);
			col.r /= area;
			col.g /= area;
			col.b /= area;

			if (ir->gain >= 0) {
				col.r = (uint32)col.r * ir->gain / 256;
				col.g = (uint32)col.g * ir->gain / 256;
				col.b = (uint32)col.b * ir->gain / 256;
			}

			if (opt->cdm != 0) {
				cdm /= 2;
				cdm = MAX(cdm, abs(col.r - cp.r));
				cdm = MAX(cdm, abs(col.g - cp.g));
				cdm = MAX(cdm, abs(col.b - cp.b));
				cdm += opt->cdm;
				if (cdm > 256) {
					cdm = 256;
				}
				cp = col;
			}

			col.r += errbuf[0][x].r;
			col.g += errbuf[0][x].g;
			col.b += errbuf[0][x].b;

			if (ir->is_gray) {
				colorcvt_gray(&col);
			}

			ColorRGB c8;
			c8.r = saturate_uint8(col.r);
			c8.g = saturate_uint8(col.g);
			c8.b = saturate_uint8(col.b);

			uint colorcode = ir->finder(ir, c8);
			uint16 v = colorcode;
			// 半分以上が透明なら透明ということにする。
			if (a > area / 2) {
				v |= 0x8000;
			}
			*d++ = v;

			col.r -= dstimg->palette[colorcode].r;
			col.g -= dstimg->palette[colorcode].g;
			col.b -= dstimg->palette[colorcode].b;

			// ランダムノイズを加える。
			if (0) {
			}

			if (cdm != 256) {
				col.r = col.r * cdm / 256;
				col.g = col.g * cdm / 256;
				col.b = col.b * cdm / 256;
			}

			switch (opt->diffuse) {
			 case DIFFUSE_SFL:
			 default:
				// Sierra Filter Lite
				set_err_asr(errbuf[0], x + 1, &col, 1);
				set_err_asr(errbuf[1], x - 1, &col, 2);
				set_err_asr(errbuf[1], x    , &col, 2);
				break;

#if defined(SIXELV)
			 case DIFFUSE_NONE:
				// 比較用の何もしないモード
				break;
			 case DIFFUSE_FS:
				// Floyd Steinberg Method
				set_err(errbuf[0], x + 1, &col, 112);
				set_err(errbuf[1], x - 1, &col, 48);
				set_err(errbuf[1], x    , &col, 80);
				set_err(errbuf[1], x + 1, &col, 16);
				break;
			 case DIFFUSE_ATKINSON:
				// Atkinson
				set_err(errbuf[0], x + 1, &col, 32);
				set_err(errbuf[0], x + 2, &col, 32);
				set_err(errbuf[1], x - 1, &col, 32);
				set_err(errbuf[1], x,     &col, 32);
				set_err(errbuf[1], x + 1, &col, 32);
				set_err(errbuf[2], x,     &col, 32);
				break;
			 case DIFFUSE_JAJUNI:
				// Jarvis, Judice, Ninke
				set_err(errbuf[0], x + 1, &col, 37);
				set_err(errbuf[0], x + 2, &col, 27);
				set_err(errbuf[1], x - 2, &col, 16);
				set_err(errbuf[1], x - 1, &col, 27);
				set_err(errbuf[1], x,     &col, 37);
				set_err(errbuf[1], x + 1, &col, 27);
				set_err(errbuf[1], x + 2, &col, 16);
				set_err(errbuf[2], x - 2, &col,  5);
				set_err(errbuf[2], x - 1, &col, 16);
				set_err(errbuf[2], x,     &col, 27);
				set_err(errbuf[2], x + 1, &col, 16);
				set_err(errbuf[2], x + 2, &col,  5);
				break;
			 case DIFFUSE_STUCKI:
				// Stucki
				set_err(errbuf[0], x + 1, &col, 43);
				set_err(errbuf[0], x + 2, &col, 21);
				set_err(errbuf[1], x - 2, &col, 11);
				set_err(errbuf[1], x - 1, &col, 21);
				set_err(errbuf[1], x,     &col, 43);
				set_err(errbuf[1], x + 1, &col, 21);
				set_err(errbuf[1], x + 2, &col, 11);
				set_err(errbuf[2], x - 2, &col,  5);
				set_err(errbuf[2], x - 1, &col, 11);
				set_err(errbuf[2], x,     &col, 21);
				set_err(errbuf[2], x + 1, &col, 11);
				set_err(errbuf[2], x + 2, &col,  5);
				break;
			 case DIFFUSE_BURKES:
				// Burkes
				set_err(errbuf[0], x + 1, &col, 64);
				set_err(errbuf[0], x + 2, &col, 32);
				set_err(errbuf[1], x - 2, &col, 16);
				set_err(errbuf[1], x - 1, &col, 32);
				set_err(errbuf[1], x,     &col, 64);
				set_err(errbuf[1], x + 1, &col, 32);
				set_err(errbuf[1], x + 2, &col, 16);
				break;
			 case DIFFUSE_2:
				// (x+1,y), (x,y+1)
				set_err(errbuf[0], x + 1, &col, 128);
				set_err(errbuf[1], x,     &col, 128);
				break;
			 case DIFFUSE_3:
				// (x+1,y), (x,y+1), (x+1,y+1)
				set_err(errbuf[0], x + 1, &col, 102);
				set_err(errbuf[1], x,     &col, 102);
				set_err(errbuf[1], x + 1, &col,  51);
				break;
			 case DIFFUSE_RGB:
				errbuf[0][x].r   = saturate_adderr(errbuf[0][x].r,   col.r);
				errbuf[1][x].b   = saturate_adderr(errbuf[1][x].b,   col.b);
				errbuf[1][x+1].g = saturate_adderr(errbuf[1][x+1].g, col.g);
				break;
#endif
			}
		}

		// 誤差バッファをローテート。
		ColorRGBint16 *tmp = errbuf[0];
		for (int i = 0; i < errbuf_count - 1; i++) {
			errbuf[i] = errbuf[i + 1];
		}
		errbuf[errbuf_count - 1] = tmp;
		// errbuf[y] には左マージンがあるのを考慮する。
		memset(errbuf[errbuf_count - 1] - errbuf_left, 0, errbuf_len);
	}

	free(errbuf_mem);
	return true;
}

#if defined(SIXELV)
// eb[x] += col * ratio / 256;
static void
set_err(ColorRGBint16 *eb, int x, const ColorRGBint32 *col, int ratio)
{
	eb[x].r = saturate_adderr(eb[x].r, col->r * ratio / 256);
	eb[x].g = saturate_adderr(eb[x].g, col->g * ratio / 256);
	eb[x].b = saturate_adderr(eb[x].b, col->b * ratio / 256);
}
#endif

// eb[x] += col >> shift
// シフト演算だけにしたもの。
static inline void
set_err_asr(ColorRGBint16 *eb, int x, const ColorRGBint32 *col, int shift)
{
	eb[x].r = saturate_adderr(eb[x].r, col->r >> shift);
	eb[x].g = saturate_adderr(eb[x].g, col->g >> shift);
	eb[x].b = saturate_adderr(eb[x].b, col->b >> shift);
}

static inline uint8
saturate_uint8(int val)
{
	if (val < 0) {
		return 0;
	}
	if (val > 255) {
		return 255;
	}
	return (uint8)val;
}

static inline int16
saturate_adderr(int16 a, int b)
{
	int16 val = a + b;
	if (val < -512) {
		return -512;
	} else if (val > 511) {
		return 511;
	} else {
		return val;
	}
}


//
// パレット
//

// グレースケール用のパレットを作成して返す。
static ColorRGB *
image_alloc_gray_palette(uint count)
{
	ColorRGB *pal = malloc(sizeof(ColorRGB) * count);
	if (pal == NULL) {
		return NULL;
	}
	for (uint i = 0; i < count; i++) {
		uint8 gray = i * 255 / (count - 1);
		ColorRGB c;
		c.u32 = RGBToU32(gray, gray, gray);
		pal[i] = c;
	}

	return pal;
}

// 256 段階グレースケールになっている c からパレット番号を返す。
static uint
finder_gray(image_reductor_handle *ir, ColorRGB c)
{
	uint count = ir->dstimg->palette_count;

	int I = (((uint)c.r) * (count - 1) + (255 / count)) / 255;
	if (I >= count) {
		return count - 1;
	}
	return I;
}

// c をグレー (NTSC 輝度) に変換する。
// I = (R * 0.299) + (G * 0.587) + (B * 0.114)
static void
colorcvt_gray(ColorRGBint32 *c)
{
	// 高速化のため雑に近似。
	// 上の式を整数演算にするには (R * 77 + G * 150 + B * 29) / 256 とかが
	// 定番だが、gcc(m68k) だと乗算命令使わず筆算式に展開するっぽくそれなら
	// 立ってるビットが少ないほうがいいので 4 ビットで済ませる。
	// 元々この係数の厳密性はそんなにないので、この程度で十分。
	// 普通に乗算命令を吐くアーキテクチャ/コンパイラなら、どっちでも一緒。

	int I = (c->r * 5 + c->g * 9 + c->b * 2) / 16;
	c->r = I;
	c->g = I;
	c->b = I;
}

// RGB 固定8色。
static const ColorRGB palette_fixed8[] = {
	{ RGBToU32(  0,   0,   0) },
	{ RGBToU32(255,   0,   0) },
	{ RGBToU32(  0, 255,   0) },
	{ RGBToU32(255, 255,   0) },
	{ RGBToU32(  0,   0, 255) },
	{ RGBToU32(255,   0, 255) },
	{ RGBToU32(  0, 255, 255) },
	{ RGBToU32(255, 255, 255) },
};

static uint
finder_fixed8(image_reductor_handle *ir, ColorRGB c)
{
#if defined(__m68k__)
	// $RRGGBB00 の各最上位ビットを下位3ビット %BGR にする。
	uint32 dst = 0;
	__asm(
		"	add.w	%1,%1	;\n"	// B -> X
		"	addx.l	%0,%0	;\n"
		"	swap	%1		;\n"
		"	add.b	%1,%1	;\n"	// G -> X
		"	addx.l	%0,%0	;\n"
		"	add.w	%1,%1	;\n"	// R -> X
		"	addx.l	%0,%0	;\n"
		: /* Output */
			"+d" (dst)
		: /* Input */
			"d" (c.u32)
	);
	return dst;
#else
	uint R = ((uint8)c.r >= 128);
	uint G = ((uint8)c.g >= 128);
	uint B = ((uint8)c.b >= 128);
	return R | (G << 1) | (B << 2);
#endif
}

// VGA 固定 16 色。
// ANSI16 の Standard VGA colors を基準とし、
// ただしパレット4 を Brown ではなく Yellow になるようにしてある。
static const ColorRGB palette_vga16[] = {
	{ RGBToU32(  0,   0,   0) },
	{ RGBToU32(170,   0,   0) },
	{ RGBToU32(  0, 170,   0) },
	{ RGBToU32(170, 170,   0) },
	{ RGBToU32(  0,   0, 170) },
	{ RGBToU32(170,   0, 170) },
	{ RGBToU32(  0, 170, 170) },
	{ RGBToU32(170, 170, 170) },
	{ RGBToU32( 85,  85,  85) },
	{ RGBToU32(255,  85,  85) },
	{ RGBToU32( 85, 255,  85) },
	{ RGBToU32(255, 255,  85) },
	{ RGBToU32( 85,  85, 255) },
	{ RGBToU32(255,  85, 255) },
	{ RGBToU32( 85, 255, 255) },
	{ RGBToU32(255, 255, 255) },
};

// 色 c を VGA 固定 16 色パレットへ変換する。
static uint
finder_vga16(image_reductor_handle *ir, ColorRGB c)
{
	uint R;
	uint G;
	uint B;
	uint I = (uint)c.r + (uint)c.g + (uint)c.b;

	if (c.r >= 213 || c.g >= 213 || c.b >= 213) {
		R = (c.r >= 213);
		G = (c.g >= 213);
		B = (c.b >= 213);
		if (R == G && G == B) {
			if (I >= 224 * 3) {
				return 15;
			} else {
				return 7;
			}
		}
		return (R + (G << 1) + (B << 2)) | 8;
	} else {
		R = (c.r >= 85);
		G = (c.g >= 85);
		B = (c.b >= 85);
		if (R == G && G == B) {
			if (I >= 128 * 3) {
				return 7;
			} else if (I >= 42 * 3) {
				return 8;
			} else {
				return 0;
			}
		}
		return R | (G << 1) | (B << 2);
	}
}

// R3,G3,B2 の固定 256 色パレットを作成して返す。
static ColorRGB *
image_alloc_fixed256_palette(void)
{
	ColorRGB *pal = malloc(sizeof(ColorRGB) * 256);
	if (pal == NULL) {
		return NULL;
	}
	for (uint i = 0; i < 256; i++) {
		ColorRGB c;
		c.r = (((i >> 5) & 0x07) * 255) / 7;
		c.g = (((i >> 2) & 0x07) * 255) / 7;
		c.b = (( i       & 0x03) * 255) / 3;
		pal[i] = c;
	}

	return pal;
}

// 固定 256 色で c に最も近いパレット番号を返す。
static uint
finder_fixed256(image_reductor_handle *ir, ColorRGB c)
{
	uint R = c.r >> 5;
	uint G = c.g >> 5;
	uint B = c.b >> 6;
	return (R << 5) | (G << 2) | B;
}

#if defined(SIXELV)

// xterm 互換の固定 256 色パレットを作成して返す。
static ColorRGB *
image_alloc_xterm256_palette(void)
{
	ColorRGB *pal = malloc(sizeof(ColorRGB) * 256);
	int i;

	if (pal == NULL) {
		return NULL;
	}
	// ANSI16 色。
	memcpy(pal, palette_vga16, sizeof(palette_vga16));

	// 216色 (6x6x6)。
	for (i = 0; i < 216; i++) {
		ColorRGB c;

		// レベルは 00, 5f, 87, af, d7, ff で、00 だけ直線上にない。
		c.r = ((i / (6*6)) % 6);
		c.r = c.r == 0 ? 0 : c.r * 0x28 + 0x37;
		c.g = ((i / (6)  ) % 6);
		c.g = c.g == 0 ? 0 : c.g * 0x28 + 0x37;
		c.b = ((i        ) % 6);
		c.b = c.b == 0 ? 0 : c.b * 0x28 + 0x37;
		pal[i + 16] = c;
	}

	// グレー24色。
	for (i = 0; i < 24; i++) {
		ColorRGB c;
		uint8 I = 8 + i * 10;
		c.r = I;
		c.g = I;
		c.b = I;
		pal[i + 16 + 216] = c;
	}

	return pal;
}

// xterm256色の1チャンネル分、0 .. 5 を返す。
static inline uint8
finder_xterm256_channel(uint8 c)
{
	// レベル: 00, 5f, 87, af, d7, ff
	// しきい:   2f, 73, 9b, bc, eb

	if (c < 0x73) {
		if (c < 0x2f) {
			c = 0;
		} else {
			c = 1;
		}
	} else {
		c = 2 + (c - 0x73) / 0x28;
	}
	return c;
}

// xterm 互換 256 色で c に最も近いパレット番号を返す。
static uint
finder_xterm256(image_reductor_handle *ir, ColorRGB c)
{
	return 16
		+ finder_xterm256_channel(c.r) * 36
		+ finder_xterm256_channel(c.g) * 6
		+ finder_xterm256_channel(c.b) * 1;
}

//
// 適応 256 色パレット。
//

struct octree {
	uint32 count;	// ピクセル数
	uint32 r;		// R 合計
	uint32 g;		// G 合計
	uint32 b;		// B 合計
	uint32 level;	// 階層 (root = 0)
	struct octree *children; // [8]
};

// { r5, g5, b5 } の色を追加する。
// r5, g5, b5 は下位 5 ビットのみ有効。
static void
octree_add(struct octree *node, uint level,
	uint32 r5, uint32 g5, uint32 b5, uint32 count)
{
	// このノード以下のピクセル数なので、途中にもすべて加算。
	node->count += count;

	if (__predict_false(level == 5)) {
		// リーフに来たらデータを置く。
		node->r += (r5 << 3) * count;
		node->g += (g5 << 3) * count;
		node->b += (b5 << 3) * count;
	} else {
		if (node->children == NULL) {
			node->children = calloc(8, sizeof(struct octree));
			for (uint i = 0; i < 8; i++) {
				node->level = level + 1;
			}
		}

		uint32 mask = 0x10 >> level;
		uint32 nr = (r5 & mask) >> (4 - level);
		uint32 ng = (g5 & mask) >> (4 - level);
		uint32 nb = (b5 & mask) >> (4 - level);
		uint32 n = (nr << 2) | (ng << 1) | nb;
		octree_add(&node->children[n], level + 1, r5, g5, b5, count);
	}
}

// node 以下のリーフの数を数える。
static uint
octree_count_leaf(const struct octree *node)
{
	if (node->children) {
		uint nleaf = 0;
		for (uint i = 0; i < 8; i++) {
			nleaf += octree_count_leaf(&node->children[i]);
		}
		return nleaf;
	} else {
		return (node->count != 0) ? 1 : 0;
	}
}

// node 以下で count が最小である、リーフ直上のノードを返す。
// *min は in/out パラメータで、初期 min を渡す。
// min より最小のものが見付かれば *min を更新してそのノードを返す。
// 見付からなければ *min を更新せず NULL を返す。
static struct octree *
octree_find_minnode(struct octree *node, uint32 *min)
{
	if (__predict_false(node->children == NULL)) {
		// リーフ。ここには来ないはず。
		return NULL;
	}

	// 子ノードのいずれかが孫を持つか。
	bool has_grandchild =
		(node->children[0].children != NULL) |
		(node->children[1].children != NULL) |
		(node->children[2].children != NULL) |
		(node->children[3].children != NULL) |
		(node->children[4].children != NULL) |
		(node->children[5].children != NULL) |
		(node->children[6].children != NULL) |
		(node->children[7].children != NULL);

	if (has_grandchild) {
		// 自分はまだ中間ノード。
		struct octree *minnode = NULL;
		for (uint i = 0; i < 8; i++) {
			struct octree *n = octree_find_minnode(&node->children[i], min);
			if (n) {
				minnode = n;
			}
		}
		return minnode;
	} else {
		// 自分の子がリーフのノード。
		if (node->count < *min) {
			*min = node->count;
			return node;
		} else {
			return NULL;
		}
	}
}

// このノードのリーフをマージする。
// リーフ直上のノードで行うこと。
static void
octree_merge_leaves(struct octree *node)
{
	uint32 r = 0;
	uint32 g = 0;
	uint32 b = 0;

	for (uint i = 0; i < 8; i++) {
		r += node->children[i].r;
		g += node->children[i].g;
		b += node->children[i].b;
	}
	// count は計算済み。
	node->r = r;
	node->g = g;
	node->b = b;
	free(node->children);
	node->children = NULL;
}

// node 以下のリーフをパレットに登録していく。
// idxp はインデックスへのポインタ。
static void
octree_set_palette(ColorRGB *pal, uint *idxp, const struct octree *node)
{
	if (node->children) {
		for (uint i = 0; i < 8; i++) {
			octree_set_palette(pal, idxp, &node->children[i]);
		}
	} else {
		if (node->count != 0) {
			uint idx = *idxp;
			pal[idx].r = node->r / node->count;
			pal[idx].g = node->g / node->count;
			pal[idx].b = node->b / node->count;
			idx++;
			*idxp = idx;
		}
	}
}

// パレットから c に最も近い色のパレット番号を返す。
static uint
finder_linear(image_reductor_handle *ir, ColorRGB c)
{
	const struct image *dstimg = ir->dstimg;
	uint32 mindist = (uint32)-1;
	uint minidx = 0;

	// [0] は無効値。[1] から palette_count 個が有効。
	const ColorRGB *pal = &dstimg->palette[1];
	for (uint i = 0; i < dstimg->palette_count; i++, pal++) {
		int32 dr = c.r - pal->r;
		int32 dg = c.g - pal->g;
		int32 db = c.b - pal->b;
		uint32 dist = (dr * dr) + (dg * dg) + (db * db);
		if (dist < mindist) {
			minidx = i;
			if (__predict_false(dist < 8)) {
				break;
			}
			mindist = dist;
		}
	}
	return minidx + 1;
}

// node の子を解放する。node 自身はこの親が解放すること。
static void
octree_free(struct octree *node)
{
	if (node->children) {
		for (uint i = 0; i < 8; i++) {
			octree_free(&node->children[i]);
		}
		free(node->children);
		node->children = NULL;
	}
}

// srcimg から適応 256 色パレットを作成。
static bool
image_calc_adaptive256_palette(image_reductor_handle *ir)
{
	struct image *dstimg = ir->dstimg;
	const struct image *srcimg = ir->srcimg;
	const uint16 *src = (const uint16 *)srcimg->buf;
	uint32 *colormap;
	uint32 colorcount = 0;
	struct octree root;
#if defined(IMAGE_PROFILE)
	struct timeval colormap_start, colormap_end;
	struct timeval octree_start, octree_end;
	struct timeval merge_start, merge_end;
	struct timeval fill_start, fill_end;
#endif

	// src 画像に使われている色を全部取り出す。
	// この時点で R,G,B 各色5ビットで足切りされていて、合計15ビットしか
	// ないので、配列の添字にしてダイレクトアクセスする。
	const uint32 capacity = 32768;
	colormap = calloc(capacity, sizeof(uint32));
	if (__predict_false(colormap == NULL)) {
		return false;
	}
	PROF(colormap_start);
	const uint16 *send = src + srcimg->width * srcimg->height;
	for (const uint16 *s = src; s < send; ) {
		uint16 v = *s++;
		v &= 0x7fff;
		if (__predict_false(ir->gain >= 0)) {
			uint32 r5 = ((v >> 10) & 0x1f) * ir->gain / 256;
			uint32 g5 = ((v >>  5) & 0x1f) * ir->gain / 256;
			uint32 b5 = ( v        & 0x1f) * ir->gain / 256;
			if (__predict_false(r5 > 31)) r5 = 31;
			if (__predict_false(g5 > 31)) g5 = 31;
			if (__predict_false(b5 > 31)) b5 = 31;
			v = (r5 << 10) | (g5 << 5) | b5;
		}
		colormap[v]++;
	}
	for (uint i = 0; i < capacity; i++) {
		if (colormap[i] != 0) {
			colorcount++;
		}
	}
	PROF(colormap_end);
	//printf("colorcount=%u/%u\n", colorcount, capacity);

	// octree に配置。
	PROF(octree_start);
	memset(&root, 0, sizeof(root));
	for (uint i = 0; i < capacity; i++) {
		if (colormap[i] == 0)
			continue;
		uint32 count = colormap[i];
		uint32 r5 = (i >> 10) & 0x1f;
		uint32 g5 = (i >>  5) & 0x1f;
		uint32 b5 = (i      ) & 0x1f;
		octree_add(&root, 0, r5, g5, b5, count);
	}
	PROF(octree_end);

	if (0) {
		for (uint i = 0; i < 8; i++) {
			printf("[%u] %u\n", i, root.children[i].count);
			if (root.children) {
				for (uint j = 0; j < 8; j++) {
					struct octree *n1 = &root.children[j];
					printf(" [%u,%u] %u\n", i, j, n1->count);
					if (n1->children) {
						for (uint k = 0; k < 8; k++) {
							struct octree *n2 = &n1->children[k];
							printf("  [%u,%u,%u] %u\n", i, j, k, n2->count);
						}
					}
				}
			}
		}
	}

	// 255 色以下になるまで少ない色をマージしていく。
	// [0] は無効値として使うので有効なのは最大 255 色。
	PROF(merge_start);
	uint leaf_count;
	while ((leaf_count = octree_count_leaf(&root)) > 255) {
		//printf("leaf_count=%u\n", leaf_count);
		uint32 min = -1;
		struct octree *minnode = octree_find_minnode(&root, &min);
		octree_merge_leaves(minnode);
	}
	PROF(merge_end);

	// パレットにセット。パレットは 1 から開始。
	uint idx = 1;
	octree_set_palette(dstimg->palette_buf, &idx, &root);
	dstimg->palette_count = idx;

	// 色ハッシュ用のバッファ。
	ir->colorhash = calloc(1, 32768);
	if (ir->colorhash == NULL) {
		return false;
	}

	PROF_RESULT("colormap",		colormap);
	PROF_RESULT("octree_add",	octree);
	PROF_RESULT("octree_merge",	merge);
	PROF_RESULT("octree_fill",	fill);

	free(colormap);
	octree_free(&root);
	return true;
}

// 適応パレットから c に最も近いパレット番号を返す。
static uint
finder_adaptive256(image_reductor_handle *ir, ColorRGB c)
{
	uint32 r5 = c.r >> 3;
	uint32 g5 = c.g >> 3;
	uint32 b5 = c.b >> 3;
	uint32 n = r5 * 32 * 32 + g5 * 32 + b5;
	uint8 cc = ir->colorhash[n];
	if (__predict_false(cc == 0)) {
		// まだパレット番号が入ってなければここで引く。
		c.r = r5 * 8 + 4;
		c.g = g5 * 8 + 4;
		c.b = b5 * 8 + 4;
		cc = finder_linear(ir, c);
		ir->colorhash[n] = cc;
	}
	return cc;
}


//
// enum のデバッグ表示用
//

// ResizeAxis を文字列にする。
// (内部バッファを使う可能性があるため同時に2回呼ばないこと)
const char *
resizeaxis_tostr(ResizeAxis axis)
{
	static const struct {
		ResizeAxis value;
		const char *name;
	} table[] = {
		{ RESIZE_AXIS_BOTH,				"Both" },
		{ RESIZE_AXIS_WIDTH,			"Width" },
		{ RESIZE_AXIS_HEIGHT,			"Height" },
		{ RESIZE_AXIS_LONG,				"Long" },
		{ RESIZE_AXIS_SHORT,			"Short" },
		{ RESIZE_AXIS_SCALEDOWN_BOTH,	"ScaleDownBoth" },
		{ RESIZE_AXIS_SCALEDOWN_WIDTH,	"ScaleDownWidth" },
		{ RESIZE_AXIS_SCALEDOWN_HEIGHT,	"ScaleDownHeight" },
		{ RESIZE_AXIS_SCALEDOWN_LONG,	"ScaleDownLong" },
		{ RESIZE_AXIS_SCALEDOWN_SHORT,	"ScaleDownShort" },
	};

	for (int i = 0; i < countof(table); i++) {
		if (axis == table[i].value) {
			return table[i].name;
		}
	}

	static char buf[16];
	snprintf(buf, sizeof(buf), "%u", (uint)axis);
	return buf;
}

// ReductorDiffuse を文字列にする。
// (内部バッファを使う可能性があるため同時に2回呼ばないこと)
const char *
reductordiffuse_tostr(ReductorDiffuse diffuse)
{
	static const struct {
		ReductorDiffuse value;
		const char *name;
	} table[] = {
		{ DIFFUSE_NONE,		"NONE" },
		{ DIFFUSE_SFL,		"SFL" },
		{ DIFFUSE_FS,		"FS" },
		{ DIFFUSE_ATKINSON,	"ATKINSON" },
		{ DIFFUSE_JAJUNI,	"JAJUNI" },
		{ DIFFUSE_STUCKI,	"STUCKI" },
		{ DIFFUSE_BURKES,	"BURKES" },
		{ DIFFUSE_2,		"2" },
		{ DIFFUSE_3,		"3" },
		{ DIFFUSE_RGB,		"RGB" },
	};

	for (int i = 0; i < countof(table); i++) {
		if (diffuse == table[i].value) {
			return table[i].name;
		}
	}

	static char buf[16];
	snprintf(buf, sizeof(buf), "%u", (uint)diffuse);
	return buf;
}

// ColorFormat を文字列にする。
// (内部バッファを使う可能性があるため同時に2回呼ばないこと)
const char *
colorformat_tostr(ColorFormat color)
{
	static const struct {
		ColorFormat value;
		const char *name;
	} table[] = {
		{ COLOR_FMT_GRAY,		"Gray" },
		{ COLOR_FMT_8_RGB,		"8(RGB)" },
		{ COLOR_FMT_16_VGA,		"16(ANSI VGA)" },
		{ COLOR_FMT_256_RGB332,	"256(RGB332)" },
		{ COLOR_FMT_256_XTERM,	"256(xterm)" },
		{ COLOR_FMT_256_ADAPTIVE, "256(Adaptive)" },
	};
	static char buf[16];
	uint type = (uint)color & COLOR_FMT_MASK;

	for (int i = 0; i < countof(table); i++) {
		if (table[i].value == type) {
			if (type == COLOR_FMT_GRAY) {
				uint num = GET_COLOR_COUNT(color);
				snprintf(buf, sizeof(buf), "%s%u", table[i].name, num);
				return buf;
			} else {
				return table[i].name;
			}
		}
	}

	snprintf(buf, sizeof(buf), "0x%x", (uint)color);
	return buf;
}

#endif // SIXELV
