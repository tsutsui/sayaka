/*
 * Copyright (C) 2015 Y.Sugahara (moveccr)
 * Copyright (C) 2021 Tetsuya Isaki
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

#include "FileStream.h"
#include "Image.h"
#include "ImageLoaderJPEG.h"
#include "SixelConverter.h"
#include "StringUtil.h"
#include "sayaka.h"
#include <cassert>
#include <cstring>
#include <errno.h>

// コンストラクタ
SixelConverter::SixelConverter()
{
	diag.SetClassname("SixelConverter");
}

// コンストラクタ
SixelConverter::SixelConverter(int debuglv)
	: SixelConverter()
{
	diag.SetLevel(debuglv);
	ir.Init(diag);
}

// stream から画像を img に読み込む
bool
SixelConverter::LoadFromStream(InputStream *stream)
{
	Debug(diag, "ResizeMode=%d", ResizeMode);

	{
		ImageLoaderJPEG loader(stream, diag);
		if (loader.Check()) {
			if (loader.Load(img)) {
				LoadAfter();
				return true;
			}
			return false;
		}
	}

	printf("Unknown picture format");
	return false;
}

void
SixelConverter::LoadAfter()
{
	Width  = img.GetWidth();
	Height = img.GetHeight();

	Debug(diag, "Size=(%d,%d) bits=%d nCh=%d rowstride=%d",
		Width,
		Height,
		img.GetBitsPerPixel(),
		img.GetChPerPixel(),
		img.GetStride());
}


// リサイズ計算
void
SixelConverter::CalcResize(int *widthp, int *heightp)
{
	int& width = *widthp;
	int& height = *heightp;

	auto ra = ResizeAxis;
	bool scaledown =
		(ra == ResizeAxisMode::ScaleDownBoth)
	 || (ra == ResizeAxisMode::ScaleDownWidth)
	 || (ra == ResizeAxisMode::ScaleDownHeight)
	 || (ra == ResizeAxisMode::ScaleDownLong)
	 || (ra == ResizeAxisMode::ScaleDownShort);

	// 条件を丸めていく
	switch (ra) {
	 case ResizeAxisMode::Both:
	 case ResizeAxisMode::ScaleDownBoth:
		if (ResizeWidth == 0) {
			ra = ResizeAxisMode::Height;
		} else if (ResizeHeight == 0) {
			ra = ResizeAxisMode::Width;
		} else {
			ra = ResizeAxisMode::Both;
		}
		break;

	 case ResizeAxisMode::Long:
	 case ResizeAxisMode::ScaleDownLong:
		if (Width >= Height) {
			ra = ResizeAxisMode::Width;
		} else {
			ra = ResizeAxisMode::Height;
		}
		break;

	 case ResizeAxisMode::Short:
	 case ResizeAxisMode::ScaleDownShort:
		if (Width <= Height) {
			ra = ResizeAxisMode::Width;
		} else {
			ra = ResizeAxisMode::Height;
		}
		break;

	 case ResizeAxisMode::ScaleDownWidth:
		ra = ResizeAxisMode::Width;
		break;

	 case ResizeAxisMode::ScaleDownHeight:
		ra = ResizeAxisMode::Height;
		break;

	 default:
		__builtin_unreachable();
		break;
	}

	auto rw = ResizeWidth;
	auto rh = ResizeHeight;

	if (rw <= 0)
		rw = Width;
	if (rh <= 0)
		rh = Height;

	// 縮小のみ指示
	if (scaledown) {
		if (Width < rw)
			rw = Width;
		if (Height < rh)
			rh = Height;
	}

	// 確定したので計算
	switch (ra) {
	 case ResizeAxisMode::Both:
		width = rw;
		height = rh;
		break;
	 case ResizeAxisMode::Width:
		width = rw;
		height = Height * width / Width;
		break;
	 case ResizeAxisMode::Height:
		height = rh;
		width = Width * height / Height;
		break;
	 default:
		__builtin_unreachable();
		break;
	}
}

//
// ----- 前処理
//

// インデックスカラーに変換する。
void
SixelConverter::ConvertToIndexed()
{
	// リサイズ
	int width = 0;
	int height = 0;
	CalcResize(&width, &height);

	Debug(diag, "resize to (width=%d height=%d)", width, height);

	Width = width;
	Height = height;

	Indexed.resize(Width * Height);

	Debug(diag, "SetColorMode(%s, %s, %d)",
		ImageReductor::RCM2str(ColorMode),
		ImageReductor::RFM2str(FinderMode),
		GrayCount);
	ir.SetColorMode(ColorMode, FinderMode, GrayCount);

	Debug(diag, "SetAddNoiseLevel=%d", AddNoiseLevel);
	ir.SetAddNoiseLevel(AddNoiseLevel);

	Debug(diag, "ReduceMode=%s", ImageReductor::RRM2str(ReduceMode));
	ir.Convert(ReduceMode, img, Indexed, Width, Height);
	Debug(diag, "Converted");
}

//
// ----- Sixel 出力
//

#define ESC "\x1b"
#define DCS ESC "P"

// Sixel の開始コードとパレットを文字列で返す。
std::string
SixelConverter::SixelPreamble()
{
	std::string linebuf;

	// Sixel 開始コード
	linebuf += DCS;
	linebuf += string_format("7;%d;q\"1;1;%d;%d", OutputMode, Width, Height);

	// パレットを出力
	if (OutputPalette) {
		for (int i = 0; i < ir.GetPaletteCount(); i++) {
			const auto& col = ir.GetPalette(i);
			linebuf += string_format("#%d;%d;%d;%d;%d", i, 2,
				col.r * 100 / 255,
				col.g * 100 / 255,
				col.b * 100 / 255);
		}
	}

	return linebuf;
}

static int
MyLog2(int n)
{
	for (int i = 0; i < 8; i++) {
		if (n <= (1 << i)) {
			return i;
		}
	}
	return 8;
}

// OR モードで Sixel コア部分を stream に出力する。
void
SixelConverter::SixelToStreamCore_ORmode(OutputStream *stream)
{
	uint8 *p0 = Indexed.data();
	int w = Width;
	int h = Height;

	// パレットのビット数
	int bcnt = MyLog2(ir.GetPaletteCount());
	Debug(diag, "%s bcnt=%d\n", __func__, bcnt);

	uint8 sixelbuf[(w + 5) * bcnt];

	uint8 *p = p0;
	int y;
	// 一つ手前の SIXEL 行まで変換
	for (y = 0; y < h - 6; y += 6) {
		int len = sixel_image_to_sixel_h6_ormode(sixelbuf, p, w, 6, bcnt);
		stream->Write(sixelbuf, len);
		stream->Flush();
		p += w * 6;
	}
	// 最終 SIXEL 行を変換
	int len = sixel_image_to_sixel_h6_ormode(sixelbuf, p, w, h - y, bcnt);
	stream->Write(sixelbuf, len);
	stream->Flush();
}

// Sixel コア部分を stream に出力する。
void
SixelConverter::SixelToStreamCore(OutputStream *stream)
{
	// 030 ターゲット

	uint8 *p0 = Indexed.data();
	int w = Width;
	int h = Height;
	int src = 0;

	int PaletteCount = ir.GetPaletteCount();
	Debug(diag, "%s PaletteCount=%d", __func__, PaletteCount);

	// カラー番号ごとの、X 座標の min, max を計算する。
	// short でいいよね…
	int16 min_x[PaletteCount];
	int16 max_x[PaletteCount];

	for (int16 y = 0; y < h; y += 6) {
		std::string linebuf;

		src = y * w;

		memset(min_x, -1, sizeof(min_x));
		memset(max_x,  0, sizeof(max_x));

		// h が 6 の倍数でない時には溢れてしまうので、上界を計算する
		int16 max_dy = 6;
		if (y + max_dy > h) {
			max_dy = (int16)(h - y);
		}

		// 各カラーの X 座標範囲を計算する
		for (int16 dy = 0; dy < max_dy; dy++) {
			for (int16 x = 0; x < w; x++) {
				uint8 I = p0[src++];
				if (min_x[I] < 0 || min_x[I] > x)
					min_x[I] = x;
				if (max_x[I] < x)
					max_x[I] = x;
			}
		}

		for (;;) {
			// 出力するべきカラーがなくなるまでのループ
			Trace(diag, "for1");
			int16 mx = -1;

			for (;;) {
				// 1行の出力で出力できるカラーのループ
				Trace(diag, "for2");

				uint8 min_color = 0;
				int16 min = INT16_MAX;

				// min_x から、mx より大きいもののうち最小のカラーを探して、
				// 塗っていく
				for (int16 c = 0; c < PaletteCount; c++) {
					if (mx < min_x[c] && min_x[c] < min) {
						min_color = (uint8)c;
						min = min_x[c];
					}
				}
				// なければ抜ける
				if (min_x[min_color] <= mx) {
					break;
				}

				// Sixel に色コードを出力
				linebuf += string_format("#%d", min_color);

				// 相対 X シーク処理
				int16 space = min_x[min_color] - (mx + 1);
				if (space > 0) {
					linebuf += SixelRepunit(space, 0);
				}

				// パターンが変わったら、それまでのパターンを出していく
				// アルゴリズム
				uint8 prev_t = 0;
				int16 n = 0;
				for (int16 x = min_x[min_color]; x <= max_x[min_color]; x++) {
					uint8 t = 0;
					for (int16 dy = 0; dy < max_dy; dy++) {
						uint8 I = p0[(y + dy) * w + x];
						if (I == min_color) {
							t |= 1 << dy;
						}
					}

					if (prev_t != t) {
						if (n > 0) {
							linebuf += SixelRepunit(n, prev_t);
						}
						prev_t = t;
						n = 1;
					} else {
						n++;
					}
				}
				// 最後のパターン
				if (prev_t != 0 && n > 0) {
					linebuf += SixelRepunit(n, prev_t);
				}

				// X 位置を更新
				mx = max_x[min_color];
				// 済んだ印
				min_x[min_color] = -1;
			}

			linebuf += '$';

			// 最後までやったら抜ける
			if (mx == -1)
				break;
		}

		linebuf += '-';

		stream->Write(linebuf);
		stream->Flush();
	}
}

// Sixel の終了コードを文字列で返す
std::string
SixelConverter::SixelPostamble()
{
	return ESC "\\";
}

// Sixel を stream に出力する
void
SixelConverter::SixelToStream(OutputStream *stream)
{
	Debug(diag, "%s", __func__);
	assert(ir.GetPaletteCount() != 0);

	// 開始コードとかの出力
	stream->Write(SixelPreamble());

	if (OutputMode == SixelOutputMode::Or) {
		SixelToStreamCore_ORmode(stream);
	} else {
		SixelToStreamCore(stream);
	}

	stream->Write(SixelPostamble());
	stream->Flush();
}

// 繰り返しのコードを考慮して、Sixel パターン文字列を返す
/*static*/ std::string
SixelConverter::SixelRepunit(int n, uint8 ptn)
{
	std::string v;

	if (n >= 4) {
		v = string_format("!%d%c", n, ptn + 0x3f);
	} else {
		v = std::string(n, ptn + 0x3f);
	}
	return v;
}

//
// enum を文字列にしたやつ orz
//

static const char *SRM2str_[] = {
	"ByLoad",
	"ByImageReductor",
};

/*static*/ const char *
SixelConverter::SOM2str(SixelOutputMode val)
{
	if (val == Normal)	return "Normal";
	if (val == Or)		return "Or";
	return "?";
}

/*static*/ const char *
SixelConverter::SRM2str(SixelResizeMode val)
{
	return ::SRM2str_[(int)val];
}

#if defined(SELFTEST)
#include "test.h"
static void
test_enum()
{
	std::vector<std::pair<SixelOutputMode, const std::string>> table_SOM = {
		{ SixelOutputMode::Normal,			"Normal" },
		{ SixelOutputMode::Or,				"Or" },
	};
	for (const auto& a : table_SOM) {
		const auto n = a.first;
		const auto& exp = a.second;
		std::string act(SixelConverter::SOM2str(n));
		xp_eq(exp, act, exp);
	}

	std::vector<std::pair<SixelResizeMode, const std::string>> table_SRM = {
		{ SixelResizeMode::ByLoad,			"ByLoad" },
		{ SixelResizeMode::ByImageReductor,	"ByImageReductor" },
	};
	for (const auto& a : table_SRM) {
		const auto n = a.first;
		const auto& exp = a.second;
		std::string act(SixelConverter::SRM2str(n));
		xp_eq(exp, act, exp);
	}
}

void
test_SixelConverter()
{
	test_enum();
}
#endif
