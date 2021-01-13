//
// ChunkedInputStream
//

#include "ChunkedInputStream.h"
#include <memory>

// コンストラクタ
ChunkedInputStream::ChunkedInputStream(InputStream *src_, Diag& diag_)
	: diag(diag_)
{
	src = src_;

	// ここでは CRLF はデフォルト
}

// デストラクタ
ChunkedInputStream::~ChunkedInputStream()
{
}

ssize_t
ChunkedInputStream::Read(char *dst, size_t dstsize)
{
	diag.Debug("Read(%zd)", dstsize);

	// 要求サイズに満たない間 src から1チャンクずつ読み込む
	for (;;) {
		std::string slen;
		bool rv;

		// chunksLength は内部バッファ長
		size_t chunksLength = Chunks.GetSize();
		diag.Debug("dstsize=%zd chunksLength=%zd", dstsize, chunksLength);
		if (chunksLength >= dstsize) {
			diag.Debug("Filled");
			break;
		} else {
			diag.Debug("Need to fill");
		}

		// 先頭行はチャンク長+CRLF
		rv = src->ReadLine(&slen);
		if (rv == false) {
			diag.Debug("ReadLine failed");
			return -1;
		}
		if (slen.empty()) {
			// EOF
			diag.Debug("src is EOF");
			break;
		}

		// チャンク長を取り出す
		char *end;
		errno = 0;
		auto intlen = strtol(slen.c_str(), &end, 16);
		if (end == slen.c_str()) {
			diag.Debug("Not a number: %s", slen.c_str());
			return -1;
		}
		if (*end != '\0' && *end != '\r' && *end != '\n') {
			diag.Debug("Trailing garbage: %s", slen.c_str());
			return -1;
		}
		if (errno) {
			diag.Debug("Out of range: %s", slen.c_str());
			return -1;
		}
		diag.Debug("intlen=%d", intlen);

		if (intlen == 0) {
			// データ終わり。CRLF を読み捨てる
			src->ReadLine();
			diag.Debug("This was the last chunk");
			break;
		}

		std::unique_ptr<char[]> bufp = std::make_unique<char[]>(intlen);
		ssize_t readlen = src->Read(bufp.get(), intlen);
		if (readlen < 0) {
			diag.Debug("Read failed");
			return -1;
		}
		diag.Debug("readlen=%zd", readlen);
		if (readlen != intlen) {
			diag.Debug("readlen=%zd intlen=%d", readlen, intlen);
			return -1;
		}

		// 内部バッファに追加
		Chunks.AddData(bufp.get(), intlen);

		// 最後の CRLF を読み捨てる
		src->ReadLine();
	}

	// dst に入るだけコピー
	auto copylen = Chunks.Read(dst, dstsize);
	diag.Debug("copylen=%d\n", copylen);

	// Chunks の作り直しは C++ では不要なはず

	return (ssize_t)copylen;
}

#if defined(SELFTEST)
#include "test.h"
void
test_ChunkedInputStream()
{
	Diag diag;

	printf("%s\n", __func__);

	// 空入力
	{
		MemoryInputStream src;
		ChunkedInputStream chunk(&src, diag);
		auto str = chunk.ReadLine();
		xp_eq("", str);
	}

	// 入力行あり
	{
		MemoryInputStream src;
		std::vector<uint8> data {
			'a','\r','\n',	// このチャンクのバイト数
			'0','1','2','3',	// 本文
			'4','5','6','7',
			'8','9',
			'\r','\n',		// 終端 CRLF
			'0','\n'		// このチャンクで終了 (LF のみの改行も許容したい)
		};
		src.AddData(data);
		ChunkedInputStream chunk(&src, diag);
		auto str = chunk.ReadLine();
		xp_eq("0123456789", str);
		str = chunk.ReadLine();
		xp_eq("", str);
	}

	// 複数チャンク
	{
		MemoryInputStream src;
		std::vector<uint8> data {
			'2', '\r', '\n',	// このチャンクのバイト数
			'a', '\r',			// 本文
			'\r', '\n',			// チャンク終端 CRLF

			'3', '\r', '\n',	// このチャンクのバイト数
			'\n','\r', '\n',	// 本文(2行目は空行)
			'\r', '\n',			// チャンク終端 CRLF

			'2', '\r', '\n',	// このチャンクのバイト数
			'b', 'c',			// 本文(3行目は改行なしで終端)
			'\r', '\n',			// チャンク終端 CRLF

			'0', '\r', '\n',	// このチャンクで終了
		};
		src.AddData(data);
		ChunkedInputStream chunk(&src, diag);
		// ReadLine() なので chunk 境界に関わらず行ずつ取り出している。
		// ついでに ReadLine(std::string*) のほうをテストする。
		std::string str;
		bool rv;
		// ReadLine() は読み込んだ行から改行を除いて返す。
		// 1行目 ("a\r\n")
		rv = chunk.ReadLine(&str);
		xp_eq(true, rv);
		xp_eq("a", str);

		// 2行目 ("\r\n")
		rv = chunk.ReadLine(&str);
		xp_eq(true, rv);
		xp_eq("", str);

		// 3行目 ("bc")。改行なしで終端すればそのまま返す
		rv = chunk.ReadLine(&str);
		xp_eq(true, rv);
		xp_eq("bc", str);

		// EOF
		rv = chunk.ReadLine(&str);
		xp_eq(false, rv);

		// EOF 後にもう一度読んでも EOF
		rv = chunk.ReadLine(&str);
		xp_eq(false, rv);
	}
}
#endif // SELFTEST
