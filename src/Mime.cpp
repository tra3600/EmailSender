#include "Mime.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>

namespace mime {

namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string randomHex(size_t n) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) out += hex[rng() % 16];
    return out;
}
}  // namespace

std::string base64Encode(const std::string& in, bool wrap) {
    std::string out;
    out.reserve(in.size() * 4 / 3 + 8);
    size_t lineLen = 0;
    auto put = [&](char c) {
        if (wrap && lineLen == 76) {
            out += "\r\n";
            lineLen = 0;
        }
        out += c;
        ++lineLen;
    };
    size_t i = 0;
    while (i + 2 < in.size()) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        put(kB64[v >> 18]); put(kB64[(v >> 12) & 63]); put(kB64[(v >> 6) & 63]); put(kB64[v & 63]);
        i += 3;
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        put(kB64[v >> 18]); put(kB64[(v >> 12) & 63]); put('='); put('=');
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8;
        put(kB64[v >> 18]); put(kB64[(v >> 12) & 63]); put(kB64[(v >> 6) & 63]); put('=');
    }
    if (wrap && lineLen > 0) out += "\r\n";
    return out;
}

std::string base64Decode(const std::string& in) {
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        const char* p = (c == '=' || c == 0) ? nullptr : std::char_traits<char>::find(kB64, 64, c);
        if (!p) continue;
        acc = (acc << 6) | (uint32_t)(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((acc >> bits) & 0xFF);
        }
    }
    return out;
}

bool isAscii(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c < 128; });
}

bool hasControlChars(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

std::string encodeHeaderWord(const std::string& s) {
    if (isAscii(s) && !hasControlChars(s)) return s;
    std::string out;
    size_t pos = 0;
    const size_t kMaxChunk = 42;  // octets bruts par encoded-word (reste < 75 caractères)
    while (pos < s.size()) {
        size_t len = std::min(kMaxChunk, s.size() - pos);
        // ne jamais couper au milieu d'un caractère UTF-8
        while (pos + len < s.size() && len > 0 && ((unsigned char)s[pos + len] & 0xC0) == 0x80) --len;
        if (len == 0) len = std::min(kMaxChunk, s.size() - pos);
        if (!out.empty()) out += "\r\n ";
        out += "=?UTF-8?B?" + base64Encode(s.substr(pos, len), false) + "?=";
        pos += len;
    }
    return out;
}

std::string normalizeCrlf(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 20);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out += "\r\n";
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else if (s[i] == '\n') {
            out += "\r\n";
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string baseName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string guessContentType(const std::string& filename) {
    static const std::map<std::string, std::string> types = {
        {"txt", "text/plain"}, {"csv", "text/csv"}, {"html", "text/html"}, {"htm", "text/html"},
        {"css", "text/css"}, {"md", "text/markdown"}, {"ics", "text/calendar"}, {"vcf", "text/vcard"},
        {"xml", "application/xml"}, {"json", "application/json"}, {"pdf", "application/pdf"},
        {"zip", "application/zip"}, {"gz", "application/gzip"}, {"tar", "application/x-tar"},
        {"7z", "application/x-7z-compressed"}, {"rtf", "application/rtf"},
        {"doc", "application/msword"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xls", "application/vnd.ms-excel"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"ppt", "application/vnd.ms-powerpoint"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"odt", "application/vnd.oasis.opendocument.text"},
        {"ods", "application/vnd.oasis.opendocument.spreadsheet"},
        {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"},
        {"bmp", "image/bmp"}, {"svg", "image/svg+xml"}, {"webp", "image/webp"}, {"ico", "image/x-icon"},
        {"mp3", "audio/mpeg"}, {"wav", "audio/wav"}, {"ogg", "audio/ogg"},
        {"mp4", "video/mp4"}, {"mpeg", "video/mpeg"}, {"webm", "video/webm"},
    };
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    auto it = types.find(ext);
    return it == types.end() ? "application/octet-stream" : it->second;
}

std::string rfc5322Date(std::time_t t) {
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::tm tm{};
    localtime_r(&t, &tm);
    char zone[8];
    std::strftime(zone, sizeof zone, "%z", &tm);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s, %02d %s %04d %02d:%02d:%02d %s", days[tm.tm_wday], tm.tm_mday,
                  months[tm.tm_mon], tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec, zone);
    return buf;
}

std::string generateBoundary() { return "=_EmailSender_" + randomHex(24); }

std::string generateMessageId(const std::string& domain) {
    return "<" + randomHex(8) + "." + randomHex(16) + "@" + (domain.empty() ? "localhost" : domain) + ">";
}

}  // namespace mime
