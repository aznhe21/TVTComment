#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <memory>
#include <regex>
#include <tchar.h>
#include <share.h>
#include "../ImportLogUtil.h"

namespace
{
// 最終行のchatタグのdate属性値を読み込む
unsigned int ReadLastChatDate(LPCTSTR path)
{
	std::unique_ptr<FILE, decltype(&fclose)> fp(_tfsopen(path, TEXT("rb"), _SH_DENYNO), fclose);
	if (!fp) {
		return 0;
	}
	setvbuf(fp.get(), nullptr, _IONBF, 0);

	char text[8192];
	// バイナリモードでのSEEK_ENDは厳密には議論あるが、Windowsでは問題ない
	if (_fseeki64(fp.get(), 0, SEEK_END) != 0 ||
	    _fseeki64(fp.get(), -std::min<LONGLONG>(sizeof(text) - 1, _ftelli64(fp.get())), SEEK_END) != 0) {
		return 0;
	}
	size_t readLen = fread(text, 1, sizeof(text) - 1, fp.get());
	text[readLen] = '\0';
	size_t textPos = strlen(text);
	if (textPos >= 1 && text[textPos - 1] == '\n') {
		text[--textPos] = '\0';
	}
	if (textPos >= 1 && text[textPos - 1] == '\r') {
		text[--textPos] = '\0';
	}
	for (; textPos > 0; --textPos) {
		if (text[textPos - 1] == '\n') {
			break;
		}
	}

	const std::regex re("^<chat(?= )[^>]*? date=\"(\\d+)\"");
	std::cmatch m;
	if (std::regex_search(text + textPos, m, re)) {
		return strtoul(m[1].first, nullptr, 10);
	}
	return 0;
}

void UnixTimeToLocalSystemTime(unsigned int tm, SYSTEMTIME &st)
{
	LONGLONG ll = tm * 10000000LL + 116444736000000000;
	FILETIME ft, ftUtc;
	ftUtc.dwLowDateTime = static_cast<DWORD>(ll);
	ftUtc.dwHighDateTime = static_cast<DWORD>(ll >> 32);
	if (!FileTimeToLocalFileTime(&ftUtc, &ft) || !FileTimeToSystemTime(&ft, &st)) {
		SYSTEMTIME stZero = {};
		st = stZero;
	}
}
}

int _tmain(int argc, TCHAR **argv)
{
	if (argc != 2) {
		_tprintf(TEXT("Usage: jkimlog src_path.\n"));
		return 2;
	}
	LPCTSTR srcPath = argv[1];

	// 入力ファイル名からjkIDを抽出する
	unsigned int jkID = 0;
	for (size_t i = _tcslen(srcPath); i > 0 && !_tcschr(TEXT("/\\"), srcPath[i - 1]); ) {
		--i;
		if (!_tcsnicmp(srcPath + i, TEXT("jk"), 2)) {
			jkID = _tcstoul(srcPath + i + 2, nullptr, 10);
			if (jkID != 0) {
				break;
			}
		}
	}
	if (jkID == 0) {
		_tprintf(TEXT("Error: Cannot determine jkID from src_path.\n"));
		return 1;
	}

	TCHAR destRoot[MAX_PATH];
	DWORD nRet = GetModuleFileName(nullptr, destRoot, _countof(destRoot));
	if (nRet && nRet < _countof(destRoot)) {
		for (size_t i = _tcslen(destRoot); i > 0 && !_tcschr(TEXT("/\\"), destRoot[i - 1]); ) {
			destRoot[--i] = TEXT('\0');
		}
	} else {
		destRoot[0] = TEXT('\0');
	}
	if (!destRoot[0]) {
		_tprintf(TEXT("Error: Unexpected.\n"));
		return 1;
	}

	std::unique_ptr<FILE, decltype(&fclose)> fpDest(nullptr, fclose);
	bool bFirst = true;
	bool bCreated = false;
	bool bTrimmed = false;
	unsigned int tmLast = 0;
	// これ以上のchatタグから書き込む(tmMax==0なら書き込まない)
	unsigned int tmMax = 0;
	// これ未満のchatタグまで書き込む
	unsigned int tmMin = 0;

	ImportLogfile(srcPath, [&, jkID](unsigned int &tm) -> FILE * {
		if (bFirst) {
			// jkIDのログファイル一覧を得る
			TCHAR pattern[_countof(destRoot) + 32];
			_stprintf_s(pattern, TEXT("%sjk%d\\??????????.txt"), destRoot, jkID);
			// tm以前で最大と、tmより後で最小のログファイルを探す
			TCHAR target[16];
			_stprintf_s(target, TEXT("%010u.txt"), tm);
			TCHAR maxTxt[16] = {};
			TCHAR minTxt[16] = {};
			WIN32_FIND_DATA fd;
			HANDLE hFind = FindFirstFile(pattern, &fd);
			if (hFind != INVALID_HANDLE_VALUE) {
				do {
					if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
					    _tcslen(fd.cFileName) == 14 &&
					    !_tcsicmp(fd.cFileName + 10, TEXT(".txt"))) {
						// テキスト形式のログ
						if (_tcscmp(fd.cFileName, target) <= 0) {
							if (_tcscmp(fd.cFileName, maxTxt) > 0) {
								_tcscpy_s(maxTxt, fd.cFileName);
							}
						} else {
							if (!minTxt[0] || _tcscmp(fd.cFileName, minTxt) < 0) {
								_tcscpy_s(minTxt, fd.cFileName);
							}
						}
					}
				} while (FindNextFile(hFind, &fd));
				FindClose(hFind);
			}

			tmMax = _tcstoul(maxTxt, nullptr, 10);
			if (tmMax == 0 || tmMax > tm) {
				// 先頭から書き込む
				tmMax = tm;
			} else {
				TCHAR path[_countof(destRoot) + 32];
				_stprintf_s(path, TEXT("%sjk%d\\%s"), destRoot, jkID, maxTxt);
				tmMax = ReadLastChatDate(path);
				if (tmMax != 0) {
					++tmMax;
				}
			}
			tmMin = _tcstoul(minTxt, nullptr, 10);
			if (tmMin == 0) {
				// 末尾まで書き込む
				tmMin = UINT_MAX;
			}
		}

		if (tmMax != 0 && tm >= tmMax && tm < tmMin) {
			if (!bCreated) {
				// jkフォルダがなければ作る
				TCHAR path[_countof(destRoot) + 32];
				_stprintf_s(path, TEXT("%sjk%d"), destRoot, jkID);
				if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) {
					_tprintf(TEXT("Creating jk%d\n"), jkID);
					CreateDirectory(path, nullptr);
				}
				_tprintf(TEXT("Creating jk%d\\%010u.txt\n"), jkID, tm);
				_stprintf_s(path, TEXT("%sjk%d\\%010u.txt"), destRoot, jkID, tm);
				FILE *fp;
				if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES &&
				    !_tfopen_s(&fp, path, TEXT("w"))) {
					fpDest.reset(fp);

					SYSTEMTIME st;
					UnixTimeToLocalSystemTime(tm, st);
					fprintf(fpDest.get(), "<!-- jkimlog imported logfile from %04d-%02d-%02dT%02d:%02d:%02d -->\n",
					        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
					_tprintf(TEXT("Sta date=\"%u\" [%04d-%02d-%02dT%02d:%02d:%02d]%s\n"),
					         tm, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
					         bFirst ? TEXT("") : TEXT(" (trimmed)"));
				}
				bCreated = true;
			}
		} else {
			if (fpDest) {
				fpDest.reset();
				bTrimmed = true;
			}
		}
		bFirst = false;

		if (fpDest) {
			tmLast = tm;
		}
		return fpDest.get();
	});

	if (!bCreated) {
		_tprintf(TEXT("Warning: Nothing to write, or duplicated log. Ignored.\n"));
		return 3;
	}
	if (!fpDest && !bTrimmed) {
		_tprintf(TEXT("Error: Cannot write to destination file.\n"));
		return 1;
	}
	fpDest.reset();

	SYSTEMTIME st;
	UnixTimeToLocalSystemTime(tmLast, st);
	_tprintf(TEXT("End date=\"%u\" [%04d-%02d-%02dT%02d:%02d:%02d]%s\n"),
	         tmLast, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
	         bTrimmed ? TEXT(" (trimmed)") : TEXT(""));
	_tprintf(TEXT("Done.\n"));
	return 0;
}
