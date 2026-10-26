#pragma once
#include <string>
#include <Windows.h>
#include <vector>
#include <stdexcept>
#include <oleauto.h>
#include <utility>
#include <filesystem>

class BNString : public std::wstring {
private:
	static std::string wstring_to_utf8(const BNString& str) {
		return std::filesystem::path((std::wstring)str).string();
	}

	static BNString gbk_to_wstring(const std::string& str) {
		auto GBK_LOCALE_NAME = ".936";
		std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> convert(
			new std::codecvt_byname<wchar_t, char, mbstate_t>(GBK_LOCALE_NAME));
		return convert.from_bytes(str);
	}

	std::string gbk_to_utf8_string(const std::string& str) {
		auto GBK_LOCALE_NAME = ".936";
		std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> convert(
			new std::codecvt_byname<wchar_t, char, mbstate_t>(GBK_LOCALE_NAME));
		std::wstring tmp_wstr = convert.from_bytes(str);

		std::wstring_convert<std::codecvt_utf8<wchar_t>> cv2;
		return cv2.to_bytes(tmp_wstr);
	}

	// https://stackoverflow.com/questions/8298081/convert-utf-8-to-ansi-in-c/35272822#35272822
	static std::string utf8_string_to_asni_string(const std::string& s) {
		BSTR bstrWide;
		char* pszAnsi;
		int nLength;
		const char* pszCode = s.c_str();

		nLength = MultiByteToWideChar(CP_UTF8, 0, pszCode, strlen(pszCode) + 1, nullptr, NULL);
		bstrWide = SysAllocStringLen(nullptr, nLength);

		MultiByteToWideChar(CP_UTF8, 0, pszCode, strlen(pszCode) + 1, bstrWide, nLength);

		nLength = WideCharToMultiByte(CP_ACP, 0, bstrWide, -1, nullptr, 0, nullptr, nullptr);
		pszAnsi = new char[nLength];

		WideCharToMultiByte(CP_ACP, 0, bstrWide, -1, pszAnsi, nLength, nullptr, nullptr);
		SysFreeString(bstrWide);

		std::string r(pszAnsi);
		delete[] pszAnsi;
		return r;
	}

	static std::string utf8_string_to_gbk_string(const std::string& str) {
		std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
		std::wstring tmp_wstr = conv.from_bytes(str);

		auto GBK_LOCALE_NAME = ".936";
		std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> convert(
			new std::codecvt_byname<wchar_t, char, mbstate_t>(GBK_LOCALE_NAME));
		return convert.to_bytes(tmp_wstr);
	}

	static BNString utf8_to_wstring(const std::string& utf8) {
		std::vector<unsigned long> unicode;
		size_t i = 0;
		while (i < utf8.size()) {
			unsigned long uni;
			size_t todo;
			bool error = false;
			unsigned char ch = utf8[i++];
			if (ch <= 0x7F) {
				uni = ch;
				todo = 0;
			}
			else if (ch <= 0xBF) {
				throw std::logic_error("not a UTF-8 string");
			}
			else if (ch <= 0xDF) {
				uni = ch & 0x1F;
				todo = 1;
			}
			else if (ch <= 0xEF) {
				uni = ch & 0x0F;
				todo = 2;
			}
			else if (ch <= 0xF7) {
				uni = ch & 0x07;
				todo = 3;
			}
			else {
				throw std::logic_error("not a UTF-8 string");
			}
			for (size_t j = 0; j < todo; ++j) {
				if (i == utf8.size())
					throw std::logic_error("not a UTF-8 string");
				unsigned char ch = utf8[i++];
				if (ch < 0x80 || ch > 0xBF)
					throw std::logic_error("not a UTF-8 string");
				uni <<= 6;
				uni += ch & 0x3F;
			}
			if (uni >= 0xD800 && uni <= 0xDFFF)
				throw std::logic_error("not a UTF-8 string");
			if (uni > 0x10FFFF)
				throw std::logic_error("not a UTF-8 string");
			unicode.push_back(uni);
		}
		std::wstring utf16;
		for (size_t i = 0; i < unicode.size(); ++i) {
			unsigned long uni = unicode[i];
			if (uni <= 0xFFFF) {
				utf16 += static_cast<wchar_t>(uni);
			}
			else {
				uni -= 0x10000;
				utf16 += static_cast<wchar_t>((uni >> 10) + 0xD800);
				utf16 += static_cast<wchar_t>((uni & 0x3FF) + 0xDC00);
			}
		}
		return utf16;
	}

public:
	static BNString fromGBK(const std::string& s) {
		return BNString(gbk_to_wstring(s));
	}

	BNString() : std::wstring() {
	}

	BNString(const std::string s) {
		*this = utf8_to_wstring(s);
	}

	BNString(const std::wstring_view s) {
		*this = BNString(std::wstring(s));
	}

	BNString(const char* s) {
		*this = BNString(std::string(s));
	}

	BNString(const std::wstring s) : std::wstring(s) {
	}


	// To UTF8 String
	operator const std::string() {
		return this->utf8();
	}

	operator const std::wstring() {
		return *this;
	}

	[[nodiscard]] const std::string toUtf8String() const {
		return wstring_to_utf8(*this);
	}

	[[nodiscard]] const std::string utf8() const {
		return this->toUtf8String();
	}

	[[nodiscard]] const std::string toANSIString() const {
		return utf8_string_to_asni_string(wstring_to_utf8(*this));
	}

	[[nodiscard]] const std::string ansi() const {
		return this->toANSIString();
	}

	[[nodiscard]] const std::string toGBKString() const {
		return utf8_string_to_gbk_string(this->toUtf8String());
	}

	[[nodiscard]] const std::string gbk() const {
		return this->toGBKString();
	}


	[[nodiscard]] bool startsWith(const std::wstring& prefix) const {
		if (prefix.length() > this->length()) return false;
		return this->substr(0, prefix.length()) == prefix;
	}

	[[nodiscard]] bool endsWith(const std::wstring& suffix) const {
		if (suffix.length() > this->length()) return false;
		return this->substr(this->length() - suffix.length()) == suffix;
	}

	[[nodiscard]] bool includes(const std::wstring& search) const {
		return this->find(search) != std::wstring::npos;
	}

	[[nodiscard]] int indexOf(const std::wstring& search) const {
		size_t index = this->find(search);
		if (index == std::wstring::npos) return -1;
		return static_cast<int>(index);
	}

	[[nodiscard]] std::vector<std::wstring> split(const std::wstring& delimiter) const {
		if (delimiter.empty()) {
			throw std::invalid_argument("split delimiter must not be empty");
		}

		std::vector<std::wstring> result;
		size_t start = 0;
		size_t end = 0;
		while ((end = this->find(delimiter, start)) != std::wstring::npos) {
			result.push_back(this->substr(start, end - start));
			start = end + delimiter.length();
		}
		result.push_back(this->substr(start));
		return result;
	}

	BNString& replace(const std::wstring& search, const std::wstring& replacement) {
		size_t index = 0;
		while ((index = this->find(search, index)) != std::wstring::npos) {
			this->erase(index, search.length());
			this->insert(index, replacement);
			index += replacement.length();
		}
		return *this;
	}
};
