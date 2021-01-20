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

#pragma once

#include <string>

class Diag
{
	// 分類名
	std::string classname {};

	// レベル。目安と後方互換製を兼ねて
	// 0: なし
	// 1: デバッグ
	// 2: トレース
	// 3: うるさい
	// としておく。
	int debuglevel {};

 public:
	Diag();
	Diag(const std::string& name);

	void SetClassname(const std::string& name);

	// デバッグレベルを設定する
	void SetLevel(int lv);
	// デバッグレベル取得
	int GetLevel() const { return debuglevel; }

	// (int) 評価するとデバッグレベルを返すと便利
	operator int() const { return GetLevel(); }

	// レベル可変のメッセージ出力 (改行はこちらで付加する)
	void Print(int lv, const char *fmt, ...);

	// レベル不問のメッセージ出力 (改行はこちらで付加する)
	// 呼び出し側でレベルを判定した後で使う
	void Print(const char *fmt, ...);

	// デバッグログ表示 (改行はこちらで付加する)
	void Debug(const char *fmt, ...);
	// トレースログ表示 (改行はこちらで付加する)
	void Trace(const char *fmt, ...);
	// 詳細ログ表示 (改行はこちらで付加する)
	void Verbose(const char *fmt, ...);
};
