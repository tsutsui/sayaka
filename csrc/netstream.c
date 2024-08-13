/* vi:set ts=4: */
/*
 * Copyright (C) 2024 Tetsuya Isaki
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
// URL のファイルを FILE * にみせる
//

#include "common.h"

#if defined(HAVE_LIBCURL)
// curl の下限は 7.61.0 (2018/07)。CURLINFO_PRETRANSFER_TIME_T のため。

#include <errno.h>
#include <string.h>
#include <curl/curl.h>
#if defined(HAVE_BSD_BSD_H)
#include <bsd/stdio.h>
#endif
#if defined(LIBCURL_HAS_MBEDTLS_BACKEND) && defined(HAVE_MBEDTLS)
#include <mbedtls/ssl.h>
#endif
#if defined(LIBCURL_HAS_OPENSSL_BACKEND) && defined(HAVE_OPENSSL)
#include <openssl/ssl.h>
#endif

struct netstream {
	CURLM *mhandle;
	CURL *curl;

	char *buf;		// realloc する
	uint bufsize;	// 確保してあるバッファサイズ
	uint bufpos;	// buf 中の次回読み出し開始位置
	uint remain;	// buf の読み出し可能な残りバイト数
	bool done;		// 今回のバッファで最後

	const struct diag *diag;
};

static void netstream_global_init(void);
static bool netstream_get_sessioninfo(struct netstream *);
static void netstream_timestamp(struct netstream *);
static int netstream_read_cb(void *, char *, int);
static int netstream_close_cb(void *);
static size_t curl_write_cb(void *, size_t, size_t, void *);

static bool curl_initialized;

// こっちは初回の netstream_open() 内から呼ばれる。
static void
netstream_global_init(void)
{
	if (curl_initialized == false) {
		curl_global_init(CURL_GLOBAL_ALL);
		curl_initialized = true;
	}
}

// こっちはアプリケーション終了時に呼ぶこと。
void
netstream_global_cleanup(void)
{
	if (curl_initialized) {
		curl_global_cleanup();
	}
}

// url をダウンロードしてファイルストリームにして返す。
FILE *
netstream_open(const char *url, const struct netstream_opt *opt,
	const struct diag *diag)
{
	struct netstream *ns = NULL;
	CURLM *mhandle = NULL;
	CURL *curl = NULL;
	FILE *fp;

	netstream_global_init();

	mhandle = curl_multi_init();
	if (mhandle == NULL) {
		Debug(diag, "curl_multi_init() failed");
		goto abort;
	}

	curl = curl_easy_init();
	if (curl == NULL) {
		Debug(diag, "curl_easy_init() failed");
		goto abort;
	}

	ns = calloc(1, sizeof(*ns));
	if (ns == NULL) {
		Debug(diag, "%s: calloc failed: %s", __func__, strerrno());
		goto abort;
	}
	ns->diag = diag;
	ns->mhandle = mhandle;
	ns->curl = curl;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, (long)1);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, (long)1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, ns);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, (long)0);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, (long)0);
	if (opt->use_rsa_only) {
		// 通称 RSA が使えるのは TLSv1.2 以下のみ。
		curl_easy_setopt(curl, CURLOPT_SSLVERSION,
			(long)CURL_SSLVERSION_MAX_TLSv1_2);

		// cipher_list はバックエンドに垂れ流しているだけなので、
		// バックエンドごとに指定方法が違う。うーんこの…。
		// そして現在のバックエンドは文字列判定しか出来ないっぽい。
		curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
		const char *ciphers = NULL;
		if (strncmp(info->ssl_version, "OpenSSL", 7) == 0) {
			ciphers = "AES128-SHA";
		} else {
			Debug(diag, "Not supported backend ssl_version \"%s\"",
				info->ssl_version);
			goto abort;
		}
		curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, ciphers);
	}

	curl_multi_add_handle(mhandle, curl);

	// ここではデータ転送直前までを担当する。
	curl_off_t prexfer = 0;
	do {
		CURLMcode mcode;
		CURLcode r;
		int still_running = 0;

		mcode = curl_multi_poll(mhandle, NULL, 0, 1000, NULL);
		if (mcode != CURLM_OK) {
			Debug(diag, "%s: curl_multi_poll() failed %d",
				__func__, (int)mcode);
			goto abort;
		}

		mcode = curl_multi_perform(mhandle, &still_running);
		if (mcode != CURLM_OK) {
			Debug(diag, "%s: curl_multi_perform() failed %d",
				__func__, (int)mcode);
			goto abort;
		}

		if (still_running == 0) {
			Debug(diag, "%s: not running", __func__);
			goto abort;
		}

		// perform が何か終えて戻ってきたので転送直前まで来てるか調べる。
		r = curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME_T, &prexfer);
		if (r != CURLE_OK) {
			Debug(diag, "%s: CURLINFO_PRETRANSFER_TIME_T failed %d",
				__func__, (int)r);
			goto abort;
		}
	} while (prexfer == 0);

	if (diag_get_level(diag) >= 1) {
		// セッション情報をデバッグ表示。
		netstream_get_sessioninfo(ns);

		// 所要時間を表示。
		netstream_timestamp(ns);
	}

	fp = funopen(ns,
		netstream_read_cb,
		NULL,	// write
		NULL,	// seek
		netstream_close_cb);
	if (fp == NULL) {
		Debug(diag, "%s: funopen failed: %s", __func__, strerrno());
		goto abort;
	}

	return fp;

 abort:
	free(ns);
	if (mhandle && curl) {
		curl_multi_remove_handle(mhandle, curl);
	}
	if (curl) {
		curl_easy_cleanup(curl);
	}
	if (mhandle) {
		curl_multi_cleanup(mhandle);
	}
	return NULL;
}

// 接続中の TLS バージョン等をデバッグ表示する。
// 処理できれば true を返す。
static bool
netstream_get_sessioninfo(struct netstream *ns)
{
	CURL *curl;
	const struct diag *diag;
	struct curl_tlssessioninfo *csession;

	assert(ns);
	curl = ns->curl;
	diag = ns->diag;

	// internals がいつからいつまで有効かは分からないっぽいので、
	// 当たるまで調べる。うーん。
	curl_easy_getinfo(curl, CURLINFO_TLS_SSL_PTR, &csession);
	if (csession->internals == NULL) {
		return false;
	}

	// バックエンドごとの処理。
#if defined(LIBCURL_HAS_MBEDTLS_BACKEND) && defined(HAVE_MBEDTLS)
	if (csession->backend == CURLSSLBACKEND_MBEDTLS) {
		mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)csession->internals;
		diag_print(diag, "Connected %s %s",
			mbedtls_ssl_get_version(ssl),
			mbedtls_ssl_get_ciphersuite(ssl));
	} else
#endif
#if defined(LIBCURL_HAS_OPENSSL_BACKEND) && defined(HAVE_OPENTLS)
	if (csession->backend == CURLSSLBACKEND_OPENSSL) {
		SSL_SESSION *sess = SSL_get_session(csession->internals);
		int ssl_version = SSL_SESSION_get_protocol_version(sess);
		char verbuf[16];
		const char *ver;
		switch (ssl_version) {
		 case SSL3_VERSION:		ver = "SSLv3";		break;
		 case TLS1_VERSION:		ver = "TLSv1.0";	break;
		 case TLS1_1_VERSION:	ver = "TLSv1.1";	break;
		 case TLS1_2_VERSION:	ver = "TLSv1.2";	break;
		 case TLS1_3_VERSION:	ver = "TLSv1.3";	break;
		 default:
			snprintf(verbuf, sizeof(verbuf), "0x%04x", ssl_version);
			ver = verbuf;
			break;
		}

		const SSL_CIPHER *ssl_cipher = SSL_SESSION_get0_cipher(sess);
		const char *cipher_name = SSL_CIPHER_get_name(ssl_cipher);

		diag_print(diag, "Connected %s %s", ver, cipher_name);
	} else
#endif
	{
		// 仕方ないので何か表示しておく。
		diag_print(diag, "Connected");
	}

	return true;
}

// 接続 (主に SSL) にかかった時間を表示したい。
static void
netstream_timestamp(struct netstream *ns)
{
	CURL *curl;
	const struct diag *diag;
	curl_off_t start_time = 0;
	curl_off_t queue_time = -1;
	curl_off_t name_time = -1;
	curl_off_t connect_time = -1;
	curl_off_t appconn_time = -1;

	assert(ns);
	curl = ns->curl;
	diag = ns->diag;

#if defined(CURLINFO_QUEUE_TIME_T)
	curl_easy_getinfo(curl, CURLINFO_QUEUE_TIME_T, &queue_time);
#endif
	curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME_T, &name_time);
	curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME_T, &connect_time);
	curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME_T, &appconn_time);

	// QUEUE_TIME だけ比較的新しい。
	if (queue_time == (curl_off_t)-1) {
		queue_time = start_time;
	}

	if (0) {
		// 生の値
		diag_print(diag,
			"queue=%ju name=%ju conn=%ju app=%ju",
			(uintmax_t)queue_time,
			(uintmax_t)name_time,
			(uintmax_t)connect_time,
			(uintmax_t)appconn_time);
	}

	// いずれも curl_easy_perform() からの積算 [usec] らしいので、
	// 個別の時間に分ける。
	// queue_time はほぼ固定費なので無視。amd64 で 130usec 程度。
	// APPCONNECT は HTTP 時は 0 が返ってくる。

	uint32 name_ms = (uint32)(name_time - queue_time) / 1000;
	uint32 conn_ms = (uint32)(connect_time - name_time) / 1000;
	char appbuf[32];
	appbuf[0] = '\0';
	if (appconn_time != 0) {
		uint32 app_ms = (uint32)(appconn_time - connect_time) / 1000;
		snprintf(appbuf, sizeof(appbuf), " appconn=%u", app_ms);
	}

	diag_print(diag,
		"Connect profile: namelookup=%u connect=%u%s [msec]",
		name_ms,
		conn_ms,
		appbuf);
}

static int
netstream_read_cb(void *arg, char *dst, int dstsize)
{
	struct netstream *ns = (struct netstream *)arg;
	int still_running;
	CURLMcode mcode;
	const struct diag *diag = ns->diag;

	Trace(diag, "%s: dstsize=%d remain=%u", __func__, dstsize, ns->remain);
	while (ns->remain == 0) {
		// 内部バッファが空なら、次の読み込みを試行。

		// 終了フラグが立っていれば EOF。
		if (ns->done) {
			Trace(diag, "%s: EOF", __func__);
			return 0;
		}

		// 何か起きるかタイムアウトするまで待つ。
		// 何か起きたかタイムアウトしたかは分からないっぽい。
		mcode = curl_multi_poll(ns->mhandle, NULL, 0, 1000, NULL);
		if (mcode != CURLM_OK) {
			Debug(diag, "%s: curl_multi_poll() failed %d",
				__func__, (int)mcode);
			return -1;
		}

		// 何か起きたかも知れないので実行する。
		// (ここでデータが届いていれば curl_write_cb() が呼ばれる)
		still_running = 0;
		mcode = curl_multi_perform(ns->mhandle, &still_running);
		if (mcode != CURLM_OK) {
			Debug(diag, "%s: curl_multi_perform() failed %d",
				__func__, (int)mcode);
			return -1;
		}

		// これで最後か。
		if (still_running == 0) {
			int rc;
			CURLMsg *msg = curl_multi_info_read(ns->mhandle, &rc);
			if (msg) {
				if (msg->data.result != CURLE_OK) {
					Debug(diag, "%s: curl_multi_info_read() returns %d",
						__func__, (int)msg->data.result);
					return -1;
				}
				ns->done = true;
			}
		}
	}

	// 内部バッファにあるのを吐き出すまで使う。
	uint len = MIN(ns->remain, dstsize);
	Trace(diag, "%s: len=%u", __func__, len);
	memcpy(dst, ns->buf + ns->bufpos, len);
	ns->bufpos += len;
	ns->remain -= len;

	return len;
}

static int
netstream_close_cb(void *arg)
{
	struct netstream *ns = (struct netstream *)arg;

	if (ns) {
		free(ns->buf);
		if (ns->mhandle && ns->curl) {
			curl_multi_remove_handle(ns->mhandle, ns->curl);
		}
		if (ns->curl) {
			curl_easy_cleanup(ns->curl);
		}
		if (ns->mhandle) {
			curl_multi_cleanup(ns->mhandle);
		}

		free(ns);
	}

	return 0;
}

// curl からのダウンロードコールバック関数
static size_t
curl_write_cb(void *src, size_t size, size_t nmemb, void *arg)
{
	struct netstream *ns = (struct netstream *)arg;

	uint newsize = (uint)(size * nmemb);
	if (newsize > ns->bufsize) {
		char *newbuf = realloc(ns->buf, newsize);
		if (newbuf == NULL) {
			// と言われても何も出来ることはない気が…
			Debug(ns->diag, "%s: realloc(%u) failed: %s",
				__func__, newsize, strerrno());
			return 0;
		}
		ns->buf = newbuf;
		ns->bufsize = newsize;
		Trace(ns->diag, "%s: realloc %u", __func__, newsize);
	}

	memcpy(ns->buf, src, newsize);
	ns->bufpos = 0;
	ns->remain = newsize;
	Trace(ns->diag, "%s: produce %u", __func__, newsize);

	return nmemb;
}

#else /* HAVE_LIBCURL */

// libcurl がない時でもクリーンアップだけは字面上必要。
void
netstream_global_cleanup(void)
{
}

#endif /* HAVE_LIBCURL */

// opt を初期化する。
void
netstream_opt_init(struct netstream_opt *opt)
{
	opt->use_rsa_only = false;
}
