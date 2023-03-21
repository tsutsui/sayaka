twitter クライアント sayaka ちゃん version 3.6.1 (2023/03/21)
======

ターミナルに特化した twitter クライアントです。

* REST API (v1.1) によるホームタイムラインの新着表示ができます。
* mlterm などの SIXEL 対応ターミナル用です。
* X68030/25MHz、メモリ12MB でも快適(?)動作。

更新履歴
---
* 3.6.1 (2023/03/21) … ゼロ除算を修正。接続間隔を調整。
* 3.6.0 (2023/03/18) … フィルタストリーム廃止に伴い REST API (v1.1) で仮復旧。
	--reconnect オプションを廃止。
* 3.5.5 (2023/02/28) … GIF 画像をサポート。
* 3.5.4 (2022/11/08) … --force-sixel オプションを実装。
	--progress 指定時の表示エンバグ修正。
* 3.5.3 (2022/02/15) … キーワードの複数指定に対応。
	--mathalpha オプションを実装。
* 3.5.2 (2021/07/22) … 自動再接続と --reconnect オプションを実装。
* 3.5.1 (2021/03/18) … アイコン取得を HTTPS でなく HTTP, HTTPS
	の順で試すよう変更。
	エラー処理をいくつか改善。
* 3.5.0 (2021/03/03) … C++ に移行し vala 版廃止。
	画像は現在のところ JPEG, PNG のみ対応。
	ターミナル背景色の自動取得を実装。
	--protect、--support-evs オプション廃止。
	--noimg オプションを廃止 (--no-image に変更)。
	userstream 時代の録画データの再生機能廃止。
* 3.4.6 (2020/11/10) … --no-image 指定時のアイコン代わりのマークが
	化ける場合があったのでマークを変更。
* 3.4.5 (2020/05/15) … 表示判定を再実装して
	フォローから非フォローへのリプライが表示されてしまう場合があるのを修正。
	NG ワード判定が漏れるケースがあったのを修正。
	`--record-all` オプションを実装。
* 3.4.4 (2020/05/01) … Linux で SIGWINCH 受信で終了していたのを修正。
	リツイートの連続表示を修正。SIXEL 判定のタイムアウトを延長。
	--token オプションの動作を変更。ログ周りを色々修正。
* 3.4.3 (2020/02/15) … 引用ツイートが表示されないケースがあったのを修正。
	SIXEL 対応ターミナルの判別を改善。
* 3.4.2 (2020/02/01) … 2色のターミナルに対応。--no-color オプションを実装。
	--no-image オプションを用意 (従来の --noimg も使用可)。
	SIXEL 非対応ターミナルならアイコンの代わりにマークを表示。
* 3.4.1 (2020/01/12) … 疑似ホームタイムラインの調整。
	SIXEL 非対応ターミナルを自動判別してみる。
* 3.4.0 (2020/01/05) … フィルタストリームによる擬似ホームタイムラインに対応。
* 3.3.3 (2020/01/04) … Linux でのビルドエラーを修正。
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
* C++17 compiler
* giflib (maybe >= 5.0)
* jpeg (libjpeg)
* libpng
* mbedtls 2.x (2.9.0 or later?)
* BSD make (not GNU make)

pkgsrc をお使いなら
graphics/giflib, graphics/jpeg, graphics/png, security/mbedtls
をインストールします。


インストール方法
---
ビルドは以下のようにします。

```
% ./configure
% make sayaka
```

make install はないので、出来上がった src/sayaka (実行ファイル) をパスの通ったところにインストールするとかしてください。
ちなみに `make all` すると、画像ファイルを SIXEL に変換して表示する
sixelv というビューアも出来ます (sayaka の実行には不要です)。


とりあえず使ってみる
---
sayaka を起動します。
初回起動時に `~/.sayaka` と `~/.sayaka/cache` のディレクトリを作成します。
また初回起動時というか `~/.sayaka/token.json` ファイルがない状態では
まず認証を行います。URL が表示されるのでこれをブラウザで開き、
アプリ連携を許可して、表示された PIN コードを入力してください。
```
% sayaka --home
Please go to:
https://twitter.com/...

And input PIN code:
```

PIN を入力するとただちにストリームモードになります。
2回目以降は認証なしで起動するようになります。


使い方
---
sayaka ver 3.6 以降は REST API による仮復旧版です。
約1分半おきに home_timeline を取得し、表示します。
```
% sayaka --home
```
キーワード(ハッシュタグなど)検索は ver 3.6.0 では使えません。


主なコマンドライン引数
---
* `--black` … 黒背景用に、可能なら明るめの文字色セットを使用します。
	デフォルトでは背景色を自動判別しますが、
	ターミナルが対応していなかったりすると `--white` が選択されます。

* `--ciphers <ciphers>` 通信に使用する暗号化スイートを指定します。
	今のところ指定できるのは "RSA" (大文字) のみです。
	2桁MHz級の遅マシンでコネクションがタイムアウトするようなら指定してみてください。
	このオプションは REST API に適用され、
	画像のダウンロードなどには適用されません。

* `--color <n>` … 色数を指定します。デフォルトは 256色です。
	他はたぶん 16 と 2 (と 8?) くらいを想定しています。

* `--eucjp` … 文字コードを EUC-JP に変換して出力します。
	VT382J 等の EUC-JP (DEC漢字) に対応したターミナルで使えます。

* ~~`--filter <keyword>` … キーワードを明示的に指定します。
	ハイフンから始まるキーワードがオプションと間違われないようにする場合に
	使います。~~

* `--font <W>x<H>` … フォントの幅と高さを `--font 7x14` のように指定します。
	デフォルトではターミナルに問い合わせて取得しますが、
	ターミナルが対応してない場合などは勝手に 7x14 としますので、
	もし違う場合はこの `--font` オプションを使って指定してください。
	アイコンと画像はここで指定したフォントサイズに連動した大きさで表示されます。

* `--force-sixel` … SIXEL 画像出力を(強制的に)オンにします。
	このオプションを指定しなくても、ターミナルが SIXEL 対応であることが
	判別できれば自動的に画像出力は有効になります。
	ターミナルが SIXEL を扱えるにも関わらずそれが判別できなかった場合などに
	指定してください。

* `--full-url` … URL が省略形になる場合でも元の URL を表示します。

* `--jis` … 文字コードを JIS に変換して出力します。
	NetBSD/x68k コンソール等の JIS に対応したターミナルで使えます。

* `--mathalpha` … Unicode の [Mathematical Alphanumeric Symbols](https://en.wikipedia.org/wiki/Mathematical_Alphanumeric_Symbols)
	を全角英数字に変換します。
	お使いのフォントが Mathematical Alphanumeric Symbols に対応しておらず
	全角英数字なら表示できる人を救済するためです。

* `--max-image-cols <n>` … 1行に表示する画像の最大数です。
	デフォルトは 0 で、この場合ターミナル幅を超えない限り横に並べて表示します。
	ターミナル幅、フォント幅が取得できないときは 1 として動作します。

* `--no-color` … テキストをすべて(色を含む)属性なしで出力します。
	`--color` オプションの結果が致命的に残念だった場合の救済用です。

* `--no-image` … SIXEL 画像出力を強制的にオフにします。
	このオプションを指定しなくても、ターミナルが SIXEL 非対応であることが
	判別できれば自動的に画像出力はオフになります。

* `--no-keycap` … `U+20E3 Combining Enclosing Keycap`
	文字を表示しません。この絵文字(前の文字をキーキャップで囲む)が
	正しく表示できない環境では読みやすくなるかも知れません。
	例えば U+0031 U+20E3 `&#x0031;&#x20e3;` を `1` にします。

* `--post` … 標準入力の内容をツイート(投稿)します。
	文字コードは UTF-8 にしてください。

* `--play` … ユーザストリームの代わりに標準入力の内容を再生します。

* `--progress` … 接続完了までの処理を表示します。
	遅マシン向けでしたが、
	フィルタストリーム廃止後の現在では、キャッシュ削除しかすることがないので
	あまり意味がないかも知れません。

* `--record <file>` / `--record-all <file>` …
	ストリームで受信した JSON のうち `--record-all` ならすべてを、
	`--record` なら概ね表示するもののみを `<file>` に記録します。
	いずれも `--play` コマンドで再生できます。

* `--timeout-image <msec>` … 画像取得のサーバへの接続タイムアウトを
	ミリ秒単位で設定します。
	0 を指定すると connect(2) のタイムアウト時間になります。
	デフォルトは 3000 (3秒)です。

* `--token <file>` … 認証トークンファイルを指定します。
	デフォルトは `token.json` です。
	`<file>` は `/` を含まなければ `~/.sayaka/` 直下のファイル、
	`/' を含むと現在のディレクトリからの相対パスか絶対パスになります。

* `--white` … 白背景用に、可能なら濃いめの文字色セットを使用します。
	デフォルトでは背景色を自動判別しますが、
	ターミナルが対応していなかったりすると選択されます。

* `--x68k` … NetBSD/x68k (SIXEL パッチ適用コンソール)
	のためのプリセットオプションで、
	実際には `--color x68k --font 8x16 --jis --black --progress --ormode on --palette off` と等価です。

その他のコマンドライン引数
---
* `-4`/`-6` … IPv4/IPv6 のみを使用します。

* `--eaw-a <n>` … Unicode の East Asian Width が Ambiguous な文字の
	文字幅を 1 か 2 で指定します。デフォルトは 1 です。
	というか通常 1 のはずです。
	ターミナルとフォントも幅が揃ってないとたぶん悲しい目にあいます。

* `--eaw-n <n>` … Unicode の East Asian Width が Neutral な文字の
	文字幅を 1 か 2 で指定します。デフォルトは 2 です。
	ターミナルとフォントも幅が揃ってないとたぶん悲しい目にあいます。

* `--max-cont <n>` … 同一ツイートに対するリツイートが連続した場合に
	表示を簡略化しますが、その上限数を指定します。デフォルトは 10 です。
	0 以下を指定すると簡略化を行いません(従来どおり)。

* `--ngword-add <ngword>` … NGワードを追加します。
	正規表現が使えます、というか `/<ngword>/i` みたいな感じで比較します。
	同時に `--ngword-user` オプションを指定すると、ユーザを限定できます。

* `--ngword-del <id>` … NGワードを削除します。
	`<id>` は `--ngword-list` で表示される1カラム目のインデックス番号を
	指定してください。

* `--ngword-list` … NGワード一覧を表示します。
	タブ区切りで、インデックス番号、NGワード、(あれば)ユーザ指定、です。

* `--ngword-user <user>` … NGワードに指定するユーザ情報です。
	`--ngword-add` オプションとともに使用します。
	`<user>` は `@<screen_name>` か `id:<user_id>` で指定します。

* `--ormode <on|off>` … on なら SIXEL を独自実装の OR モードで出力します。
	デフォルトは off です。
	ターミナル側も OR モードに対応している必要があります。

* `--palette <on|off>` … on なら SIXEL 画像にパレット定義情報を出力します。
	デフォルトは on です。
	NetBSD/x68k SIXEL 対応パッチのあててある俺様カーネルでは、
	SIXEL 画像内のパレット定義を参照しないため、off にすると少しだけ
	高速になります。
	それ以外の環境では on のまま使用してください。


.
---
[@isaki68k](https://twitter.com/isaki68k/)  
[差入れ](https://www.amazon.co.jp/hz/wishlist/ls/3TXVBRKSKTF31)してもらえると喜びます。
