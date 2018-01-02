twitter クライアント sayaka ちゃん version 3.3.2 (2018/01/02)
======

ターミナルに特化した twitter クライアントです。

* ユーザストリームの垂れ流しが出来ます。
* mlterm などの sixel 対応ターミナル用です。
* X68030/25MHz、メモリ12MB でも快適(?)動作。

更新履歴
---
* 3.3.2 (2018/01/02) … pkgsrc-2017Q4 (vala-0.38.1以上) でのビルドに対応。
	画像は Content-Type が image/* の時のみ表示するようにしてみる。
	mbedTLS-2.4.2 に更新。
	--timeout-image オプションを実装。
* 3.3.1 (2016/12/23) … リソースリークを含むバグ修正。
* 3.3.0 (2016/11/25) … libcurl ではなく mbedTLS に移行してみる。
	--full-url オプション、--progress オプションを実装。
	--sixel-cmd オプション廃止、PHP 版サポート廃止。
	EUC-JP/JIS に変換できない文字の処理を追加。
	画像の高品質化、高速化いろいろ。
* 3.2.2 (2016/09/25) … glib-networking ではなく libcurl に移行してみる。
	--post オプション、--ciphers オプションを実装。
	extended_tweet の表示に対応。
* 3.2.1 (2016/04/24) … --filter オプション、--record オプションを実装。
	NGワード編集機能実装。
	「リツイートを非表示」にしたユーザに対応。
	shindanmaker の画像サムネイルに対応。
	Unicode 外字をコード表示。
	連続するリツイート・ふぁぼを圧縮して表示。
* 3.2.0 (2016/02/24) … vala 版サポート。
	画像の横方向への展開サポート (vala 版のみ)。
	SQLite3 データベース廃止 (PHP 版のみ)。
* 3.1.0 (2015/07/26) …
	--font オプションの仕様変更。
	VT382(?)など(いわゆる)半角フォントの縦横比が 1:2 でない環境に対応。
	--noimg の時に改行が一つ多かったのを修正。
* 3.0.9 (2015/06/14) …
	--eucjp、--protect オプションを追加しました。
	またコメント付き RT の仕様変更(?)に追従しました。
* 3.0.8 (2015/05/03) …
	--font オプションを追加して、画像サイズを連動するようにしました。
* 3.0.7 (2015/04/19) … コメント付き RT の表示に対応してみました。
* 3.0.6 (2014/12/06) … libsixel 1.3 に対応。libsixel 1.3 未満は使えません。
* 3.0.5 (2014/10/23) … 本文を折り返して表示。



必要なもの
---
* vala-0.38.1 以上
* glib2-2.44 以上
* gdk-pixbuf2
* libjpeg
* GNU make

pkgsrc なら
lang/vala, devel/glib2, graphics/gdk-pixbuf2, graphics/jpeg, devel/gmake
をインストールしてください。


インストール方法
---
展開したディレクトリ内だけで動作しますので、
適当なところに展開してください。
といいつつ後述の理由により ~/.sayaka/ に展開することを前提にしています。

コンパイルは以下のようにします。

```
% gmake
```

make install はないので、出来上がった vala/sayaka (実行ファイル) をパスの通ったところにインストールするとかしてください。


とりあえず使ってみる
---
sayaka を起動します。引数なしだとユーザストリームモードになります。
初回起動時に `~/.sayaka` と `~/.sayaka/cache` のディレクトリを作成します。
また初回起動時というか `~/.sayaka/token.json` ファイルがない状態では
まず認証を行います。URL が表示されるのでこれをブラウザで開き、
アプリ連携を許可して、表示された PIN コードを入力してください。
```
% sayaka
Please go to:
https://twitter.com/...

And input PIN code:
```

PIN を入力するとただちにユーザストリームモードになります。
2回目以降は sayaka を起動するだけです。


コマンドライン引数など
---
* `-4` … IPv4 のみを使用します。

* `-6` … IPv6 のみを使用します。

* `--ciphers <ciphers>` ユーザストリームに使用する暗号化スイートを指定します。
	今のところ指定できるのは "RSA" のみです。
	このオプションはユーザストリーム以外 (REST API や画像ファイルのダウンロード)
	には適用されません。

* `--color <n>` … 色数を指定します。デフォルトは 256色です。
	他はたぶん 16 と 2 (と 8?) くらいを想定しています。

* `--eucjp` … 文字コードを EUC-JP に変換して出力します。
	VT382J 等の EUC-JP (DEC漢字) に対応したターミナルで使えます。

* `--filter <keyword>` … キーワード検索を行います。
	通常のタイムラインは表示しません。

* `--font <W>x<H>` … フォントの幅と高さを `--font 7x14` のように指定します。
	デフォルトではターミナルに問い合わせて取得しますが、
	ターミナルが対応してない場合などは勝手に 7x14 としますので、
	もし違う場合はこの `--font` オプションを使って指定してください。
	アイコンと画像はここで指定したフォントサイズに連動した大きさで表示されます。

* `--full-url` … URL が省略形になる場合でも元の URL を表示します。

* `--jis` … 文字コードを JIS に変換して出力します。
	NetBSD/x68k コンソール等の JIS に対応したターミナルで使えます。

* `--max-cont <n>` … 同一ツイートに対するリツイートやふぁぼが連続した場合に
	表示を簡略化しますが、その上限数を指定します。デフォルトは 10 です。
	0 以下を指定すると簡略化を行いません(従来どおり)。

* `--max-image-cols <n>` … 1行に表示する画像の最大数です。
	デフォルトは 0 で、この場合ターミナル幅を超えない限り横に並べて表示します。
	ターミナル幅、フォント幅が取得できないときは 1 として動作します。

* `--ngword-add <ngword>` … NGワードを追加します。
	正規表現が使えます、というか `/<ngword>/i` みたいな感じで比較します。
	同時に `--user` オプションを指定すると、ユーザを限定できます。

* `--ngword-del <id>` … NGワードを削除します。
	`<id>` は `--ngword-list` で表示される1カラム目のインデックス番号を
	指定してください。

* `--ngword-list` … NGワード一覧を表示します。
	タブ区切りで、インデックス番号、NGワード、(あれば)ユーザ指定、です。

* `--noimg` … SIXEL 画像を一切出力しません。SIXEL 非対応ターミナル用。

* `--ormode <on|off>` … on なら SIXEL を独自実装の OR モードで出力します。
	デフォルトは off です。
	ターミナル側も OR モードに対応している必要があります。

* `--palette <on|off>` … on なら SIXEL 画像にパレット定義情報を出力します。
	デフォルトは on です。
	NetBSD/x68k SIXEL 対応パッチのあててある俺様カーネルでは、
	SIXEL 画像内のパレット定義を参照しないため、off にすると少しだけ
	高速になります。
	それ以外の環境では on のまま使用してください。

* `--post` … 標準入力の内容をツイート(投稿)します。
	文字コードは UTF-8 にしてください。

* `--play` … ユーザストリームの代わりに標準入力の内容を再生します。

* `--progress` … 接続完了までの処理を表示します。
	遅マシン向け。

* `--protect` … 鍵付きアカウントのツイートを表示しません。
	デモ展示などの際にどうぞ。

* `--record <file>` … ユーザストリームで受信したすべてのデータを
	`<file>` に記録します。`--play` コマンドで再生できます。

* `--support-evs` … EVS (絵文字セレクタ?) 文字をそのまま出力します。
	デフォルトは EVS 非対応ターミナル用に EVS 文字を出力しません。

* `--timeout-image <msec>` … 画像取得のサーバへの接続タイムアウトを設定します。
	デフォルトは 3000 (3秒)です。
	0 を指定すると connect(2) のタイムアウト時間になります。

* `--token <file>` … 認証トークンファイルを指定します。
	デフォルトは `~/.sayaka/token.json` です。

* `--user <user>` … NGワードに指定するユーザ情報です。
	`--ngword-add` オプションとともに使用します。
	`<user>` は `@<screen_name>` か `id:<user_id>` で指定します。

* `--white` … 文字色を白背景用の濃いめの色合いに変更します。
	デフォルトは黒背景用に明るめの文字色になっています。

* `--x68k` … NetBSD/x68k 用モードです。
	実際には `--color x68k --font 8x16 --jis --black --progress --ormode on --palette off` と等価です。
