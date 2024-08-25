/* vi:set ts=4: */
/*
 * Copyright (C) 2023-2024 Tetsuya Isaki
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
// Misskey
//

#include "sayaka.h"
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool misskey_init(void);
static bool misskey_stream(wsclient *);
static void misskey_recv_cb(const string *);
static void misskey_message(string *);
static bool misskey_show_note(const json *, int, uint);
static bool misskey_show_announcement(const json *, int);
static void misskey_show_icon(const json *, int, const string *);
static bool misskey_show_photo(const json *, int, int);
static void misskey_print_filetype(const json *, int, const char *);
static void make_cache_filename(char *, uint, const char *);
static string *string_unescape_c(const char *);
static string *misskey_format_time(const json *, int);
static string *misskey_format_renote_count(const json *, int);
static string *misskey_format_reaction_count(const json *, int);
static string *misskey_format_renote_owner(const json *, int);
static const char *misskey_get_user(const json *, int, string *, const char **);

static json *js;

// サーバ接続とローカル再生との共通の初期化。
static bool
misskey_init(void)
{
	js = json_create(diag_json);
	if (js == NULL) {
		return false;
	}

	return true;
}

static void
misskey_cleanup(void)
{
	json_destroy(js);
}

void
cmd_misskey_play(const char *infile)
{
	char buf[8192];	// XXX
	FILE *fp;

	misskey_init();

	if (infile == NULL) {
		fp = stdin;
	} else {
		fp = fopen(infile, "r");
		if (fp == NULL) {
			err(1, "%s", infile);
		}
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		string *s = string_from_cstr(buf);
		misskey_message(s);
		string_free(s);
	}

	if (infile != NULL) {
		fclose(fp);
	}

	misskey_cleanup();
}

void
cmd_misskey_stream(const char *server)
{
	const diag *diag = diag_net;
	char url[strlen(server) + 20];

	misskey_init();

	snprintf(url, sizeof(url), "wss://%s/streaming", server);

	printf("Ready...");
	fflush(stdout);

	// -1 は初回。0 は EOF による正常リトライ。
	int retry_count = -1;
	bool terminate = false;
	for (;;) {
		if (retry_count > 0) {
			time_t now;
			struct tm tm;
			char timebuf[16];

			time(&now);
			localtime_r(&now, &tm);
			strftime(timebuf, sizeof(timebuf), "%T", &tm);
			printf("%s Retrying...", timebuf);
			fflush(stdout);
		}

		wsclient *ws = wsclient_create(diag);
		if (ws == NULL) {
			Debug(diag, "%s: wsclient_create failed", __func__);
			goto abort;
		}

		if (wsclient_init(ws, misskey_recv_cb) == false) {
			Debug(diag, "%s: wsclient_init failed", __func__);
			goto abort;
		}

		if (wsclient_connect(ws, url) != 0) {
			Debug(diag, "%s: %s: wsclient_connect failed", __func__, server);
			goto abort;
		}

		// 接続成功。
		// 初回とリトライ時に表示。EOF 後の再接続では表示しない。
		if (retry_count != 0) {
			printf("Connected\n");
		}

		// メイン処理。
		if (misskey_stream(ws) == false) {
			// エラーなら終了。メッセージは表示済み。
			terminate = true;
			goto abort;
		}

		retry_count = 0;
 abort:
		wsclient_destroy(ws);
		if (terminate) {
			break;
		}
		// 初回で失敗か、リトライ回数を超えたら終了。
		if (retry_count < 0) {
			break;
		}
		if (++retry_count >= 5) {
			warnx("Gave up reconnecting.");
			break;
		}
		sleep(1 << retry_count);
	}

	misskey_cleanup();
}

// Misskey Streaming の接続後メインループ。定期的に切れるようだ。
// 相手からの Connection Close なら true を返す。
// エラー (おそらく復旧不可能)なら false を返す。
static bool
misskey_stream(wsclient *ws)
{
	// コマンド送信。
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "{\"type\":\"connect\",\"body\":{"
		"\"channel\":\"localTimeline\",\"id\":\"sayaka-%08x\"}}",
		rnd_get32());

	if (wsclient_send_text(ws, cmd) < 0) {
		warn("%s: Sending command failed", __func__);
		return false;
	}

	// あとは受信。メッセージが出来ると misskey_recv_cb() が呼ばれる。
	for (;;) {
		int r = wsclient_process(ws);
		if (__predict_false(r <= 0)) {
			if (r < 0) {
				warn("%s: wsclient_process failed", __func__);
				break;
			} else {
				// EOF
				return true;
			}
		}
	}

	return false;
}

// サーバから1メッセージ (以上?)を受信したコールバック。
static void
misskey_recv_cb(const string *msg)
{
	// 録画。
	if (__predict_false(opt_record_file)) {
		FILE *fp = fopen(opt_record_file, "a");
		if (fp) {
			fputs(string_get(msg), fp);
			fputc('\n', fp);
			fclose(fp);
		}
	}

	misskey_message(UNCONST(msg));
}

// 1メッセージの処理。ここからストリーミングとローカル再生共通。
static void
misskey_message(string *jsonstr)
{
	int n = json_parse(js, jsonstr);
	Debug(diag_json, "token = %d\n", n);

	//json_jsmndump(js);

	// ストリームから来る JSON は以下のような構造。
	// {
	//   "type":"channel",
	//   "body":{
	//     "id":"ストリーム開始時に指定した ID",
	//     "type":"note",
	//     "body":{ ノート本体 }
	//   }
	// }
	// {
	//   "type":"emojiUpdated",
	//   "body":{ }
	// }
	// {
	//   "type":"announcementCreated",
	//   "body":{
	//     "announcement": { }
	//   }
	// } とかいうのも来たりする。
	//
	// ストリームじゃないところで取得したノートを流し込んでも
	// そのまま見えると嬉しいので、皮をむいたやつを次ステージに渡す。

	int id = 0;
	for (;;) {
		const char *typestr = json_obj_find_cstr(js, id, "type");
		int bodyid = json_obj_find_obj(js, id, "body");
		if (typestr != NULL && bodyid >= 0) {
			if (strcmp(typestr, "channel") == 0 ||
				strcmp(typestr, "note") == 0 ||
				strcmp(typestr, "announcementCreated") == 0)
			{
				// "body" の下へ。
				id = bodyid;
			} else if (strncmp(typestr, "emoji", 5) == 0) {
				// emoji{Added,Deleted} とかは無視でいい。
				return;
			} else {
				// 知らないタイプは無視。
				warnx("Unknown message type \"%s\"", typestr);
				return;
			}
		} else {
			// ここが本文っぽい。
			break;
		}
	}

	bool crlf = misskey_show_note(js, id, 0);
	if (crlf) {
		printf("\n");
	}
}

// 1ノートを処理する。
static bool
misskey_show_note(const json *js, int inote, uint depth)
{
	//json_dump(js, inote);
	assert(json_is_obj(js, inote));

	// acl

	// 録画?
	// 階層変わるのはどうする?

	// NG ワード

	// アナウンスなら別処理。
	int iann = json_obj_find_obj(js, inote, "announcement");
	if (iann >= 0) {
		return misskey_show_announcement(js, iann);
	}

	// 地文なら note == renote。
	// リノートなら RN 元を note、RN 先を renote。
	bool has_renote;
	int irenote = json_obj_find_obj(js, inote, "renote");
	if (irenote >= 0) {
		// XXX text があったらどうするのかとか。
		has_renote = true;
	} else {
		irenote = inote;
		has_renote = false;
	}

	// --nsfw=hide なら、添付ファイルに isSensitive が一つでも含まれていれば
	// このノート自体を表示しない。
	int ifiles = json_obj_find(js, irenote, "files");
	if (opt_nsfw == NSFW_HIDE) {
		bool has_sensitive = false;
		if (ifiles >= 0) {
			JSON_ARRAY_FOR(ifile, js, ifiles) {
				has_sensitive |= json_obj_find_bool(js, ifile, "isSensitive");
			}
		}
		if (has_sensitive) {
			return false;
		}
	}

	// 1行目は名前、アカウント名など。
	int iuser = json_obj_find_obj(js, irenote, "user");
	string *userid = string_alloc(64);
	const char *instance = NULL;
	const char *name = misskey_get_user(js, irenote, userid, &instance);
	ustring *headline = ustring_alloc(64);
	ustring_append_utf8_color(headline, name, COLOR_USERNAME);
	ustring_append_unichar(headline, ' ');
	ustring_append_utf8_color(headline, string_get(userid), COLOR_USERID);
	if (instance) {
		ustring_append_unichar(headline, ' ');
		ustring_append_utf8_color(headline, instance, COLOR_USERNAME);
	}

	// 本文。
	// cw	text	--show-cw	result
	// ----	----	---------	-------
	// -	y		n			text
	// -	y		y			text
	// y	*		n			cw [CW]
	// y	*		y			cw [CW] text

	// jsmn はテキストを一切加工しないので、例えば改行文字は JSON エンコードに
	// 従って '\' 'n' の2文字のまま。本文中の改行は画面でも改行してほしいので
	// ここでエスケープを処理する。一方、名前中の改行('\' 'n' の2文字) は
	// 都合がいいのでそのままにしておく (JSON パーサがテキストをデコードして
	// いた場合にはこっちを再エスケープするはずだった)。

	const char *c_text = json_obj_find_cstr(js, irenote, "text");
	string *text = string_unescape_c(c_text);
	if (text == NULL) {
	}

	// "cw":null は CW なし、"cw":"" は前置きなしの [CW]、で意味が違う。
	string *cw;
	int icw = json_obj_find(js, irenote, "cw");
	if (icw >= 0 && json_is_str(js, icw)) {
		const char *c_cw = json_get_cstr(js, icw);
		cw = string_unescape_c(c_cw);
	} else {
		cw = NULL;
	}

	ustring *textline = ustring_alloc(256);
	if (cw) {
		ustring_append_utf8(textline, string_get(cw));
		ustring_append_ascii(textline, " [CW]");
		if (opt_show_cw) {
			ustring_append_unichar(textline, '\n');
			ustring_append_utf8(textline, string_get(text));
		}
	} else {
		ustring_append_utf8(textline, string_get(text));
	}
	string_free(text);

	misskey_show_icon(js, iuser, userid);

	iprint(headline);
	printf("\n");
	iprint(textline);
	printf("\n");
	ustring_free(headline);
	ustring_free(textline);

	// これらは本文付随なので CW 以降を表示する時だけ表示する。
	if (cw == NULL || opt_show_cw) {
		// picture
		image_count = 0;
		image_next_cols = 0;
		image_max_rows = 0;
		if (ifiles >= 0) {
			JSON_ARRAY_FOR(ifile, js, ifiles) {
				print_indent(indent_depth + 1);
				// i_ がループ変数なのを知っている
				misskey_show_photo(js, ifile, i_);
				printf("\r");
			}
		}
	}

	// 引用部分

	// 時刻と、あればこのノートの既 RN 数、リアクション数。
	string *time = misskey_format_time(js, irenote);
	string *rnmsg = misskey_format_renote_count(js, irenote);
	string *reactmsg = misskey_format_reaction_count(js, irenote);

	ustring *footline = ustring_alloc(64);
	ustring_append_ascii_color(footline, string_get(time), COLOR_TIME);
	ustring_append_ascii_color(footline, string_get(rnmsg), COLOR_RENOTE);
	ustring_append_ascii_color(footline, string_get(reactmsg), COLOR_REACTION);
	string_free(time);
	string_free(rnmsg);
	string_free(reactmsg);

	iprint(footline);
	printf("\n");
	ustring_free(footline);

	// リノート元
	if (has_renote) {
		ustring *rnline = ustring_alloc(64);
		string *rnowner = misskey_format_renote_owner(js, inote);
		ustring_append_utf8_color(rnline, string_get(rnowner), COLOR_RENOTE);
		iprint(rnline);
		printf("\n");
		string_free(rnowner);
		ustring_free(rnline);
	}

	string_free(cw);
	string_free(userid);
	return true;
}

// アナウンス文を処理する。構造が全然違う。
static bool
misskey_show_announcement(const json *js, int inote)
{
	printf("%s not yet\n", __func__);
	abort();
	return true;
}

// アイコン表示。
static void
misskey_show_icon(const json *js, int iuser, const string *userid)
{
	const diag *diag = diag_image;

	if (diag_get_level(diag) == 0) {
		// 改行x3 + カーソル上移動x3 を行ってあらかじめスクロールを
		// 発生させ、アイコン表示時にスクロールしないようにしてから
		// カーソル位置を保存する
		// (スクロールするとカーソル位置復元時に位置が合わない)
		printf("\n\n\n" CSI "3A" ESC "7");

		// インデント。
		if (indent_depth > 0) {
			print_indent(indent_depth);
		}
	}

	bool shown = false;
	if (__predict_true(opt_show_image)) {
		char filename[PATH_MAX];
		const char *avatar_url = json_obj_find_cstr(js, iuser, "avatarUrl");
		if (avatar_url && userid) {
			// URL の FNV1 ハッシュをキャッシュのキーにする。
			// Misskey の画像 URL は長いのと URL がネストした構造を
			// しているので単純に一部を切り出して使う方法は無理。
			snprintf(filename, sizeof(filename), "icon-%s-%u-%s-%08x",
				colorname, fontheight,
				string_get(userid), hash_fnv1a(avatar_url));
			shown = show_image(filename, avatar_url, iconsize, iconsize, -1);
		}

		if (shown == false) {
			const char *avatar_blurhash =
				json_obj_find_cstr(js, iuser, "avatarBlurhash");
			if (avatar_blurhash) {
				char url[256];
				snprintf(filename, sizeof(filename), "icon-%s-%u-%s-%08x",
					colorname, fontheight,
					string_get(userid), hash_fnv1a(avatar_blurhash));
				snprintf(url, sizeof(url), "blurhash://%s", avatar_blurhash);
				shown = show_image(filename, url, iconsize, iconsize, -1);
			}
		}
	}

	if (__predict_true(shown)) {
		if (diag_get_level(diag) == 0) {
			printf(
				// アイコン表示後、カーソル位置を復帰。
				"\r"
				// カーソル位置保存/復元に対応していない端末でも動作するように
				// カーソル位置復元前にカーソル上移動x3を行う。
				CSI "3A" ESC "8"
			);
		}
	} else {
		// アイコンを表示してない場合はここで代替アイコンを表示。
		printf(" *\r");
	}
}

// "files" : [ file1, file2, ... ]
// file は {
//   "blurhash" : "...",
//   "isSensitive" : bool,
//   "name" : string,
//   "properties" : {
//     "width" : int,
//     "height" : int,
//   },
//   "size" : int,
//   "thumbnailUrl" : "...",
//   "type" : "image/jpeg",
//   "url" : "...",
// }
static bool
misskey_show_photo(const json *js, int ifile, int index)
{
	char img_file[PATH_MAX];
	char urlbuf[256];
	const char *img_url;
	uint width = 0;
	uint height = 0;

	bool isSensitive = json_obj_find_bool(js, ifile, "isSensitive");
	if (isSensitive && opt_nsfw != NSFW_SHOW) {
		const char *blurhash = json_obj_find_cstr(js, ifile, "blurhash");
		if (blurhash[0] == '\0' || opt_nsfw == NSFW_ALT) {
			// 画像でないなど Blurhash がない、あるいは --nsfw=alt なら、
			// ファイルタイプだけでも表示しておくか。
			misskey_print_filetype(js, ifile, " [NSFW]");
			return false;
		}
		int iproperties = json_obj_find_obj(js, ifile, "properties");
		if (iproperties >= 0) {
			width  = json_obj_find_int(js, iproperties, "width");
			height = json_obj_find_int(js, iproperties, "height");

			// 原寸のアスペクト比を維持したまま長辺が imagesize になる
			// ようにする。
			// image_reduct() には入力画像サイズとしてこのサイズを、
			// 出力画像サイズも同じサイズを指定することで等倍で動作させる。
			if (width > height) {
				height = height * imagesize / width;
				width = imagesize;
			} else {
				width = width * imagesize / height;
				height = imagesize;
			}
		}
		if (width < 1) {
			width = imagesize;
		}
		if (height < 1) {
			height = imagesize;
		}
		snprintf(urlbuf, sizeof(urlbuf), "blurhash://%s", blurhash);
		img_url = urlbuf;
	} else {
		// 元画像を表示。thumbnailUrl を使う。
		img_url = json_obj_find_cstr(js, ifile, "thumbnailUrl");
		if (img_url[0] == '\0') {
			// なければ、ファイルタイプだけでも表示しとく?
			//misskeky_print_filetype(js, ifile, "");
			return false;
		}
		width  = imagesize;
		height = imagesize;
	}
	make_cache_filename(img_file, sizeof(img_file), img_url);
	return show_image(img_file, img_url, width, height, index);
}

// 改行してファイルタイプだけを出力する。
static void
misskey_print_filetype(const json *js, int ifile, const char *msg)
{
	image_count = 0;
	image_max_rows = 0;
	image_next_cols = 0;

	const char *type = json_obj_find_cstr(js, ifile, "type");
	printf("\r");
	print_indent(indent_depth + 1);
	printf("(%s)%s\n", type, msg);
}

// 画像 URL からキャッシュファイル名を作成して返す。
// "file-<color>-<fontheight>-<url>"(.sixel)
static void
make_cache_filename(char *filename, uint bufsize, const char *url)
{
	snprintf(filename, bufsize, "file-%s-%u-%s", colorname, fontheight, url);

	// 面倒な文字を置換しておく。
	for (char *p = filename; *p; p++) {
		if (strchr(":/()? ", *p)) {
			*p = '_';
		}
	}
}

// src 中の "\\n" などのエスケープされた文字を "\n" に戻す。
static string *
string_unescape_c(const char *src)
{
	// 最長で元文字列と同じ長さのはず?
	string *dst = string_alloc(strlen(src) + 1);
	if (dst == NULL) {
		return NULL;
	}

	char c;
	bool escape = false;
	for (int i = 0; (c = src[i]) != '\0'; i++) {
		if (escape == false) {
			if (c == '\\') {
				escape = true;
			} else {
				string_append_char(dst, c);
			}
		} else {
			switch (c) {
			 case 'n':
				string_append_char(dst, '\n');
				break;
			 case 'r':
				string_append_char(dst, '\r');
				break;
			 case 't':
				string_append_char(dst, '\t');
				break;
			 case '\\':
				string_append_char(dst, '\\');
				break;
			 case '\"':
				string_append_char(dst, '"');
				break;
			 default:
				string_append_char(dst, '\\');
				string_append_char(dst, c);
				break;
			}
			escape = false;
		}
	}

	return dst;
}

// note オブジェクトから表示用時刻文字列を取得。
static string *
misskey_format_time(const json *js, int inote)
{
	const char *createdat;
	int icreatedAt = json_obj_find(js, inote, "createdAt");
	if (__predict_true(icreatedAt >= 0 && json_is_str(js, icreatedAt))) {
		createdat = json_get_cstr(js, icreatedAt);
		time_t unixtime = decode_isotime(createdat);
		return format_time(unixtime);
	} else {
		return string_init();
	}
}

// リノート数を表示用に整形して返す。
static string *
misskey_format_renote_count(const json *js, int inote)
{
	string *s;
	uint rncnt = 0;

	int irenoteCount = json_obj_find(js, inote, "renoteCount");
	if (irenoteCount >= 0) {
		rncnt = json_get_int(js, irenoteCount);
	}

	if (rncnt > 0) {
		char buf[32];
		snprintf(buf, sizeof(buf), " %uRN", rncnt);
		s = string_from_cstr(buf);
	} else {
		s = string_init();
	}
	return s;
}

// リアクション数を表示用に整形して返す。
static string *
misskey_format_reaction_count(const json *js, int inote)
{
	string *s;
	uint cnt = 0;

	int ireactions = json_obj_find(js, inote, "reactions");
	if (ireactions >= 0) {
		// reactions: { "name1":cnt1, "name2":cnt2, ... }
		// の cnt だけをカウントする。
		JSON_OBJ_FOR(ikey, js, ireactions) {
			int ival = ikey + 1;
			cnt += json_get_int(js, ival);
		}
	}

	if (cnt > 0) {
		char buf[32];
		snprintf(buf, sizeof(buf), " %uReact", cnt);
		s = string_from_cstr(buf);
	} else {
		s = string_init();
	}
	return s;
}

// リノート元通知を表示用に整形して返す。
static string *
misskey_format_renote_owner(const json *js, int inote)
{
	string *s = string_init();
	string *rn_time = misskey_format_time(js, inote);
	string *rn_userid = string_alloc(64);
	const char *rn_instance = NULL;
	const char *rn_name = misskey_get_user(js, inote, rn_userid, &rn_instance);

	string_append_cstr(s, string_get(rn_time));
	string_append_char(s, ' ');
	string_append_cstr(s, rn_name);
	string_append_char(s, ' ');
	string_append_cstr(s, string_get(rn_userid));
	if (rn_instance) {
		string_append_char(s, ' ');
		string_append_cstr(s, rn_instance);
	}
	string_append_cstr(s, " renoted");

	string_free(rn_time);
	string_free(rn_userid);
	return s;
}

// ノートのユーザ情報を返す。
// 戻り値でユーザ名を返す。
// userid にアカウント名を入れて(追加して)返す。
// *instancep に、あればインスタンス名を格納する。
static const char *
misskey_get_user(const json *js, int inote, string *userid,
	const char **instancep)
{
	const char *name = NULL;
	const char *instance = NULL;

	int iuser = json_obj_find_obj(js, inote, "user");
	if (iuser >= 0) {
		const char *c_name     = json_obj_find_cstr(js, iuser, "name");
		const char *c_username = json_obj_find_cstr(js, iuser, "username");
		const char *c_host     = json_obj_find_cstr(js, iuser, "host");

		// ユーザ名 は name だが、空なら username を使う仕様のようだ。
		if (c_name && c_name[0] != '\0') {
			name = c_name;
		} else {
			name = c_username;
		}

		// @アカウント名 [ @外部ホスト名 ]
		string_append_char(userid, '@');
		string_append_cstr(userid, c_username);
		if (c_host) {
			string_append_char(userid, '@');
			string_append_cstr(userid, c_host);
		}

		// インスタンス名
		instance = json_obj_find_cstr(js, iuser, "instance");
	}

	if (instancep) {
		*instancep = instance;
	}
	return name;
}
