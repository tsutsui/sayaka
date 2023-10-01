/*
 * Copyright (C) 2023 Tetsuya Isaki
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

#pragma once

#include <string>

class Blurhash
{
	struct ColorF;
 public:
	Blurhash();

	// ハッシュを指定したコンストラクタ。
	Blurhash(const std::string& hash_);

	// hash が正しそうなら true を返す。
	bool IsValid() const;

	// hash を RGB にデコードして dst に書き出す。
	// dst は width * height * 3 バイト確保してあること。
	bool Decode(uint8 *dst, int width, int height);

 private:
	int Decode83(int pos, int len) const;
	void DecodeDC(ColorF *col, int val) const;
	void DecodeAC(ColorF *col, int val) const;
	static float DecodeMaxAC(int val);
	static float SRGBToLinear(int val);
	static int LinearToSRGB(float val);
	static float SignPow(float val, float exp);
	void BasesFor(std::vector<float>& bases, int pixels, int comp);

	std::string hash {};
	float maxvalue {};
};