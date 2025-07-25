/* vi:set ts=4: */
/*
 * Copyright (C) 2021-2024 Tetsuya Isaki
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
// sayaka, sixelv 共通ヘッダ
//

#ifndef sayaka_common_h
#define sayaka_common_h

#include "header.h"
#include <assert.h>
#include <stdio.h>

#define CAN "\x18"
#define ESC "\x1b"
#define CSI ESC "["
#define ESCchar '\x1b'

#define Debug(diag_, fmt...)	do {	\
	if (diag_get_level(diag_) >= 1)	\
		diag_print(diag_, fmt);	\
} while (0)

#define Trace(diag_, fmt...)	do {	\
	if (diag_get_level(diag_) >= 2)	\
		diag_print(diag_, fmt);	\
} while (0)

#define Verbose(diag_, fmt...)	do {	\
	if (diag_get_level(diag_) >= 3)	\
		diag_print(diag_, fmt);	\
} while (0)

struct net;

struct diag
{
	int level;
	bool timestamp;
};

struct net_opt {
	// 接続に使用するプロトコル。
	// 0 なら指定なし、4 なら IPv4、6 なら IPv6。
	int address_family;

	// 接続に使用する cipher suites を RSA_WITH_AES_128_CBC_SHA に限定する。
	bool use_rsa_only;

	// 接続タイムアウト [msec]。
	// 0 ならタイムアウトしない (デフォルト)。
	uint timeout_msec;
};

// コマンドラインオプション文字列のデコード用
struct optmap {
	const char *name;
	int value;
};

// 文字列型。sayaka.h の ustring と揃えること。
struct string_ {
	char *buf;		// len == 0 の時 buf を触らないこと。
	uint len;		// 文字列の長さ ('\0' の位置)
	uint capacity;	// 確保してあるバイト数
};
typedef struct string_ string;

struct urlinfo {
	string *scheme;
	string *host;
	string *port;
	string *user;
	string *password;
	string *pqf;
};

// diag.c
static inline int diag_get_level(const struct diag *diag)
{
	return diag->level;
}
extern struct diag *diag_alloc(void);
extern void diag_free(struct diag *);
extern void diag_set_level(struct diag *, int);
extern void diag_set_timestamp(struct diag *, bool);
extern void diag_print(const struct diag *, const char *, ...)
	__attribute__((format(printf, 2, 3)));

// httpclient.c
extern struct httpclient *httpclient_create(const struct diag *);
extern void httpclient_destroy(struct httpclient *);
extern int  httpclient_connect(struct httpclient *, const char *,
	const struct net_opt *);
extern const char *httpclient_get_resmsg(const struct httpclient *);
extern FILE *httpclient_fopen(struct httpclient *);
extern void diag_http_header(const struct diag *, const string *);

// net.c
extern struct urlinfo *urlinfo_parse(const char *);
extern void urlinfo_free(struct urlinfo *);
extern void urlinfo_update_path(struct urlinfo *, const struct urlinfo *);
extern string *urlinfo_to_string(const struct urlinfo *);
extern void net_opt_init(struct net_opt *);
extern struct net *net_create(const struct diag *);
extern void net_destroy(struct net *);
extern int  net_connect(struct net *, const char *, const char *, const char *,
	const struct net_opt *);
extern string *net_gets(struct net *);
extern int  net_read(struct net *, void *, uint);
extern int  net_write(struct net *, const void *, uint);
extern void net_shutdown(struct net *);
extern void net_close(struct net *);
extern int  net_get_fd(const struct net *);

// pstream.c
extern struct pstream *pstream_init_fp(FILE *);
extern struct pstream *pstream_init_fd(int);
extern void pstream_cleanup(struct pstream *);
extern FILE *pstream_open_for_peek(struct pstream *);
extern FILE *pstream_open_for_read(struct pstream *);

// string.c
extern string *string_init(void);
extern string *string_alloc(uint);
extern string *string_from_cstr(const char *);
extern string *string_from_mem(const void *, uint);
extern string *string_dup(const string *);
extern string *string_fgets(FILE *);
extern bool string_realloc(string *, uint);
extern void string_free(string *);
extern const char *string_get(const string *);
extern char *string_get_buf(const string *);
extern bool string_equal(const string *, const string *);
extern bool string_equal_cstr(const string *, const char *);
extern void string_append_char(string *, char);
extern void string_append_cstr(string *, const char *);
extern void string_append_mem(string *, const void *, uint);
extern void string_append_printf(string *, const char *, ...)
	__attribute__((format(printf, 2, 3)));
extern void string_rtrim_inplace(string *);
static inline uint string_len(const string *s) {
	assert(s);
	return s->len;
}
static inline void string_clear(string *s) {
	assert(s);
	s->len = 0;
}

// util.c
extern const char *strerrno(void);
extern void chomp(char *);
extern int  parse_optmap(const struct optmap *, const char *);
extern uint32 stou32def(const char *, uint32, char **);
extern uint32 stox32def(const char *, uint32, char **);
extern uint putd_fast(char *, uint);
static inline uint putd_inline(char *dst, uint n)
{
	if (__predict_true(n < 10)) {
		dst[0] = n + '0';
		return 1;
	} else {
		return putd_fast(dst, n);
	}
}
#define PUTD(buf, n)	putd_inline(buf, n)
struct timespec;
extern uint64 timespec_to_usec(const struct timespec *);
extern uint64 timespec_to_msec(const struct timespec *);

// main
extern const char progname[];
extern const char progver[];

#endif // !sayaka_common_h
