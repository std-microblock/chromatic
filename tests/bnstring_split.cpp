#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include <codecvt>
#include <cstring>
#include <iostream>
#include <locale>
#include <string_view>
#include "../src/utils/BNString.hpp"

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    const std::wstring url = L"https://gitcode.net/qq_21551787/bncm-plugin-packed/-/raw/master/";
    const BNString config(std::wstring(url + L"\n" + url));
    auto lines = config.split(L"\n");
    check(lines == std::vector<std::wstring>{url, url}, "long URL contents");

    // Results must own their data even after a temporary source is destroyed.
    auto retained = BNString(std::wstring(url + L"\nshort")).split(L"\n");
    std::wstring reuse(4096, L'x');
    check(retained == std::vector<std::wstring>{url, L"short"}, "source lifetime");
    check(BNString(std::wstring(L"a\n\nb\n")).split(L"\n") ==
        std::vector<std::wstring>{L"a", L"", L"b", L""}, "empty fields");
    check(BNString(std::wstring(L"")).split(L"\n") ==
        std::vector<std::wstring>{L""}, "empty source");
    check(BNString(std::wstring(L"a::b")).split(L"::") ==
        std::vector<std::wstring>{L"a", L"b"}, "multi-character delimiter");
    bool rejected = false;
    try { (void)config.split(L""); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "empty delimiter must be rejected");
    std::cout << "PASS: BNString split ownership and edge cases\n";
}
