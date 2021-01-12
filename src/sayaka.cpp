/*
 * sayaka - twitter client
 */
/*
 * Copyright (C) 2014-2021 Tetsuya Isaki
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

#include "acl.h"
#include "sayaka.h"
#include "FileInputStream.h"
#include "StringUtil.h"
#include "UString.h"
#include "main.h"
#include "subr.h"
#include "term.h"
#include <memory>
#include <cstdio>
#include <cstring>
#include <err.h>
#include <unicode/uchar.h>

#if !defined(PATH_SEPARATOR)
#define PATH_SEPARATOR "/"
#endif

class MediaInfo
{
 public:
	MediaInfo()
	{
	}
	MediaInfo(const std::string& target_url_, const std::string& display_url_)
	{
		target_url = target_url_;
		display_url = display_url_;
	}

	std::string target_url  {};
	std::string display_url {};
};

enum Color {
	Username,
	UserId,
	Time,
	Source,

	Retweet,
	Favorite,
	Url,
	Tag,
	Verified,
	Protected,
	NG,
	Max,
};

static void progress(const char *msg);
static void get_access_token();
static bool showobject(const std::string& line);
static bool showstatus(const Json *status, bool is_quoted);
static std::string format_rt_owner(const Json& s);
static std::string format_rt_cnt(const Json& s);
static std::string format_fav_cnt(const Json& s);
static void print_(const std::string& text);
static std::string str_join(const std::string& sep,
	const std::string& s1, const std::string& s2);
static std::string coloring(const std::string& text, Color col);
static UString coloring(const UString& utext, Color col);
class TextTag;
std::string formatmsg(const Json& s, std::vector<MediaInfo> *mediainfo);
static void SetTag(std::vector<TextTag>& tags, const Json& list, Color color);
static void show_icon(const Json& user);
static bool show_photo(const std::string& img_url, int resize_width, int index);
static bool show_image(const std::string& img_file, const std::string& img_url,
	int resize_width, int index);
static FILE *fetch_image(const std::string& cache_filename,
	const std::string& img_url, int resize_width);
static void get_credentials();
static StringDictionary get_paged_list(const std::string& api,
	const char *funcname);
static void record(const Json& obj);
static void invalidate_cache();
static std::string errors2string(const Json& json);

#if defined(SELFTEST)
extern void test_showstatus_acl();
#endif

// 色定数
static const std::string BOLD		= "1";
static const std::string UNDERSCORE	= "4";
static const std::string STRIKE		= "9";
static const std::string BLACK		= "30";
static const std::string RED		= "31";
static const std::string GREEN		= "32";
static const std::string BROWN		= "33";
static const std::string BLUE		= "34";
static const std::string MAGENTA	= "35";
static const std::string CYAN		= "36";
static const std::string WHITE		= "37";
static const std::string GRAY		= "90";
static const std::string YELLOW		= "93";

int  address_family;			// AF_INET*
bool opt_noimage;				// 画像を表示しないなら true
int  color_mode;				// 色数もしくはカラーモード
Diag diag;						// デバッグ (無分類)
Diag diagHttp;					// デバッグ (HTTP コネクション)
Diag diagImage;					// デバッグ (画像周り)
Diag diagShow;					// デバッグ (メッセージ表示判定)
int  opt_debug_sixel;			// デバッグレベル (SIXEL変換周り)
bool opt_debug;					// デバッグオプション (後方互換用)
int  screen_cols;				// 画面の桁数
int  opt_fontwidth;				// オプション指定のフォント幅
int  opt_fontheight;			// オプション指定のフォント高さ
int  fontwidth;					// フォントの幅(ドット数)
int  fontheight;				// フォントの高さ(ドット数)
int  iconsize;					// アイコンの大きさ(正方形、ドット数)
int  imagesize;					// 画像の大きさ(どこ?)
int  indent_cols;				// インデント1階層分の桁数
int  indent_depth;				// インデント深さ
int  max_image_count;			// この列に表示する画像の最大数
int  image_count;				// この列に表示している画像の数
int  image_next_cols;			// この列で次に表示する画像の位置(桁数)
int  image_max_rows;			// この列で最大の画像の高さ(行数)
bool bg_white;					// 明るい背景用に暗い文字色を使う場合は true
std::string iconv_tocode;		// 出力文字コード
std::array<std::string, Color::Max> color2esc;	// 色エスケープ文字列
Twitter tw;
StringDictionary followlist;	// フォロー氏リスト
StringDictionary blocklist;		// ブロック氏リスト
StringDictionary mutelist;		// ミュート氏リスト
StringDictionary nortlist;		// RT非表示氏リスト
bool opt_norest;				// REST API を発行しない
bool opt_evs;					// EVS を使用する
bool opt_show_ng;				// NG ツイートを隠さない
std::string opt_ngword;			// NG ワード (追加削除コマンド用)
std::string opt_ngword_user;	// NG 対象ユーザ (追加コマンド用)
std::string record_file;		// 記録用ファイルパス
std::string opt_filter;			// フィルタキーワード
std::string last_id;			// 直前に表示したツイート
int  last_id_count;				// 連続回数
int  last_id_max;				// 連続回数の上限
bool in_sixel;					// SIXEL 出力中なら true
std::string opt_ciphers;		// 暗号スイート
bool opt_full_url;				// URL を省略表示しない
bool opt_progress;				// 起動時の途中経過表示
NGWord ngword;					// NG ワード
bool opt_ormode;				// SIXEL ORmode で出力するなら true
bool opt_output_palette;		// SIXEL にパレット情報を出力するなら true
int  opt_timeout_image;			// 画像取得の(接続)タイムアウト [msec]
bool opt_pseudo_home;			// 疑似ホームタイムライン
std::string myid;				// 自身の user id
bool opt_nocolor;				// テキストに(色)属性を一切付けない
int  opt_record_mode;			// 0:保存しない 1:表示のみ 2:全部保存
std::string basedir;
std::string cachedir;
std::string tokenfile;
std::string colormapdir;

// 投稿する
void
cmd_tweet()
{
	// 標準入力から受け取る。UTF-8 前提。
	// ツイートは半角240字、全角140字換算で、全角はたぶんだいたい3バイト
	// なので、420 バイト程度が上限のはず?
	std::array<char, 1024> buf;
	int len = 0;
	while (len < buf.size() - 1) {
		if (fgets(buf.data() + len, buf.size() - len - 1, stdin) == NULL)
			break;
		len = strlen(buf.data());
	}

	std::string text(buf.data());
	text = Chomp(text);

	// アクセストークンを取得
	CreateTwitter();

	// 投稿するパラメータを用意
	StringDictionary options;
	options.AddOrUpdate("status", text);
	options.AddOrUpdate("trim_user", "1");

	// 投稿
	auto json = tw.API2Json("POST", Twitter::APIRoot, "statuses/update",
		options);
	if (json.is_null()) {
		errx(1, "statuses/update API2Json failed");
	}
	if (json.contains("errors")) {
		errx(1, "statuses/update failed%s", errors2string(json).c_str());
	}
	printf("Posted.\n");
}

// 起動経過を表示 (遅マシン用)
static void
progress(const char *msg)
{
	if (opt_debug || opt_progress) {
		fputs(msg, stdout);
		fflush(stdout);
	}
}

// フィルタストリーム
void
cmd_stream()
{
	InputStream *stream = NULL;

	// 古いキャッシュを削除
	progress("Deleting expired cache files...");
	invalidate_cache();
	progress("done\n");

	// アクセストークンを取得
	CreateTwitter();

	if (opt_norest == false) {
		if (opt_pseudo_home) {
			// 疑似タイムライン用に自分の ID 取得
			progress("Getting credentials...");
			get_credentials();
			progress("done\n");

			// 疑似タイムライン用にフォローユーザ取得
			progress("Getting follow list...");
			get_follow_list();
			progress("done\n");

			// ストリームの場合だけフォローの中に自身を入れておく。
			// 表示するかどうかの判定はほぼフォローと同じなので。
			followlist.AddOrUpdate(myid, myid);
		}

		// ブロックユーザ取得
		progress("Getting block users list...");
		get_block_list();
		progress("done\n");

		// ミュートユーザ取得
		progress("Getting mute users list...");
		get_mute_list();
		progress("done\n");

		// RT非表示ユーザ取得
		progress("Getting nort users list...");
		get_nort_list();
		progress("done\n");
	}

	printf("Ready..");
	fflush(stdout);

	// ストリーミング開始
	diag.Debug("PostAPI call");
	{
		StringDictionary dict;
		if (opt_pseudo_home) {
			// 疑似ホームタイムライン
			std::string liststr;
			for (const auto& kv : followlist) {
				const auto& key = kv.first;
				liststr += key + ",";
			}
			// followlist には自分自身を加えているので必ず1人以上いるので、
			// 最後の ',' だけ取り除けば join(",") 相当になる。
			liststr.pop_back();
			dict.AddOrUpdate("follow", liststr);
		} else {
			// キーワード検索
			dict.AddOrUpdate("track", opt_filter);
		}
		stream = tw.PostAPI(Twitter::PublicAPIRoot, "statuses/filter", dict);
		if (stream == NULL) {
			errx(1, "statuses/filter failed");
		}
	}
	printf("Connected.\n");

	for (;;) {
		std::string line;
		if (stream->ReadLine(&line) == false) {
			errx(1, "statuses/filter: ReadLine failed");
		}
		if (line.empty())
			break;

		if (showobject(line) == false) {
			break;
		}
	}
}

// 再生モード
void
cmd_play()
{
	FileInputStream stdinstream(stdin, false);
	std::string line;

	while (stdinstream.ReadLine(&line)) {
		if (line.empty())
			break;
		if (showobject(line) == false) {
			break;
		}
	}
}

// Twitter オブジェクトを初期化
void
CreateTwitter()
{
	static bool initialized = false;

	// XXX 元はここで tw を必要なら new していたからこうなっている
	if (!initialized) {
		initialized = true;

		tw.SetDiag(diagHttp);
		get_access_token();

		if (!opt_ciphers.empty()) {
			tw.SetCiphers(opt_ciphers);
		}
	}
}

// アクセストークンを取得する
static void
get_access_token()
{
	bool r;

	// ファイルからトークンを取得
	r = tw.AccessToken.LoadFromFile(tokenfile);
	if (r == false) {
		// なければトークンを取得してファイルに保存
		tw.GetAccessToken();
		if (tw.AccessToken.Token.empty()) {
			errx(1, "GIVE UP");
		}

		r = tw.AccessToken.SaveToFile(tokenfile);
		if (r == false) {
			errx(1, "Token save failed");
		}
	}
}

// ストリームから受け取った何かの1行 line を処理する共通部分。
// line はイベントかメッセージの JSON 文字列1行分。
// たぶんイベントは userstream 用なので、もう来ないはず。
static bool
showobject(const std::string& line)
{
	// 空行がちょくちょく送られてくるようだ
	if (line.empty()) {
		diag.Debug("empty line");
		return true;
	}

	// line (文字列) から obj (JSON) に。
	Json obj = Json::parse(line);
	if (obj.is_null()) {
		warnx("%s: Json parser failed.\n"
			"There may be something wrong with twitter.", __func__);
		return false;
	}

	// 全ツイートを録画
	if (opt_record_mode == 2) {
		record(obj);
	}

	if (obj.contains("text")) {
		// 通常のツイート
		bool crlf = showstatus(&obj, false);
		if (crlf) {
			printf("\n");
		}
	} else {
		// それ以外はとりあえず無視
	}
	return true;
}

// 1ツイートを表示。
// true なら戻ったところで1行空ける改行。ツイートとツイートの間は1行
// 空けるがここで判定の結果何も表示しなかったら空けないなど。
static bool
showstatus(const Json *status, bool is_quoted)
{
	// このツイートを表示するかどうかの判定。
	// これは、このツイートがリツイートを持っているかどうかも含めた判定を
	// 行うのでリツイート分離前に行う。
	if (acl(*status, is_quoted) == false) {
		return false;
	}

	// 表示範囲だけ録画ならここで保存。
	// 実際にはここから NG ワードと鍵垢の非表示判定があるけど
	// もういいだろう。
	if (opt_record_mode == 1 && is_quoted == false) {
		record(*status);
	}

	// NGワード
	NGStatus ngstat;
	bool match = ngword.Match(&ngstat, *status);
	if (match) {
		// マッチしたらここで表示
		diagShow.Print(1, "showstatus: ng -> false");
		if (opt_show_ng) {
			auto userid = coloring(formatid(ngstat.screen_name), Color::NG);
			auto name = coloring(formatname(ngstat.name), Color::NG);
			auto time = coloring(ngstat.time, Color::NG);
			auto msg = coloring("NG:" + ngstat.ngword, Color::NG);

			print_(name + " " + userid + "\n"
			     + time + " " + msg + "\n");
			return true;
		}
		return false;
	}

	// RT なら、RT 元を status に、RT先を s に。
	const Json *s = status;
	bool has_retweet = false;
	if ((*status).contains("retweeted_status")) {
		s = &(*status)["retweeted_status"];
		has_retweet = true;
	}

	// 簡略表示の判定。QT 側では行わない
	if (is_quoted == false) {
		if (has_retweet) {
			auto rt_id = (*s).value("id_str", "");

			// 直前のツイートが (フォロー氏による) 元ツイートで
			// 続けてこれがそれを RT したツイートなら簡略表示だが、
			// この二者は別なので1行空けたまま表示。
			if (rt_id == last_id) {
				if (last_id_count++ < last_id_max) {
					auto rtmsg = format_rt_owner(*status);
					auto rtcnt = format_rt_cnt(*s);
					auto favcnt = format_fav_cnt(*s);
					print_(rtmsg + rtcnt + favcnt + "\n");
					// これ以降のリツイートは連続とみなす
					last_id += "_RT";
					return true;
				}
			}
			// 直前のツイートがすでに誰か氏によるリツイートで
			// 続けてこれが同じツイートを RT したものなら簡略表示だが、
			// これはどちらも他者をリツイートなので区別しなくていい。
			if (rt_id + "_RT" == last_id) {
				if (last_id_count++ < last_id_max) {
					auto rtmsg = format_rt_owner(*status);
					auto rtcnt = format_rt_cnt(*s);
					auto favcnt = format_fav_cnt(*s);
					printf(CSI "1A");
					print_(rtmsg + rtcnt + favcnt + "\n");
					return true;
				}
			}
		}

		// 直前のツイートのふぁぼなら簡略表示
		if (0) {
			// userstream でしか来ない
		}

		// 表示確定
		// 次回の簡略表示のために覚えておく。その際今回表示するのが
		// 元ツイートかリツイートかで次回の連続表示が変わる。
		if (has_retweet) {
			last_id = (*s).value("id_str", "") + "_RT";
		} else {
			last_id = (*status).value("id_str", "");
		}
		last_id_count = 0;
	}

	const Json& s_user = (*s)["user"];
	auto userid = coloring(formatid(s_user.value("screen_name", "")),
		Color::UserId);
	auto name = coloring(formatname(s_user.value("name", "")), Color::Username);
	auto src = coloring(unescape(strip_tags((*s).value("source", ""))) + "から",
		Color::Source);
	auto time = coloring(formattime(*s), Color::Time);
	auto verified = s_user.value("verified", false)
		? coloring(" ●", Color::Verified)
		: "";

	std::vector<MediaInfo> mediainfo;
	auto msg = formatmsg(*s, &mediainfo);

	show_icon(s_user);
	print_(name + " " + userid + verified);
	printf("\n");
	print_(msg);
	printf("\n");

	// picture
	image_count = 0;
	image_next_cols = 0;
	image_max_rows = 0;
	for (int i = 0; i < mediainfo.size(); i++) {
		const auto& m = mediainfo[i];

		auto indent = (indent_depth + 1) * indent_cols;
		printf(CSI "%dC", indent);
		show_photo(m.target_url, imagesize, i);
		printf("\r");
	}

	// コメント付きRT の引用部分
	if ((*s).contains("quoted_status")) {
		// この中はインデントを一つ下げる
		printf("\n");
		indent_depth++;
		showstatus(&(*s)["quoted_status"], true);
		indent_depth--;
		// 引用表示後のここは改行しない
	}

	// このステータスの既 RT、既ふぁぼ数
	auto rtmsg = format_rt_cnt(*s);
	auto favmsg = format_fav_cnt(*s);
	print_(time + " " + src + rtmsg + favmsg);
	printf("\n");

	// リツイート元
	if (has_retweet) {
		print_(format_rt_owner(*status));
		printf("\n");
	}

	// ふぁぼはもう飛んでこない

	return true;
}

// リツイート元通知を整形して返す
static std::string
format_rt_owner(const Json& status)
{
	const Json& user = status["user"];
	auto rt_time   = formattime(status);
	auto rt_userid = formatid(user.value("screen_name", ""));
	auto rt_name   = formatname(user.value("name", ""));
	auto str = coloring(string_format("%s %s %s がリツイート",
		rt_time.c_str(), rt_name.c_str(), rt_userid.c_str()), Color::Retweet);
	return str;
}

// リツイート数を整形して返す
static std::string
format_rt_cnt(const Json& s)
{
	auto rtcnt = s.value("retweet_count", 0);
	return (rtcnt > 0)
		? coloring(string_format(" %dRT", rtcnt), Color::Retweet)
		: "";
}

// ふぁぼ数を整形して返す
static std::string
format_fav_cnt(const Json& s)
{
	auto favcnt = s.value("favorite_count", 0);
	return (favcnt > 0)
		? coloring(string_format(" %dFav", favcnt), Color::Favorite)
		: "";
}

// インデントを付けて文字列を表示する
static void
print_(const std::string& text)
{
	// まず Unicode 文字単位でいろいろフィルターかける。
	UString src = StringToUString(text);
	UString textarray;
	for (const auto uni : src) {
		// Private Use Area (外字) をコードポイント形式(?)にする
		if (__predict_false((  0xe000 <= uni && uni <=   0xf8ff))	// BMP
		 || __predict_false(( 0xf0000 <= uni && uni <=  0xffffd))	// 第15面
		 || __predict_false((0x100000 <= uni && uni <= 0x10fffd))) 	// 第16面
		{
			auto tmp = string_format("<U+%X>", uni);
			textarray.Append(tmp);
			continue;
		}

		// ここで EVS 文字を抜く。
		// 絵文字セレクタらしいけど、うちの mlterm + sayaka14 フォントだと
		// U+FE0E とかの文字が前の文字に上書き出力されてぐちゃぐちゃに
		// なってしまうので、mlterm が対応するまではこっちでパッチ対応。
		if (__predict_false(uni == 0xfe0e || uni == 0xfe0f)) {
			if (opt_evs) {
				textarray += uni;
			}
			continue;
		}

		// JIS/EUC-JP(/Shift-JIS) に変換する場合のマッピング
		if (iconv_tocode != "") {
			// 全角チルダ(U+FF5E) -> 波ダッシュ(U+301C)
			if (uni == 0xff5e) {
				textarray.Append(0x301c);
				continue;
			}

			// 全角ハイフンマイナス(U+FF0D) -> マイナス記号(U+2212)
			if (uni == 0xff0d) {
				textarray.Append(0x2212);
				continue;
			}

			// BULLET (U+2022) -> 中黒(U+30FB)
			if (uni == 0x2022) {
				textarray.Append(0x30fb);
				continue;
			}
		}

		// NetBSD/x68k なら半角カナは表示できる。
		// XXX 正確には JIS という訳ではないのだがとりあえず
		if (__predict_false(iconv_tocode == "iso-2022-jp")) {
			if (0xff61 <= uni && uni < 0xffa0) {
				// 半角カナはそのまま、あるいは JIS に変換、SI/SO を
				// 使うなどしても、この後の JIS-> UTF-8 変換を安全に
				// 通せないので、ここで半角カナを文字コードではない
				// 自前エスケープシーケンスに置き換えておいて、
				// 変換後にもう一度デコードして復元することにする。
				// ESC [ .. n は端末問い合わせの CSI シーケンスなので
				// 入力には来ないはずだし、仮にそのまま出力されたと
				// してもあまりまずいことにはならないんじゃないかな。
				// 半角カナを GL に置いた時の10進数2桁でエンコード。
				auto tmp = string_format(ESC "[%dn", uni - 0xff60 + 0x20);
				textarray.Append(tmp);
				continue;
			}
		}

		textarray += uni;
	}

	// 文字コードを変換する場合は、
	// ここで一度変換してみて、それを Unicode に戻す。
	// この後の改行処理で、Unicode では半角幅だが変換すると全角ゲタ(〓)
	// になるような文字の文字幅が合わなくなるのを避けるため。
#if 0
	if (__predict_false(!iconv_tocode.empty()) {
		sb = UStringToString(textarray);

		// 変換してみる (変換できない文字をフォールバックさせる)
	}
#endif

	// ここからインデント
	UString sb;

	// インデント階層
	auto left = indent_cols * (indent_depth + 1);
	auto indent_str = string_format(CSI "%dC", left);
	auto indent = StringToUString(indent_str);
	sb.Append(indent);

	if (__predict_false(screen_cols == 0)) {
		// 桁数が分からない場合は何もしない
		sb.Append(textarray);
	} else {
		// 1文字ずつ文字幅を数えながら出力用に整形していく
		int inescape = 0;
		auto x = left;
		for (auto uni : textarray) {
			if (__predict_false(inescape > 0)) {
				// 1: ESC直後
				// 2: ESC [
				// 3: ESC (
				sb.Append(uni);
				switch (inescape) {
				 case 1:
					// ESC 直後の文字で二手に分かれる
					if (uni == '[') {
						inescape = 2;
					} else {
						inescape = 3;	// 手抜き
					}
					break;
				 case 2:
					// ESC [ 以降 'm' まで
					if (uni == 'm') {
						inescape = 0;
					}
					break;
				 case 3:
					// ESC ( の次の1文字だけ
					inescape = 0;
					break;
				}
			} else {
				if (uni == ESCchar) {
					sb += uni;
					inescape = 1;
				} else if (uni == '\n') {
					sb += uni;
					sb += indent;
					x = left;
				} else {
					// Neutral と Ambiguous は安全のため2桁側に振っておく。
					// 2桁で数えておけば端末が1桁しか進まなくても、改行が
					// 想定より早まるだけで、逆よりはマシ。
					auto eaw = (UEastAsianWidth)u_getIntPropertyValue(uni,
						UCHAR_EAST_ASIAN_WIDTH);
					switch (eaw) {
					 case U_EA_NARROW:
					 case U_EA_HALFWIDTH:
						sb += uni;
						x++;
						break;
					 case U_EA_WIDE:
					 case U_EA_FULLWIDTH:
					 case U_EA_NEUTRAL:
					 case U_EA_AMBIGUOUS:
						if (x > screen_cols - 2) {
							sb += '\n';
							sb += indent;
							x = left;
						}
						sb += uni;
						x += 2;
						break;
					}
				}
				if (x > screen_cols - 1) {
					sb += '\n';
					sb += indent;
					x = left;
				}
			}
		}
	}
	std::string outtext = UStringToString(sb);

	// 出力文字コードの変換
	if (__predict_false(!iconv_tocode.empty())) {
	}

	fputs(outtext.c_str(), stdout);
}

void
init_color()
{
	std::string blue;
	std::string green;
	std::string username;
	std::string fav;
	std::string gray;
	std::string verified;

	if (color_mode == 2) {
		// 2色モードなら色は全部無効にする。
		// ユーザ名だけボールドにすると少し目立って分かりやすいか。
		username = BOLD;
	} else {
		// それ以外のケースは色ごとに個別調整。

		// 青は黒背景か白背景かで色合いを変えたほうが読みやすい
		if (bg_white) {
			blue = BLUE;
		} else {
			blue = CYAN;
		}

		// ユーザ名。白地の場合は出来ればもう少し暗めにしたい
		if (bg_white && color_mode > 16) {
			username = "38;5;28";
		} else {
			username = BROWN;
		}

		// リツイートは緑色。出来れば濃い目にしたい
		if (color_mode == ColorFixedX68k) {
			green = "92";
		} else if (color_mode > 16) {
			green = "38;5;28";
		} else {
			green = GREEN;
		}

		// ふぁぼは黄色。白地の場合は出来れば濃い目にしたいが
		// こちらは太字なのでユーザ名ほどオレンジにしなくてもよさげ。
		if (bg_white && color_mode > 16) {
			fav = "38;5;184";
		} else {
			fav = BROWN;
		}

		// x68k 独自16色パッチでは 90 は黒、97 がグレー。
		// mlterm では 90 がグレー、97 は白。
		if (color_mode == ColorFixedX68k) {
			gray = "97";
		} else {
			gray = "90";
		}

		// 認証マークは白背景でも黒背景でもシアンでよさそう
		verified = CYAN;
	}

	color2esc[Color::Username]	= username;
	color2esc[Color::UserId]	= blue;
	color2esc[Color::Time]		= gray;
	color2esc[Color::Source]	= gray;

	color2esc[Color::Retweet]	= str_join(";", BOLD, green);
	color2esc[Color::Favorite]	= str_join(";", BOLD, fav);
	color2esc[Color::Url]		= str_join(";", UNDERSCORE, blue);
	color2esc[Color::Tag]		= blue;
	color2esc[Color::Verified]	= verified;
	color2esc[Color::Protected]	= gray;
	color2esc[Color::NG]		= str_join(";", STRIKE, gray);
}

// 文字列を sep で結合した文字列を返します。
// ただし (glib の) string.join() と異なり、(null と)空文字列の要素は排除
// した後に結合を行います。
// XXX 今の所、引数は2つのケースしかないので手抜き。
// 例)
//   string.join(";", "AA", "") -> "AA;"
//   str_join(";", "AA", "")    -> "AA"
static std::string
str_join(const std::string& sep, const std::string& s1, const std::string& s2)
{
	if (s1 == "" || s2 == "") {
		return s1 + s2;
	} else {
		return s1 + sep + s2;
	}
}

// 文字列 text を属性付けした新しい文字列を返す (std::string 版)
static std::string
coloring(const std::string& text, Color col)
{
	std::string rv;

	if (opt_nocolor) {
		// --nocolor なら一切属性を付けない
		rv = text;
	} else if (__predict_false(color2esc.empty())) {
		// ポカ避け
		rv = string_format("Coloring(%s,%d)", text.c_str(), col);
	} else {
		rv = CSI + color2esc[col] + "m" + text + CSI + "0m";
	}
	return rv;
}

// UString の utext を属性付けした新しい UString を返す。
// (vala では unichar と string がもっと親和性が高かったのでこうなっている)
static UString
coloring(const UString& utext, Color col)
{
	UString rv;

	if (opt_nocolor) {
		// --nocolor なら一切属性を付けない
		rv = utext;
	} else if (__predict_false(color2esc.empty())) {
		// ポカ避け (%d は省略)
		const UString tmp { 'C', 'o', 'l', 'o', 'r', 'i', 'n', 'g', '(' };
		rv += tmp;
		rv += utext;
		rv += ')';
	} else {
		// ( CSI + color2esc[col] + "m" ) + text + ( CSI + "0m" )
		std::string pre = CSI + color2esc[col] + "m";
		std::string post = CSI "0m";
		rv.AppendChars(pre);
		rv.Append(utext);
		rv.AppendChars(post);
	}
	return rv;
}

class TextTag
{
	// 文字列先頭からタグが始まる場合は Start == 0 となるため、
	// 未使用エントリは Start == -1 とする。
 public:
	int Start {};
	int End {};
	Color Type {};
	std::string Text {};

	TextTag()
		: TextTag(-1, -1, (Color)0) { }
	TextTag(int start_, int end_, Color type_)
		: TextTag(start_, end_, type_, "") { }
	TextTag(int start_, int end_, Color type_, const std::string& text_)
	{
		Start = start_;
		End = end_;
		Type = type_;
		Text = text_;
	}

	int Length() const { return End - Start; }

	bool Valid() const { return (Start >= 0); }

	std::string to_string() {
		return string_format("(%d, %d, %d)", Start, End, (int)Type);
	}
};

// 本文を整形して返す
// (そのためにここでハッシュタグ、メンション、URL を展開)
//
// 従来はこうだった(↓)が
//   "text":本文,
//   "entities":{
//     "hashtag":[..]
//     "user_mentions":[..]
//     "urls":[..]
//   },
//   "extended_entities":{
//     "media":[..]
//   }
// extended_tweet 形式ではこれに加えて
//   "extended_tweet":{
//     "full_text":本文,
//     "entities":{
//     "hashtag":[..]
//     "user_mentions":[..]
//     "urls":[..]
//     "media":[..]
//   }
// が追加されている。media の位置に注意。
std::string
formatmsg(const Json& s, std::vector<MediaInfo> *mediainfo)
{
	const Json *extw = NULL;
	const Json *textj = NULL;

	// 本文
	if (s.contains("extended_tweet")) {
		extw = &s["extended_tweet"];
		if ((*extw).contains("full_text")) {
			textj = &(*extw)["full_text"];
		}
	} else {
		if (s.contains("text")) {
			textj = &s["text"];
		}
	}
	if (__predict_false(textj == NULL)) {
		// ないことはないはず
		return "";
	}
	const std::string& text = (*textj).get<std::string>();

	// 1文字ずつ分解して配列に
	UString utext = StringToUString(text);

	// エンティティの位置が新旧で微妙に違うのを吸収
	const Json *entities;
	const Json *media_entities;
	if (extw) {
		if ((*extw).contains("entities")) {
			entities = &(*extw)["entities"];
		}
		media_entities = entities;
	} else {
		if (s.contains("entities")) {
			entities = &s["entities"];
		}
		if (s.contains("extended_entities")) {
			media_entities = &s["extended_entities"];
		}
	}

	// エンティティを調べる
	std::vector<TextTag> tags(utext.size());
	if (entities) {
		// ハッシュタグ情報を展開
		if ((*entities).contains("hashtags")) {
			const Json& hashtags = (*entities)["hashtags"];
			SetTag(tags, hashtags, Color::Tag);
		}

		// ユーザID情報を展開
		if ((*entities).contains("user_mentions")) {
			const Json& mentions = (*entities)["user_mentions"];
			SetTag(tags, mentions, Color::UserId);
		}

		// URL を展開
		if ((*entities).contains("urls")) {
			const Json& urls = (*entities)["urls"];
			if (urls.is_array()) {
				for (const Json& u : urls) {
					if (u.contains("indices")) {
						const Json& indices = u["indices"];
						if (!indices.is_array() || indices.size() != 2) {
							continue;
						}
						int start = indices[0].get<int>();
						int end   = indices[1].get<int>();

						// url          … 本文中の短縮 URL
						// display_url  … 差し替えて表示用の URL
						// expanded_url … 展開後の URL
						const auto& url      = u.value("url", "");
						const auto& disp_url = u.value("display_url", "");
						const auto& expd_url = u.value("expanded_url", "");

						// 本文の短縮 URL を差し替える
						std::string newurl;
						const auto& qid = s.value("quoted_status_id_str", "");
						std::string text2 = Chomp(text);
						if (s.contains("quoted_status")
						 && expd_url.find(qid) != std::string::npos
						 && EndWith(text2, url))
						{
							// この場合は引用 RT の URL なので、表示しなくていい
							newurl = "";
						} else {
							newurl = disp_url;
						}
						// --full-url モードなら短縮 URL ではなく元 URL を使う
						if (opt_full_url
						 && newurl.find("…") != std::string::npos)
						{
							newurl = expd_url;
							string_replace(newurl, "http://", "");
						}

						tags[start] = TextTag(start, end, Color::Url, newurl);

						// 外部画像サービスを解析
						MediaInfo minfo;
#if 0
						if (format_image_url(&minfo, expd_url, disp_url)) {
							(*mediainfo).emplace_back(minfo);
						}
#endif
					}
				}
			}
		}
	}

	// メディア情報を展開
	if (media_entities != NULL && (*media_entities).contains("media")) {
		const Json& media = (*media_entities)["media"];
		for (const Json& m : media) {
			// 本文の短縮 URL を差し替える
			const std::string& disp_url = m.value("display_url", "");
			if (m.contains("indices")) {
				const Json& indices = m["indices"];
				if (indices.is_array() && indices.size() == 2) {
					int start = indices[0].get<int>();
					int end   = indices[1].get<int>();
					tags[start] = TextTag(start, end, Color::Url, disp_url);
				}
			}

			// 画像展開に使う
			//   url         本文中の短縮 URL (twitterから)
			//   display_url 差し替えて表示用の URL (twitterから)
			//   media_url   指定の実ファイル URL (twitterから)
			//   target_url  それを元に実際に使う URL (こちらで付加)
			//   width       幅指定。ピクセルか割合で (こちらで付加)
			const std::string& media_url = m.value("media_url", "");
			std::string target_url = media_url + ":small";
			MediaInfo minfo(target_url, disp_url);
			(*mediainfo).emplace_back(minfo);
		}
	}

	// タグ情報をもとにテキストを整形
	// 表示区間が指定されていたらそれに従う
	// XXX 後ろは添付画像 URL とかなので削るとして
	// XXX 前はメンションがどうなるか分からないのでとりあえず後回し
	auto display_end = utext.size();
	if (extw != NULL && (*extw).contains("display_text_range")) {
		const Json& range = (*extw)["display_text_range"];
		if (range.is_array() && range.size() >= 2) {
			display_end = range[1].get<int>();
		}
	}
	// 文字数を数える必要があるのでコードポイントのまま文字列を作っていく
	UString new_utext;
	for (int i = 0; i < display_end; ) {
		if (__predict_false(tags[i].Valid())) {
			switch (tags[i].Type) {
			 case Color::Tag:
			 case Color::UserId:
			 {
				UString sb;
				for (int j = 0, jend = tags[i].Length(); j < jend; j++) {
					sb.Append(utext[i + j]);
				}
				sb = coloring(sb, tags[i].Type);
				new_utext.Append(sb);
				i += tags[i].Length();
				break;
			 }
			 case Color::Url:
			 {
				std::string sb = coloring(tags[i].Text, tags[i].Type);
				for (int i = 0; i < sb.size(); i++) {
					new_utext.emplace_back(sb[i]);
				}
				i += tags[i].Length();
				break;
			 }
			 default:
				break;
			}
		} else {
			new_utext.emplace_back(utext[i++]);
		}
	}
	// ここで文字列に戻す
	std::string new_text = UStringToString(new_utext);

	// タグの整形が済んでからエスケープと改行を整形
	new_text = unescape(new_text);
	new_text = string_replace(new_text, "\r\n", "\n");
	string_inreplace(new_text, '\r', '\n');

	return new_text;
}

// formatmsg() の下請け。
// list からタグ情報を取り出して tags にセットする。
// ハッシュタグとユーザメンションがまったく同じ構造なので。
//
// "hashtag": [
//   { "indices": [
//	     <start> … 開始位置、1文字目からなら 0
//       <end>   … 終了位置。この1文字前まで
//     ],
//     "...": 他のキーもあるかも知れないがここでは見ない
//   }, ...
// ]
static void
SetTag(std::vector<TextTag>& tags, const Json& list, Color color)
{
	if (list.is_array() == false) {
		return;
	}

	for (const Json& t : list) {
		if (t.contains("indices")) {
			const Json& indices = t["indices"];
			if (indices.is_array() && indices.size() == 2) {
				int start = indices[0].get<int>();
				int end   = indices[1].get<int>();
				tags[start] = TextTag(start, end, color);
			}
		}
	}
}

// 現在行に user のアイコンを表示。
// 呼び出し時点でカーソルは行頭にあるため、必要なインデントを行う。
// アイコン表示後にカーソル位置を表示前の位置に戻す。
static void
show_icon(const Json& user)
{
	// 改行x3 + カーソル上移動x3 を行ってあらかじめスクロールを
	// 発生させ、アイコン表示時にスクロールしないようにしてから
	// カーソル位置を保存する
	// (スクロールするとカーソル位置復元時に位置が合わない)
	printf("\n\n\n" CSI "3A" ESC "7");

	// インデント。
	// CSI."0C" は0文字でなく1文字になってしまうので、必要な時だけ。
	if (indent_depth > 0) {
		int left = indent_cols * indent_depth;
		printf(CSI "%dC", left);
	}

	bool shown = false;
	if (__predict_false(opt_noimage)) {
		// shown = false;
	} else {
		auto screen_name = unescape(user.value("screen_name", ""));
		const auto& image_url = user.contains("profile_image_url_https")
			? user["profile_image_url_https"].get<std::string>()
			: (user.contains("profile_image_url")
				? user["profile_image_url"].get<std::string>()
				: "");

		// URL のファイル名部分をキャッシュのキーにする
		auto p = image_url.rfind('/');
		if (__predict_false(p >= 0)) {
			auto img_file = string_format("icon-%dx%d-%s-%s",
				iconsize, iconsize, screen_name.c_str(),
				image_url.c_str() + p + 1);
			show_image(img_file, image_url, iconsize, -1);
			shown = true;
		}
	}

	if (__predict_true(shown)) {
		// アイコン表示後、カーソル位置を復帰
		printf("\r");
		// カーソル位置保存/復元に対応していない端末でも動作するように
		// カーソル位置復元前にカーソル上移動x3を行う
		printf(CSI "3A" ESC "8");
	} else {
		// アイコンを表示してない場合はここで代替アイコンを表示。
		printf(" *");
		// これだけで復帰できるはず
		printf("\r");
	}
}

// index は画像の番号 (位置決めに使用する)
static bool
show_photo(const std::string& img_url, int resize_width, int index)
{
	auto img_file = img_url;

	for (auto p = 0;
		(p = img_file.find_first_of(":/()? ", p)) != std::string::npos;
		p++)
	{
		img_file[p] = '_';
	}

	return show_image(img_file, img_url, resize_width, index);
}

// 画像をキャッシュして表示
//  img_file はキャッシュディレクトリ内でのファイル名
//  img_url は画像の URL
//  resize_width はリサイズ後の画像の幅。ピクセルで指定。0 を指定すると
//  リサイズせずオリジナルのサイズ。
//  index は -1 ならアイコン、0 以上なら添付写真の何枚目かを表す。
//  どちらも位置決めなどのために使用する。
// 表示できれば真を返す。
static bool
show_image(const std::string& img_file, const std::string& img_url,
	int resize_width, int index)
{
	if (opt_noimage)
		return false;

	std::string img_path = cachedir + PATH_SEPARATOR + img_file;

	diagImage.Debug("show_image: img_url=%s", img_url.c_str());
	diagImage.Debug("show_image: img_path=%s", img_path.c_str());
	auto cache_filename = img_path + ".sixel";
	FILE *cache_file = fopen(cache_filename.c_str(), "r");
	if (cache_file == NULL) {
		// キャッシュファイルがないので、画像を取得
		diagImage.Debug("sixel cache is not found");
		cache_file = fetch_image(cache_filename, img_url, resize_width);
		if (cache_file == NULL) {
			return false;
		}
	}

	// SIXEL の先頭付近から幅と高さを取得
	auto sx_width = 0;
	auto sx_height = 0;
	char buf[4096];
	char *ep;
	auto n = fread(buf, 1, sizeof(buf), cache_file);
	if (n < 32) {
		return false;
	}
	// " <Pan>; <Pad>; <Ph>; <Pv>
	int i;
	// Search "
	for (i = 0; i < n && buf[i] != '\x22'; i++)
		;
	// Skip Pan;
	for (i++; i < n && buf[i] != ';'; i++)
		;
	// Skip Pad
	for (i++; i < n && buf[i] != ';'; i++)
		;
	// Ph
	errno = 0;
	sx_width = strtol(buf + i, &ep, 10);
	if (ep == buf + i || (*ep != ';' && *ep != ' ') || errno == ERANGE) {
		sx_width = 0;
	}
	// Pv
	i = ep - buf;
	i++;
	errno = 0;
	sx_height = strtol(buf + i, &ep, 10);
	if (ep == buf + i || errno == ERANGE) {
		sx_height = 0;
	}

	if (sx_width == 0 || sx_height == 0)
		return false;

	// この画像が占める文字数
	auto image_rows = (sx_height + fontheight - 1) / fontheight;
	auto image_cols = (sx_width + fontwidth - 1) / fontwidth;

	if (index < 0) {
		// アイコンの場合は呼び出し側で実施。
	} else {
		// 添付画像の場合、表示位置などを計算。
		auto indent = (indent_depth + 1) * indent_cols;
		if ((max_image_count > 0 && image_count >= max_image_count) ||
		    (indent + image_next_cols + image_cols >= screen_cols))
		{
			// 指定された枚数を超えるか、画像が入らない場合は折り返す
			printf("\r");
			printf(CSI "%dC", indent);
			image_count = 0;
			image_max_rows = 0;
			image_next_cols = 0;
		} else {
			// 前の画像の横に並べる
			if (image_count > 0) {
				if (image_max_rows > 0) {
					printf(CSI "%dA", image_max_rows);
				}
				if (image_next_cols > 0) {
					printf(CSI "%dC", image_next_cols);
				}
			}
		}
	}

	// 最初の1回はすでに buf に入っているのでまず出力して、
	// 次からは順次読みながら最後まで出力。
	do {
		in_sixel = true;
		fwrite(buf, 1, n, stdout);
		fflush(stdout);
		in_sixel = false;

		n = fread(buf, 1, sizeof(buf), cache_file);
	} while (n > 0);

	if (index < 0) {
		// アイコンの場合は呼び出し側で実施。
	} else {
		// 添付画像の場合
		image_count++;
		image_next_cols += image_cols;

		// カーソル位置は同じ列に表示した画像の中で最長のものの下端に揃える
		if (image_max_rows > image_rows) {
			printf(CSI "%dB", image_max_rows - image_rows);
		} else {
			image_max_rows = image_rows;
		}
	}
	return true;
}

// 画像をダウンロードして SIXEL に変換してキャッシュする。
// 成功すれば、書き出したキャッシュファイルの FILE* (位置は先頭) を返す。
// 失敗すれば NULL を返す。
// cache_filename はキャッシュするファイルのファイル名
// img_url は画像 URL
// resize_width はリサイズすべき幅を指定、0 ならリサイズしない
static
FILE *fetch_image(const std::string& cache_filename, const std::string& img_url,
	int resize_width)
{
	printf("%s not implemented\n", __func__);
	return NULL;
}

// 自分の ID を取得
static void
get_credentials()
{
	CreateTwitter();

	StringDictionary options;
	options["include_entities"] = "false";
	options["include_email"] = "false";
	auto json = tw.API2Json("GET", Twitter::APIRoot,
		"account/verify_credentials", options);
	if (json.is_null()) {
		errx(1, "get_credentials API2Json failed");
	}
	diag.Debug("json=|%s|", json.dump());
	if (json.contains("errors")) {
		errx(1, "get_credentials failed%s", errors2string(json).c_str());
	}

	myid = json.value("id_str", "");
}

// ユーザ一覧を読み込む(共通)。
// フォロー(friends)、ブロックユーザ、ミュートユーザは同じ形式。
// 読み込んだリストを Dictionary 形式で返す。エラーなら終了する。
// funcname はエラー時の表示用。
static StringDictionary
get_paged_list(const std::string& api, const char *funcname)
{
	// ユーザ一覧は一度に全部送られてくるとは限らず、
	// next_cursor{,_str} が 0 なら最終ページ、そうでなければ
	// これを cursor に指定してもう一度リクエストを送る。

	std::string cursor = "-1";
	StringDictionary list;

	do {
		StringDictionary options;
		options["cursor"] = cursor;

		// JSON を取得
		auto json = tw.API2Json("GET", Twitter::APIRoot, api, options);
		if (json.is_null()) {
			errx(1, "%s API2Json failed", funcname);
		}
		diag.Debug("json=|%s|", json.dump());
		if (json.contains("errors")) {
			errx(1, "%s failed: %s", funcname, errors2string(json).c_str());
		}

		auto users = json["ids"];
		for (auto u : users) {
			auto id = u.get<Json::number_integer_t>();
			auto id_str = std::to_string(id);
			list[id_str] = id_str;
		}

		cursor = json.value("next_cursor_str", "");
		diag.Debug("cursor=|%s|", cursor.c_str());
		if (__predict_false(cursor.empty())) {
			cursor = "0";
		}
	} while (cursor != "0");

	return list;
}

// フォローユーザ一覧の読み込み
void
get_follow_list()
{
	followlist = get_paged_list("friends/ids", __func__);
}

// ブロックユーザ一覧の読み込み
void
get_block_list()
{
	blocklist = get_paged_list("blocks/ids", __func__);
}

// ミュートユーザ一覧の読み込み
void
get_mute_list()
{
	mutelist = get_paged_list("mutes/users/ids", __func__);
}

// RT非表示ユーザ一覧の読み込み
void
get_nort_list()
{
	// ミュートユーザ一覧等とは違って、リスト一発で送られてくるっぽい。
	// ただの数値の配列 [1,2,3,4] の形式。
	// なんであっちこっちで仕様が違うんだよ…。

	nortlist.clear();

	// JSON を取得
	auto json = tw.API2Json("GET", Twitter::APIRoot,
		"friendships/no_retweets/ids", {});
	if (json.is_null()) {
		errx(1, "get_nort_list API2Json failed");
	}
	diag.Debug("json=|%s|", json.dump());

	if (!json.is_array()) {
		// どうするかね
		return;
	}

	for (const auto& u : json) {
		auto id = u.get<Json::number_integer_t>();
		auto id_str = std::to_string(id);
		nortlist[id_str] = id_str;
	}
}

// ツイートを保存する
static void
record(const Json& obj)
{
	FILE *fp;

	fp = fopen(record_file.c_str(), "a+");
	if (fp == NULL) {
		return;
	}
	fputs(obj.dump().c_str(), fp);
	fputs("\n", fp);
	fclose(fp);
}

// 古いキャッシュを破棄する
static void
invalidate_cache()
{
	char cmd[1024];

	// アイコンは7日分くらいか
	snprintf(cmd, sizeof(cmd),
		"find %s -name icon-\\* -type f -atime +7 -exec rm {} +",
		cachedir.c_str());
	system(cmd);

	// 写真は24時間分くらいか
	snprintf(cmd, sizeof(cmd),
		"find %s -name http\\* -type f -atime +1 -exec rm {} +",
		cachedir.c_str());
	system(cmd);
}

// API2Json の応答がエラーだった時に表示用文字列に整形して返す。
// if (json.contains("errors")) {
//   auto msg = errors2string(json);
// のように呼び出す。
static std::string
errors2string(const Json& json)
{
	const Json& errors = json["errors"];
	if (errors.is_array()) {
		// エラーが複数返ってきたらどうするかね
		const Json& error = errors[0];
		auto code = error.value("code", 0);
		auto message = error.value("message", "");
		return string_format(": %s(%d)", message.c_str(), code);
	}
	return "";
}