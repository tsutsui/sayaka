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

typedef struct diag_
{
	int level;
	bool timestamp;
	char name[32];
} diag;
struct net;
typedef struct pstream_ pstream;
typedef struct string_ string;

// diag.c
static inline int diag_get_level(const diag *diag)
{
	return diag->level;
}
extern diag *diag_alloc(void);
extern void diag_free(diag *);
extern void diag_set_name(diag *, const char *);
extern void diag_set_level(diag *, int);
extern void diag_set_timestamp(diag *, bool);
extern void diag_print(const diag *, const char *, ...)
	__attribute__((format(printf, 2, 3)));

// net.c
extern struct net *net_create(const diag *);
extern void net_destroy(struct net *);
extern bool net_connect(struct net *, const char *, const char *, const char *);
extern ssize_t net_read(struct net *, void *, size_t);
extern ssize_t net_write(struct net *, const void *, size_t);
extern void net_close(struct net *);
extern int  net_get_fd(const struct net *);

// netstream.c
struct netstream_opt {
	// 接続に使用する cipher suites を RSA_WITH_AES_128_CBC_SHA に限定する。
	bool use_rsa_only;
};
extern void netstream_opt_init(struct netstream_opt *);
extern struct netstream *netstream_init(const diag *);
extern void netstream_cleanup(struct netstream *);
extern int  netstream_connect(struct netstream *, const char *,
	const struct netstream_opt *);
extern FILE *netstream_fopen(struct netstream *);
extern void netstream_global_cleanup(void);

// pstream.c
extern pstream *pstream_init_fp(FILE *);
extern pstream *pstream_init_fd(int);
extern void pstream_cleanup(pstream *);
extern FILE *pstream_open_for_peek(pstream *);
extern FILE *pstream_open_for_read(pstream *);

// string.c
extern string *string_init(void);
extern string *string_alloc(uint);
extern string *string_from_cstr(const char *);
extern string *string_from_mem(const void *, uint);
extern bool string_realloc(string *, uint);
extern void string_free(string *);
extern const char *string_get(const string *);
extern char *string_get_buf(const string *);
extern uint string_len(const string *);
extern bool string_equal(const string *, const string *);
extern bool string_equal_cstr(const string *, const char *);
extern void string_clear(string *);
extern void string_append_char(string *, char);
extern void string_append_cstr(string *, const char *);
extern void string_append_mem(string *, const void *, uint);
extern void string_append_printf(string *, const char *, ...)
	__attribute__((format(printf, 2, 3)));

// util.c
extern const char *strerrno(void);
extern void chomp(char *);
extern uint32 stou32def(const char *, uint32, char **);
#if defined(__OpenBSD__)
extern uint putd(char *, uint, uint);
#define PUTD(buf, n, bufsize)	putd(buf, n, bufsize)
#else
extern uint putd(char *, uint);
#define PUTD(buf, n, bufsize)	putd(buf, n)
#endif

#endif // !sayaka_common_h
