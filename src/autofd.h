/*
 * Copyright (C) 2020 Tetsuya Isaki
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

#include <cstdio>
#include <unistd.h>

// 自動変数みたいな生存期間を持つディスクリプタ
class autofd
{
 public:
	autofd() {
		fd = -1;
	}
	autofd(int fd_) {
		fd = fd_;
	}

	~autofd() {
		if (fd >= 0)
			close(fd);
	}

	autofd& operator=(int fd_) {
		fd = fd_;
		return *this;
	}
	operator int() const {
		return fd;
	}

	// 明示的にクローズする
	int Close() {
		int r = 0;
		if (fd >= 0) {
			r = close(fd);
			fd = -1;
		}
		return r;
	}

 private:
	int fd;
};

// 自動変数みたいな生存期間を持つ FILE ポインタ
class AutoFILE
{
 public:
	AutoFILE() { }
	AutoFILE(FILE *fp_) {
		fp = fp_;
	}

	~AutoFILE() {
		if (Valid()) {
			fclose(fp);
		}
	}

	AutoFILE& operator=(FILE *fp_) {
		fp = fp_;
		return *this;
	}
	operator FILE*() const {
		return fp;
	}

	// if (fp == NULL) の比較に相当するのを字面のまま真似するのは
	// いまいちな気がするので、Valid() を用意。
	bool Valid() const {
		return (fp != NULL);
	}

	// 明示的にクローズする
	int Close() {
		int r = 0;
		if (Valid()) {
			r = fclose(fp);
			fp = NULL;
		}
		return r;
	}

 private:
	FILE *fp {};
};
