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
