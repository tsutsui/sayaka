<?php
/*
 * sayaka - twitter client
 */
/*
 * Copyright (C) 2011-2016 Tetsuya Isaki
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

	$version = "3.2.0 (2016/02/24)";
	$progname = $_SERVER["argv"][0];

	$bindir = dirname($_SERVER["argv"][0]);
	set_include_path($bindir);

	// 定数定義
	define("ESC", "\x1b");
	define("CSI", ESC."[");
	define("DEFAULT_FONT_WIDTH",	"7");
	define("DEFAULT_FONT_HEIGHT",	"14");
	define("COLOR_USERNAME",	"COLOR_USERNAME");
	define("COLOR_USERID",		"COLOR_USERID");
	define("COLOR_TIME",		"COLOR_TIME");
	define("COLOR_SOURCE",		"COLOR_SOURCE");
	define("COLOR_RETWEET",		"COLOR_RETWEET");
	define("COLOR_FAVORITE",	"COLOR_FAVORITE");
	define("COLOR_URL",			"COLOR_URL");
	define("COLOR_TAG",			"COLOR_TAG");
	define("COLOR_VERIFIED",	"COLOR_VERIFIED");
	define("COLOR_PROTECTED",	"COLOR_PROTECTED");
	define("COLOR_NG",			"COLOR_NG");

	$basedir    = "{$_SERVER['HOME']}/.sayaka/";
	$cachedir   = "{$basedir}/cache";
	$tokenfile  = "{$basedir}/token.json";
	$ngwordfile = "{$basedir}/ngword.json";

	$fontheight = 0;
	$indent_cols = 6;
	$color_mode = 256;

	// まず引数のチェックをする
	$cmd = "";
	$record_file = "";
	$play_file = "";
	$bg_white = false;
	$opt_x68k = false;

	if (isset($_SERVER["SERVER_PROTOCOL"]) && $_SERVER["SERVER_PROTOCOL"] === "HTTP/1.1") {
		header("Connection: Keep-alive");
		$cmd = "stream";
	} else {
		if ($_SERVER["argc"] < 2) {
			usage();
		}
		$longopt = array(
			"stream",
				"color:",
				"eucjp",
				"font:",
				"jis",
				"noimg",
				"protect",
				"record:",
				"white",
			"play:",
			"post:",
			"pipe",
			"debug",
			"mutelist",
			"token:",
			"x68k",
			"help",
			"version",
		);
		$opts = getopt("", $longopt);
		if (isset($opts["stream"])) {
			$cmd = "stream";
		}
		if (isset($opts["color"])) {
			$color_mode = $opts["color"];
		}
		if (isset($opts["eucjp"]) && function_exists("mb_convert_encoding")) {
			$eucjp = true;
		}
		if (isset($opts["font"])) {
			$metric = preg_split("/x/", $opts["font"]);
			if (count($metric) != 2) {
				usage();
			}
			$opt_fontwidth = $metric[0];
			$opt_fontheight = $metric[1];
		}
		if (isset($opts["jis"]) && function_exists("mb_convert_encoding")) {
			$jis = true;
		}
		if (isset($opts["noimg"])) {
			$img2sixel = "none";
		}
		if (isset($opts["protect"])) {
			$protect = true;
		}
		if (isset($opts["record"])) {
			$record_file = $opts["record"];
		}
		if (isset($opts["white"])) {
			$bg_white = true;
		}
		if (isset($opts["play"])) {
			$cmd = "play";
			$play_file = $opts["play"];
		}
		if (isset($opts["post"])) {
			$cmd = "tweet";
			$text = $opts["post"];
		}
		if (isset($opts["pipe"])) {
			$cmd = "tweet";
			// パイプモードなら標準入力から全部読み込む
			$text = "";
			while (($buf = fgets(STDIN))) {
				$text .= $buf;
			}
		}
		if (isset($opts["debug"])) {
			$debug = true;
		}
		if (isset($opts["mutelist"])) {
			$cmd = "mutelist";
		}
		if (isset($opts["token"])) {
			if ($opts["token"][0] == "/") {
				$tokenfile = $opts["token"];
			} else {
				$tokenfile = "{$basedir}/{$opts["token"]}";
			}
		}
		if (isset($opts["x68k"])) {
			$opt_x68k = true;
			// "--font 8x16 --jis --color 16" を指定したのと同じ
			$opt_fontwidth = 8;
			$opt_fontheight = 16;
			$jis = true;
			$color_mode = 16;
		}
		if (isset($opts["version"])) {
			cmd_version();
			exit(0);
		}
		if ($cmd == "") {
			usage();
		}
	}

	// ここからメインルーチン
	require_once "TwistOAuth.php";
	require_once "subr.php";
	setTimeZone();
	declare(ticks = 1);	// for pcntl_signal()

	// アクセストークンを取得
	if (!file_exists($tokenfile)) {
		print "{$tokenfile} not found!\n";
		exit(1);
	}
	$access = json_decode(file_get_contents($tokenfile));

	// OAuth オブジェクト作成
	$tw = new TwistOAuth($consumer_key, $consumer_secret,
		$access->token, $access->secret);

	// コマンド別処理
	switch ($cmd) {
	 case "tweet":
		tweet($text);
		break;
	 case "stream":
		init_stream();
		stream();
		break;
	 case "play":
		init_stream();
		play();
		break;
	 case "mutelist":
		cmd_mutelist();
		break;
	}
	exit(0);
?>
<?php
function tweet($text)
{
	global $tw;

	// エンコードは?
	$encoded_text = $text;

	// 投稿するパラメータを用意
	$options = array();
	$options["status"] = $encoded_text;
	$options["trim_user"] = 1;
	// 投稿
	$json = $tw->post("statuses/update", $options);
	if (isset($json->error)) {
		print "Error has occured: {$json->error}\n";
	} else {
		print "Posted.\n";
	}
}

// ユーザストリームモードのための準備
function init_stream()
{
	global $color_mode;
	global $img2sixel;
	global $cachedir;
	global $tput;
	global $cellsize;
	global $opt_fontheight;
	global $opt_x68k;
	global $bindir;

	// 色の初期化
	init_color();

	// img2sixel
	// --noimg オプションなら img2sixel を使わない
	// そうでなければ探して使う。がなければ使わない
	if ($img2sixel == "none") {
		$img2sixel = "";
	} else {
		$img2sixel = rtrim(`which img2sixel`);
	}
	if ($img2sixel != "") {
		$img2sixel .= " -S";
		if ($opt_x68k) {
			$img2sixel .= " -m colormapx68k16.png";
		} else if ($color_mode == 2) {
			$img2sixel .= " -e --quality=low";
		} else if ($color_mode <= 16) {
			$img2sixel .= " -m colormap{$color_mode}.png";
		}
	}

	// tput
	$tput = rtrim(`which tput`);

	// cellsize
	// --font が未指定の時のみ cellsize を使う
	if ($opt_fontheight != 0) {
		$cellsize = "";
	} else if (file_exists("{$bindir}/cellsize")) {
		$cellsize = "{$bindir}/cellsize";
	} else if (file_exists("./cellsize")) {
		$cellsize = "./cellsize";
	} else {
		$cellsize = "";
	}

	// pcntl モジュールがあればシグナルハンドラを設定。
	// なければウィンドウサイズの変更が受け取れないだけなので
	// (そもそもコンソールとかだと飛んでこないし) 気にしない。
	if (function_exists("pcntl_signal")) {
		pcntl_signal(SIGWINCH, "signal_handler");
	}
	// 一度手動で呼び出して桁数を取得
	signal_handler(SIGWINCH);

	// NGワード取得
	read_ngword_file();
}

// ユーザストリーム
function stream()
{
	global $tw;

	// 古いキャッシュを削除
	invalidate_cache();

	// ミュートユーザ取得
	get_mute_list();

	// Disable timeout
	set_time_limit(0);

	// Finish all buffering
	while (ob_get_level()) {
		ob_end_clean();
	}

	print "Ready..";
	@fflush(STDOUT);

	// Start streaming
	$tw->streaming("user", "showstatus_callback",
		"headerfunction_callback");
}

function headerfunction_callback($ch, $text)
{
	global $f_connected;
	if (isset($f_connected) == false) {
		print "Connected.\n";
		$f_connected = true;
	}
	return strlen($text);
}

// 再生モード
function play()
{
	global $play_file;

	if ($play_file == "-") {
		$fp = STDIN;
	} else {
		$fp = fopen($play_file, "r");
	}

	while (($buf = fgets($fp))) {
		$object = json_decode($buf);
		showstatus_callback($object);
	}

	if ($play_file != "-") {
		fclose($fp);
	}
}

//
// 1ツイートを表示するコールバック関数
//
function showstatus_callback($object)
{
	global $mutelist;
	global $record_file;

	// $object が元オブジェクト (イベント or メッセージ)

	// 録画
	if ($record_file != "") {
		$fp = fopen($record_file, "a");
		fwrite($fp, json_encode($object)."\n");
		fclose($fp);
	}

	// https://dev.twitter.com/streaming/overview/messages-types#Events_event
	if (isset($object->event)) {
		// event => イベント種別
		//			"favorite", "unfavorite", "follow", "unfollow", ...
		// timestamp_ms => イベント発生時刻(UNIXTIME)
		// created_at => イベント発生時刻

		switch ($object->event) {
		 case "favorite":
			// source => ふぁぼ元ユーザ
			// target => ふぁぼ先ユーザ
			// target_object => ふぁぼったメッセージ
			$status = $object->target_object;

			// これだけだと、$status から $object が拾えないので
			// $object をバックリンクしておく。
			$status->object = $object;
			break;
		 case "follow":
			$time = coloring(formattime($object), COLOR_TIME);

			$s = $object->source;
			$src_userid = coloring(formatid($s->screen_name), COLOR_USERID);
			$src_name   = coloring(formatname($s->name), COLOR_USERNAME);

			$t = $object->target;
			$dst_userid = coloring(formatid($t->screen_name), COLOR_USERID);
			$dst_name   = coloring(formatname($t->name), COLOR_USERNAME);
			$src        = coloring("sayakaちゃんからお知らせ", COLOR_SOURCE);

			show_icon($s);
			print_("{$src_name} ${src_userid} が "
				.  "{$dst_name} {$dst_userid} をフォローしました。");
			print "\n";
			print_("{$time} {$src}");
			print "\n";
			print "\n";
			return;

		 case "mute":
			add_mute_list($object->target);
			return;

		 case "unmute":
			del_mute_list($object->target);
			return;

		 default:
			return;
		}

	} else if (isset($object->text)) {
		// 通常のツイート
		// $status はツイートメッセージ
		$status = $object;
	} else if (isset($object->friends)) {
		// 最初に送られてくる friends リストはいらない
		return;
	} else {
		// それ以外の情報はとりあえず無視
		return;
	}

	// ミュートしてるユーザも stream には流れてきてしまうので、ここで弾く
	if (isset($mutelist[$status->user->id_str])) {
		return;
	}
	if (isset($status->retweeted_status)) {
		if (isset($mutelist[$status->retweeted_status->user->id_str])) {
			return;
		}
	}

	// NGワード
	$ng = false;
	if (1 && ($ng = match_ngword($status)) !== false) {
		// マッチしたらここで表示
		$userid = coloring(formatid($ng['user']->screen_name), COLOR_NG);
		$name   = coloring(formatname($ng['user']->name), COLOR_NG);
		$time   = coloring(formattime($status), COLOR_NG);

		$msg = coloring("NG:{$ng['ngword']}", COLOR_NG);

		print_("{$name} {$userid}\n"
			.  "{$time} {$msg}");
		print "\n";
		print "\n";
		return;
	}

	showstatus($status);
	print "\n";
}

// 1ツイートを表示
function showstatus($status)
{
	global $indent_cols;
	global $indent_depth;
	global $protect;

	if (isset($status->object)) {
		$object = $status->object;
	}

	$s = $status;
	// RT なら、RT 元を $status、RT先を $s
	if (isset($status->retweeted_status)) {
		$s = $status->retweeted_status;
	}

	$userid = coloring(formatid($s->user->screen_name), COLOR_USERID);
	$name   = coloring(formatname($s->user->name), COLOR_USERNAME);
	$src    = coloring(unescape(strip_tags($s->source))." から", COLOR_SOURCE);
	$time   = coloring(formattime($s), COLOR_TIME);
	$verified = $s->user->verified
		? coloring(" ●", COLOR_VERIFIED)
		: "";
	$protected = $s->user->protected
		? coloring(" ■", COLOR_PROTECTED)
		: "";

	// --protect オプションなら鍵ユーザのツイートを表示しない
	if ($protect == true && $protected != "") {
		print_(coloring("鍵垢", COLOR_NG)."\n"
			.  "{$time}");
		print "\n";
		return;
	}

	list ($msg, $mediainfo) = formatmsg($s);

	show_icon($s->user);

	print_("{$name} {$userid}{$verified}{$protected}");
	print "\n";
	print_($msg);
	print "\n";

	// picture
	foreach ($mediainfo as $m) {
		print CSI."{$indent_cols}C";
		show_photo($m["target_url"], $m["width"]);
		print "\r";
	}

	// コメント付きRT の引用部分
	if (isset($s->quoted_status)) {
		// この中はインデントを一つ下げる
		print "\n";
		$indent_depth++;
		showstatus($s->quoted_status);
		$indent_depth--;
	}

	// このステータスの既 RT、既ふぁぼ数
	$rtmsg = "";
	$favmsg = "";
	// RT
	$rtcnt = $s->retweet_count;
	$rtcnt += 0;
	if ($rtcnt > 0) {
		$rtmsg = coloring(" {$rtcnt}RT", COLOR_RETWEET);
	}
	// fav
	$favcnt = $s->favorite_count;
	$favcnt += 0;
	if ($favcnt > 0) {
		$favmsg = coloring(" {$favcnt}Fav", COLOR_FAVORITE);
	}
	print_("{$time} {$src}{$rtmsg}{$favmsg}");
	print "\n";

	// リツイート元
	if (isset($status->retweeted_status)) {
		$rt_time   = formattime($status);
		$rt_userid = formatid($status->user->screen_name);
		$rt_name   = formatname($status->user->name);
		print_(coloring("{$rt_time} {$rt_name} {$rt_userid} がリツイート",
			COLOR_RETWEET));
		print "\n";
	}

	// ふぁぼ元
	if (isset($object->event) && $object->event == "favorite") {
		$fav_time   = formattime($object);
		$fav_userid = formatid($object->source->screen_name);
		$fav_name   = formatname($object->source->name);
		print_(coloring("{$fav_time} {$fav_name} {$fav_userid} がふぁぼ",
			COLOR_FAVORITE));
		print "\n";
	}
}

// インデント及び文字コード変換付きの printf ラッパー
// ただし "\n" は "\n"+インデント に置換するため、(メッセージ中に含まれる
// 改行のように) 自動インデントしてほしい改行はこの $msg に含めてよく、
// 逆にメッセージ行と日付行の間の改行のようなシステム的な改行は print "\n";
// のほうを使うこと。
function print_($msg)
{
	global $jis;
	global $eucjp;

	$msg = make_indent($msg);

	// 全角チルダ(U+FF5E)はおそらく全角チルダを表示したいのではなく、
	// Windows が波ダッシュ(U+301C)を表示しようとしたものだと解釈したほうが
	// 適用範囲が広いので、U+FF5E はすべて U+301C に変換してみる。
	$msg = str_replace("\xef\xbd\x9e", "\xe3\x80\x9c", $msg);

	// 全角ハイフンマイナス(U+FF0D)は環境によって表示出来ない可能性があるが
	// 全角マイナスで代用できるし、そっちはほぼ表示できるのだから、
	// すべて置換しておく。困るようなシチュエーションはないだろう。
	$msg = str_replace("\xef\xbc\x8d", "\xe2\x88\x92", $msg);

	if ($jis) {
		$msg = mb_convert_encoding($msg, "JIS", "UTF-8");
	} else if ($eucjp) {
		$msg = mb_convert_encoding($msg, "EUC-JP", "UTF-8");
	}
	print $msg;
}

// 名前表示用に整形
function formatname($text)
{
	return preg_replace("/[\r\n]/", " ", unescape($text));
}

// ID 表示用に整形
function formatid($text)
{
	return "@" . $text;
}

function unescape($text)
{
	return htmlspecialchars_decode($text, ENT_NOQUOTES);
}

function init_color()
{
	global $color2esc;
	global $color_mode;
	global $bg_white;
	global $opt_x68k;

	define("BOLD",		"1");
	define("UNDERSCORE","4");
	define("STRIKE",	"9");
	define("BLACK",		"30");
	define("RED",		"31");
	define("GREEN",		"32");
	define("BROWN",		"33");
	define("BLUE",		"34");
	define("MAGENTA",	"35");
	define("CYAN",		"36");
	define("WHITE",		"37");
	define("GRAY",		"90");
	define("YELLOW",	"93");

	// 黒背景か白背景かで色合いを変えたほうが読みやすい
	if ($bg_white) {
		$blue = BLUE;
	} else {
		$blue = CYAN;
	}

	// ユーザ名。白地の場合は出来ればもう少し暗めにしたい
	$username = BROWN;
	if ($bg_white && $color_mode > 16) {
		$username = "38;5;136";
	}

	// リツイートは緑色。出来れば濃い目にしたい
	$green = GREEN;
	if ($opt_x68k) {
		$green = "92";
	} else if ($color_mode > 16) {
		$green = "38;5;28";
	}

	// ふぁぼは黄色。白地の場合は出来れば濃い目にしたいが
	// こちらは太字なのでユーザ名ほどオレンジにしなくてもよさげ。
	$fav = BROWN;
	if ($bg_white && $color_mode > 16) {
		$fav = "38;5;184";
	}

	$color2esc = array(
		"COLOR_USERNAME"	=> $username,
		"COLOR_USERID"		=> $blue,
		"COLOR_TIME"		=> GRAY,
		"COLOR_SOURCE"		=> GRAY,

		"COLOR_RETWEET"		=> BOLD.";".$green,
		"COLOR_FAVORITE"	=> BOLD.";".$fav,
		"COLOR_URL"			=> UNDERSCORE.";".$blue,
		"COLOR_TAG"			=> $blue,
		"COLOR_VERIFIED"	=> CYAN,
		"COLOR_PROTECTED"	=> GRAY,
		"COLOR_NG"			=> STRIKE.";".GRAY,
	);
}

function coloring($text, $color_type)
{
	global $color2esc;

	if (isset($color2esc[$color_type])) {
		$rv = CSI."{$color2esc[$color_type]}m". $text .CSI."0m";
	} else {
		$rv = "coloring({$text},{$color_type})";
	}
	return $rv;
}

// $object の日付時刻を表示用に整形して返す。
// timestamp_ms があれば使い、なければ created_at を使う。
// 今のところ、timestamp_ms はたぶん新しめのツイート/イベント通知には
// 付いてるはずだが、リツイートされた側は created_at しかない模様。
function formattime($object)
{
	$unixtime = isset($object->timestamp_ms)
		? ($object->timestamp_ms / 1000)
		: conv_twtime_to_unixtime($object->created_at);

	if (strftime("%F", $unixtime) == strftime("%F", time())) {
		// 今日なら時刻のみ
		return strftime("%T", $unixtime);
	} else {
		// 今日でなければ日付時刻
		return strftime("%F %T", $unixtime);
	}
}

// ステータスを解析しながら本文を整形したり情報抜き出したりを同時にする。
// 戻り値は array($msg, $mediainfo)。
function formatmsg($s)
{
	global $imagesize;

	$mediainfo = array();

	// 本文
	$text = $s->text;

	// タグ情報を展開
	// 文字位置しか指定されてないので、$text に一切の変更を加える前に
	// 調べないとタグが分からないというクソ仕様…。
	if (isset($s->entities) && count($s->entities->hashtags) > 0) {
		$tags = array();
		foreach ($s->entities->hashtags as $t) {
			// t->indices[0] … 開始位置、1文字目からなら0
			// t->indices[1] … 終了位置。この1文字前まで
			$tags[] = $t->indices[0];
			$tags[] = $t->indices[1];
		}

		$splittext = utf8_split($text, $tags);
		$text = "";
		for ($i = 0; $i < count($splittext); $i++) {
			if ($i & 1) {
				$text .= coloring($splittext[$i], COLOR_TAG);
			} else {
				$text .= $splittext[$i];
			}
		}
	}

	// ハッシュタグが済んでからエスケープと改行を整形
	$text = unescape($text);
	$text = str_replace("\r\n", "\n", $text);
	$text = str_replace("\r", "\n", $text);

	// ユーザID
	$text = preg_replace_callback(
			"/(^|[^A-Za-z\d])(@\w+)/",
			function($m) { return $m[1].coloring($m[2], COLOR_USERID); },
			$text);

	// 短縮 URL を展開
	if (isset($s->entities) && isset($s->entities->urls)) {
		foreach ($s->entities->urls as $u) {
			//   url         本文中の短縮 URL (twitterから)
			//   display_url 差し替えて表示用の URL (twitterから)
			//   expanded_url 展開後の URL (twitterから)
			$disp = $u->display_url;
			$exp  = $u->expanded_url;

			// 本文の短縮 URL を差し替える
			if (isset($s->quoted_status)
			 && preg_match("|/{$s->quoted_status_id_str}$|", $exp)
			 && preg_match("|{$u->url}\s*$|", $text))
			{
				// この場合はコメント付き RT の URL なので取り除く
				$text = preg_replace("|{$u->url}|", "", $text);
			} else {
				// indices 使ってないけどまあ大丈夫だろう
				$text = preg_replace("|{$u->url}|",
					coloring($disp, COLOR_URL), $text);
			}

			// 外部画像サービスを解析
			$minfo = format_image_url($exp, $disp);
			if ($minfo !== false) {
				$mediainfo[] = $minfo;
			}
		}
	}

	// メディア情報を展開
	if (isset($s->extended_entities) && isset($s->extended_entities->media)) {
		foreach ($s->extended_entities->media as $m) {
			// 本文の短縮 URL を差し替える
			// indices 使ってないけどまあ大丈夫だろう

			// 画像複数枚貼り付けてるとこの preg_replace をn回通るけど
			// 一応副作用はないので気にしないことにするか

			$text = preg_replace("|{$m->url}|",
				coloring($m->display_url, COLOR_URL), $text);

			// あとで画像展開につかうために覚えておく
			//   url         本文中の短縮 URL (twitterから)
			//   display_url 差し替えて表示用の URL (twitterから)
			//   media_url   指定の実ファイル URL (twitterから)
			//   target_url  それを元に実際に使う URL
			//   width       幅指定。ピクセルか割合で

			// pic.twitter.com の画像のうち :thumb は縮小ではなく切り抜き
			// なので使わない。:small は縦横比に関わらず横 340px に縮小。
			// 横長なら 340 x (340以下)、縦長なら 340 x (340以上) になって
			// そのままでは縦長写真と横長写真で縮尺が揃わないクソ仕様なので
			// ここでは長辺を基準に 40% に縮小する。
			$w = $m->sizes->small->w;
			$h = $m->sizes->small->h;
			if ($h > $w) {
				$width = intval(($w / $h) * $imagesize);
			} else {
				$width = $imagesize;
			}

			$mediainfo[] = array(
				"display_url" => $m->display_url,
				"target_url"  => "{$m->media_url}:small",
				"width"       => $width,
			);
		}
	}

	return array($text, $mediainfo);
}

// 外部画像サービス URL を解析した結果を返す
// なければ false を返す
function format_image_url($exp, $disp)
{
	global $imagesize;

	$target = "";
	$width = "";

	if (preg_match("|twitpic.com/(\w+)|", $exp, $m)) {
		$target = "http://twitpic.com/show/mini/{$m[1]}";

	} else if (preg_match("|movapic.com/(pic/)?(\w+)|", $exp, $m)) {
		$target = "http://image.movapic.com/pic/t_{$m[2]}.jpeg";

	} else if (preg_match("|p.twipple.jp/(\w+)|", $exp, $m)) {
		$target = "http://p.twpl.jp/show/thumb/{$m[1]}";

	} else if (preg_match("|(.*instagram.com/p/[\w\-]+)/?|", $exp, $m)) {
		$target = "{$m[1]}/media/?size=t";

	} else if (preg_match("/\.(jpg|jpeg|png|gif)$/", $exp)) {
		$target = $exp;
		$width = $imagesize;

	} else {
		return false;
	}

	return array(
		"target_url" => $target,
		"display_url" => $disp,
		"width" => $width,
	);
}

// インデントをつける
function make_indent($text)
{
	global $screen_cols;
	global $indent_cols;
	global $indent_depth;

	// 桁数が分からない場合は何もしない
	if ($screen_cols == 0) {
		return $text;
	}

	// インデント階層
	$left = $indent_cols * ($indent_depth + 1);
	$indent = CSI."{$left}C";

	$state = "";
	$newtext = $indent;
	$x = $left;
	$s = preg_split("//", $text, -1, PREG_SPLIT_NO_EMPTY);
	for ($i = 0; $i < count($s); ) {
		switch ($state) {
		 case "esc":
			$newtext .= $s[$i];
			if ($s[$i] == "m") {
				$state = "";
			}
			$i++;
			break;

		 case "":
			if ($s[$i] == ESC) {
				$state = "esc";
				break;
			} else if ($s[$i] == "\n") {
				$newtext .= $s[$i];
				$newtext .= $indent;
				$x = $left;
				$i++;
			} else if (ord($s[$i]) < 0x80) {
				$newtext .= $s[$i];
				$x++;
				$i++;
			} else if (ord($s[$i]) == 0xef && utf8_ishalfkana($s, $i)) {
				// 半角カナ
				$newtext .= $s[$i] . $s[$i + 1] . $s[$i + 2];
				$x++;
				$i += 3;
			} else {
				// とりあえず全部全角扱い
				if ($x > $screen_cols - 2) {
					$newtext .= "\n";
					$newtext .= $indent;
					$x = $left;
				}
				$clen = utf8_charlen($s[$i]);
				for ($j = 0; $j < $clen; $j++) {
					$newtext .= $s[$i++];
				}
				$x += 2;
			}
			if ($x > $screen_cols - 1) {
				$newtext .= "\n";
				$newtext .= $indent;
				$x = $left;
			}
			break;
		}
	}
	return $newtext;
}

// 現在のカーソル位置に user のアイコンを表示。
// アイコン表示後にカーソル位置を表示前の位置に戻す。
function show_icon($user)
{
	global $iconsize;
	global $color_mode;

	// 改行x3 + カーソル上移動x3 を行ってあらかじめスクロールを発生させ
	// アイコン表示時にスクロールしないようにしてからカーソル位置を保存する
	// (スクロールするとカーソル位置復元時に位置が合わない)
	print "\n\n\n".CSI."3A".ESC."7";

	$screen_name = unescape($user->screen_name);
	$image_url = $user->profile_image_url;

	// URLのファイル名部分をキャッシュのキーにする
	$filename = basename($image_url);
	$img_file = "icon-{$iconsize}x{$iconsize}-{$screen_name}-{$filename}";

	if (show_image($img_file, $image_url, $iconsize, -1) === false) {
		print "\n\n\n";
	}

	print "\r";
	// カーソル位置保存/復元に対応していない端末でも動作するように
	// カーソル位置復元前にカーソル上移動x3を行う
	print CSI."3A".ESC."8";
}

function show_photo($img_url, $percent)
{
	$img_file = preg_replace("|[:/\(\)\? ]|", "_", $img_url);
	show_image($img_file, $img_url, $percent);
}

// 画像をキャッシュして表示
//  $img_file はキャッシュディレクトリ内でのファイル名
//  $img_url は画像の URL
//  $width は画像の幅。ピクセルかパーセントで指定。
// 表示できれば真を返す。
function show_image($img_file, $img_url, $width)
{
	global $cachedir;
	global $img2sixel;
	global $indent_cols;
	global $indent_depth;

	// CSI."0C" は0文字でなく1文字になってしまうので、必要な時だけ。
	if ($indent_depth > 0) {
		$left = $indent_cols * $indent_depth;
		print CSI."{$left}C";
	}

	// img2sixel 使わないモードならここで帰る
	if ($img2sixel == "") {
		return false;
	}

	$img_file = "{$cachedir}/{$img_file}.sixel";

	if (strlen($width) > 0) {
		$width = "-w {$width}";
	} else {
		$width = "";
	}

	if (!file_exists($img_file)) {
		$imgconv = "{$img2sixel} {$width}";
		system("(curl -Lks {$img_url} | "
		     . "{$imgconv} > {$img_file}) 2>/dev/null");
	}
	if (filesize($img_file) == 0) {
		unlink($img_file);
		return false;
	} else {
		// ファイルを読んで標準出力に吐き出す(標準関数)
		readfile($img_file);
		@fflush(STDOUT);
		return true;
	}
}

// ミュートユーザ一覧の読み込み。
// $mutelist は "id_str" => "id_str" の連想配列。
// 今は毎回 twitter から取得しているだけ。
function get_mute_list()
{
	global $tw;
	global $mutelist;

	// ミュートユーザ一覧は一度に全部送られてくるとは限らず、
	// next_cursor{,_str} が 0 なら最終ページ、そうでなければ
	// これを cursor に指定してもう一度リクエストを送る。

	$mutelist = array();
	$cursor = "0";

	do {
		$options = array();
		if ($cursor != "0") {
			$options["cursor"] = $cursor;
		}
		try {
			$json = $tw->get("mutes/users/ids", $options);
		} catch (TwistException $e) {
			$message = $e->GetMessage();
			print "get_mute_list failed: {$message}\n";
			exit(1);
		}

		foreach ($json->ids as $id) {
			$mutelist[$id] = $id;
		}

		$cursor = $json->next_cursor_str;
	} while ($cursor != "0");
}

// ミュートユーザを追加
function add_mute_list($user)
{
	global $mutelist;

	$mutelist[$user->id_str] = $user->id_str;
}

// ミュートユーザを削除
function del_mute_list($user)
{
	global $mutelist;

	unset($mutelist[$user->id_str]);
}

// 取得したミュートユーザの一覧を表示する
function cmd_mutelist()
{
	global $mutelist;

	get_mute_list();

	foreach ($mutelist as $id => $value) {
		print "{$id}\n";
	}
}

// 古いキャッシュを破棄する
function invalidate_cache()
{
	global $cachedir;

	// そもそもキャッシュディレクトリがあるか
	if (!is_dir($cachedir)) {
		print "No cachedir found!!: {$cachedir}\n";
		return;
	}

	// アイコンは7日分くらいか
	system("find {$cachedir} -name icon-\* -type f -atime +7 -exec rm {} +");

	// 写真は24時間分くらいか
	system("find {$cachedir} -name http\* -type f -atime +1 -exec rm {} +");
}

// UTF-8 文字列を分割する。
//  utf8_split("abcdef", array(1, 3, 5, 6));
//  rv = array("a", "bc", "de", "f");
//
// 元々 mb_substr() を使っていたが、mbstring 拡張だけで約 1.5MB あって
// php の footprint に響くので、ここでは mbstring を使わずに書いてみる。
function utf8_split($str, $charpos)
{
	$len = strlen($str);

	// 文字のインデックスをバイトインデックスに変換
	$charindex = 0;
	$bytepos = array(0);
	$i = 0;
	for ($j = 0; $j < count($charpos); $j++) {
		while ($i < $len) {
			if ($charindex == $charpos[$j]) {
				$bytepos[] = $i;
				break;
			}
			$chlen = utf8_charlen($str[$i]);
			if ($chlen == 0) {
				// 文字として数えない
				$i++;
			} else {
				$i += $chlen;
				$charindex++;
			}
		}
	}

	// バイトインデックスで分割
	$rv = array();
	for ($i = 0; $i < count($bytepos) - 1; $i++) {
		$rv[] = substr($str,
			$bytepos[$i],
			$bytepos[$i + 1] - $bytepos[$i]);
	}
	$rv[] = substr($str, $bytepos[count($bytepos) - 1]);

	return $rv;
}

// UTF-8 文字の先頭バイトからこの文字のバイト数を返す
function utf8_charlen($c)
{
	$c = ord($c);
	// UTF-8 は1バイト目で1文字のバイト数が分かる
	if ($c <= 0x7f) {
		return 1;
	} else if ($c < 0xc2) {
		return 0;
	} else if ($c <= 0xdf) {
		return 2;
	} else if ($c <= 0xef) {
		return 3;
	} else if ($c <= 0xf7) {
		return 4;
	} else if ($c <= 0xfb) {
		return 5;
	} else if ($c <= 0xfd) {
		return 6;
	} else {
		return 0;
	}
}

// UTF-8 文字が半角カナなら真を返す。
// $s は UTF-8 文字列を1バイトごとに分解した配列。
// その $i 番目(から3バイト) を調べる。
// ただし先頭バイトが 0xef であることは調査済み。
function utf8_ishalfkana($s, $i)
{
	// UTF-8 の半角カナは次の2ブロック
	// 0xef bd a1 - 0xef bd bf
	// 0xef be 80 - 0xef be 9f

	// 入力が不正なら ord() が 0 になるのでこの次の if が成立しない
	$s1 = ord($s[$i + 1]);
	$s2 = ord($s[$i + 2]);

	if ($s1 == 0xbd && (0xa1 <= $s2 && $s2 <= 0xbf))
		return true;
	if ($s1 == 0xbe && (0x80 <= $s2 && $s2 <= 0x9f))
		return true;
	return false;
}

// NG ワードをデータベースから読み込む
function read_ngword_file()
{
	global $ngwordfile;
	global $ngwords;

	$ngwords = array();

	if (file_exists($ngwordfile)) {
		$file = json_decode(file_get_contents($ngwordfile));
		if (isset($file->ngword_list)) {
			$ngwords = $file->ngword_list;
		}
	}
}

// NG ワードと照合する。
// 一致したら array(
//  "word" => $ngwords['word'],
//	"user" => userオブジェクト,
// ) を返す。
// 一致しなければ false を返す。
function match_ngword($status)
{
	global $ngwords;

	$user = false;	// マッチしたユーザ
	foreach ($ngwords as $ng) {
		if (isset($status->retweeted_status)) {
			$s = $status->retweeted_status;

			if ($ng->user == "") {
				// ユーザ指定がなければ、RT先本文を比較
				if (match_ngword_main($ng, $s)) {
					$user = $s->user;
				}
			} else {
				// ユーザ指定があって、RT元かRT先のユーザと一致すれば
				// RT先本文を比較。ただしユーザ情報はマッチしたほう。
				if (match_ngword_user($ng->user, $status)) {
					if (match_ngword_main_rt($ng, $s)) {
						$user = $status->user;
					}
				} else if (match_ngword_user($ng->user, $s)) {
					if (match_ngword_main($ng, $s)) {
						$user = $s->user;
					}
				}
			}
		} else {
			// RT でないステータス
			// ユーザ指定がないか、あって一致すれば、本文を比較
			if ($ng->user == "" || match_ngword_user($ng->user, $status)) {
				if (match_ngword_main($ng, $status)) {
					$user = $status->user;
				}
			}
		}

		if ($user !== false) {
			$rv = array(
				"user" => $user,
				"ngword" => $ng->ngword,
			);
			return $rv;
		}
	}

	return false;
}

// ツイート status がユーザ ng_user のものか調べる。
// ng_user は "id:<numeric_id>" か "@<screen_name>" 形式。
function match_ngword_user($ng_user, $status)
{
	if (preg_match("/^id:(\d+)$/", $ng_user, $m)) {
		$ng_user_id = $m[1];
		if ($ng_user_id == $status->user->id_str) {
			return true;
		}
	}
	if (preg_match("/^@(\S+)/", $ng_user, $m)) {
		$ng_screen_name = $m[1];
		if ($ng_screen_name == $status->user->screen_name) {
			return true;
		}
	}

	return false;
}

// status の本文その他を NGワード ng と照合する。
// マッチしたかどうかを返す。
function match_ngword_main($ng, $status)
{
	// 生実況 NG
	// %LIVE,www,hh:mm,HH:MM,comment
	// www曜日、hh:mmからHH:MMまでの間、該当ユーザのツイートを非表示にする
	// HH:MM は24時を越えることが出来る
	if (preg_match("/\%LIVE,(\w+),([\d:]+),([\d:]+)/", $ng->ngword, $match)) {
		// 曜日と時刻2つを取り出す
		$t1 = strptime($match[1], "%a");
		$t2 = strptime($match[2], "%R");
		$t3 = strptime($match[3], "%R");
		$t4 = false;
		// 終了時刻が 24時を越える場合にも対応
		if ($t3 === false && preg_match("/(\d+):(\d+)/", $match[3], $mm)) {
			$h = $mm[1] + 0;
			// 24時を越えていれば
			if ($h >= 24) {
				// $t3 は一旦 24時にする
				$t3 = array(
					'tm_hour' => 24,
					'tm_min' => 0,
				);

				// $t4 が実際の終了時刻
				$h -= 24;
				$t4 = strptime("{$h}:{$mm[2]}", "%R");
			}
		}

		$wday  = $t1['tm_wday'];
		$start = $t2['tm_hour'] * 60 + $t2['tm_min'];
		$end   = $t3['tm_hour'] * 60 + $t3['tm_min'];

		// 発言時刻
		$unixtime = isset($status->timestamp_ms)
			? intval($status->timestamp_ms / 1000)
			: conv_twtime_to_unixtime($status->created_at);
		$tm = localtime($unixtime, true);
		$tmmin = $tm['tm_hour'] * 60 + $tm['tm_min'];

		// 指定曜日の時間の範囲内ならアウト
		if ($tm['tm_wday'] == $wday && $start <= $tmmin && $tmmin < $end) {
			return true;
		}
		// 終了時刻が24時を越える場合は、越えたところも比較
		if ($t4 !== false) {
			$wday = ($wday + 1) % 7;
			$start = 0;
			$end = $t4['tm_hour'] * 60 + $t4['tm_min'];
			if ($tm['tm_wday'] == $wday && $start <= $tmmin && $tmmin < $end) {
				return true;
			}
		}
	}

	// クライアント名
	if (preg_match("/%SOURCE,(.*)/", $ng->ngword, $match)) {
		if (preg_match("/{$match[1]}/", $status->source)) {
			return true;
		}
	}

	// 単純ワード比較
	if (preg_match("/{$ng->ngword}/si", $status->text)) {
		return $status->user;
	}

	return false;
}

// status の本文その他を NG ワード ng と照合する。
// リツイートメッセージ用。
function match_ngword_main_rt($ng, $status)
{
	// まず通常比較
	if (match_ngword_main($ng, $status)) {
		return true;
	}

	// 名前も比較
	if (preg_match("/{$ng->ngword}/i", $status->user->screen_name)) {
		return true;
	}
	if (preg_match("/{$ng->ngword}/i", $status->user->name)) {
		return true;
	}

	return false;
}


// シグナルハンドラ
function signal_handler($signo)
{
	global $screen_cols;
	global $tput;
	global $cellsize;
	global $opt_fontwidth;
	global $opt_fontheight;
	global $fontwidth;
	global $fontheight;
	global $indent_cols;
	global $iconsize;
	global $imagesize;
	global $debug;

	switch ($signo) {
	 case SIGWINCH:
		// 付属の cellsize か、なければ tput を試す
		if ($cellsize != "") {
			$metric = preg_split("/\s+/", `{$cellsize} -a`);
			$ws_cols   = $metric[0];
			$ws_width  = $metric[2];
			$ws_height = $metric[3];

		} else if ($tput != "") {
			// ターミナル1行の桁数を取得
			$ws_cols = rtrim(`{$tput} cols`);
		}
		$ws_cols += 0;
		$ws_width += 0;
		$ws_height += 0;

		// 画面幅は常に更新
		$screen_cols = 0;
		if ($ws_cols > 0) {
			$screen_cols = $ws_cols;
		}

		// フォント幅と高さは指定されてない時だけ取得した値を使う
		$use_default_size = false;
		if ($opt_fontwidth != "") {
			$fontwidth = $opt_fontwidth;
		} else {
			if ($ws_width > 0) {
				$fontwidth = $ws_width;
			} else {
				$fontwidth = DEFAULT_FONT_WIDTH;
				$use_default_size = true;
			}
		}
		if ($opt_fontheight != "") {
			$fontheight = $opt_fontheight;
		} else {
			if ($ws_height > 0) {
				$fontheight = $ws_height;
			} else {
				$fontheight = DEFAULT_FONT_HEIGHT;
				$use_default_size = true;
			}
		}
		$fontwidth  = intval($fontwidth + 0);
		$fontheight = intval($fontheight + 0);
		if ($use_default_size) {
			print "sayaka: Fontsize not detected. ";
			print "Application default {$fontwidth}x{$fontheight} is used.\n";
		}

		// フォントの高さからアイコンサイズを決定する。
		//
		// SIXEL 表示後のカーソル位置は、
		// o xterm 等では SIXEL 最終ラスタを含む行の次の行、
		// o VT382 等では SIXEL 最終ラスタの次のラスタを含む行
		// になる。
		// アイコンは2行以上3行未満にする必要があり、
		// かつ6の倍数だと SIXEL 的に都合がいい。
		$iconsize  = intval(($fontheight * 3 - 1) / 6) * 6;
		// 画像サイズにはアイコンのような行制約はないので計算は適当。
		// XXX まだ縦横について考慮してない
		$imagesize = intval(($fontheight * 9 - 1) / 6) * 6;

		// そこからインデント幅を決定
		$indent_cols = intval($iconsize / $fontwidth) + 1;

		if ($debug) {
			print "screen columns={$screen_cols}\n";
			print "font height={$fontheight}\n";
			print "font width={$fontwidth}\n";
			print "iconsize={$iconsize}\n";
			print "indent columns={$indent_cols}\n";
			print "imagesize={$imagesize}\n";
		}
		break;
	 default:
		break;
	}
}

function cmd_version()
{
	global $version;

	print "sayaka.php ${version}\n";
}

function usage()
{
	global $progname;

	cmd_version();
	print <<<__EOM__
usage:
 {$progname} --stream
	streaming mode.
		--color <n>: color mode { 2, 16, 256 }. default 256.
		--font <w>x<h>: font height x width. default 7x14.
		--white
		--noimg
		--jis
		--eucjp
		--protect : don't display protected user's tweet
		--record <file>
		--token <file> : token file (default: ~/.sayaka/token.json)
 {$progname} [ --pipe | --post "msg" ]
	tweet from stdin or "msg"(without quote)
 {$progname} --play <file>
	replay the recorded file as stream

__EOM__;
	exit(0);
}
?>
