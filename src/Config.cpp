#include "Config.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
bool parseBool(const std::string& v, bool& out) {
    std::string l = lower(v);
    if (l == "1" || l == "true" || l == "yes" || l == "oui" || l == "on") { out = true; return true; }
    if (l == "0" || l == "false" || l == "no" || l == "non" || l == "off") { out = false; return true; }
    return false;
}
bool parseLong(const std::string& v, long& out, long min, long max) {
    try {
        size_t pos = 0;
        long n = std::stol(v, &pos);
        if (pos != v.size() || n < min || n > max) return false;
        out = n;
        return true;
    } catch (...) {
        return false;
    }
}
bool exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}
}  // namespace

bool applySetting(Settings& s, const std::string& rawKey, const std::string& value, std::string& error) {
    std::string key = lower(rawKey);
    long n = 0;
    bool b = false;
    auto badNumber = [&](const char* what) { error = std::string("valeur invalide pour ") + what + " : " + value; return false; };
    if (key == "server" || key == "host") s.smtp.server = value;
    else if (key == "port") { if (!parseLong(value, n, 1, 65535)) return badNumber("port"); s.smtp.port = (int)n; }
    else if (key == "username" || key == "user") s.smtp.username = value;
    else if (key == "password") s.smtp.password = value;
    else if (key == "security") { if (!parseSecurity(value, s.smtp.security)) { error = "security : auto|none|starttls|ssl attendu, reçu " + value; return false; } }
    else if (key == "helo") s.smtp.heloDomain = value;
    else if (key == "timeout") { if (!parseLong(value, n, 1, 86400)) return badNumber("timeout"); s.smtp.timeoutSec = n; }
    else if (key == "connect_timeout") { if (!parseLong(value, n, 1, 3600)) return badNumber("connect_timeout"); s.smtp.connectTimeoutSec = n; }
    else if (key == "retries") { if (!parseLong(value, n, 0, 20)) return badNumber("retries"); s.smtp.maxRetries = (int)n; }
    else if (key == "retry_delay_ms") { if (!parseLong(value, n, 0, 600000)) return badNumber("retry_delay_ms"); s.smtp.retryDelayMs = n; }
    else if (key == "verify_tls") { if (!parseBool(value, b)) return badNumber("verify_tls"); s.smtp.verifyPeer = s.smtp.verifyHost = b; }
    else if (key == "ca_bundle") s.smtp.caBundle = value;
    else if (key == "from") { if (!Address::parse(value, s.from, &error)) return false; }
    else if (key == "from_name") s.from.name = value;
    else if (key == "delay_ms") { if (!parseLong(value, n, 0, 3600000)) return badNumber("delay_ms"); s.delayMs = n; }
    else { error = "clé inconnue : " + rawKey; return false; }
    return true;
}

bool loadConfigFile(const std::string& path, Settings& s, std::string& error) {
    std::ifstream f(path);
    if (!f) { error = "Impossible d'ouvrir le fichier de configuration : " + path; return false; }
    std::string line;
    int no = 0;
    bool hasPassword = false;
    while (std::getline(f, line)) {
        ++no;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) { error = path + ":" + std::to_string(no) + " : format 'clé = valeur' attendu"; return false; }
        std::string key = trim(t.substr(0, eq)), val = trim(t.substr(eq + 1));
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\'')))
            val = val.substr(1, val.size() - 2);
        std::string e;
        if (!applySetting(s, key, val, e)) { error = path + ":" + std::to_string(no) + " : " + e; return false; }
        if (lower(key) == "password") hasPassword = true;
    }
    struct stat st;
    if (hasPassword && ::stat(path.c_str(), &st) == 0 && (st.st_mode & (S_IRWXG | S_IRWXO)))
        std::cerr << "Avertissement : " << path << " contient un mot de passe et est lisible par d'autres "
                  << "utilisateurs (chmod 600 recommandé)." << std::endl;
    return true;
}

bool applyEnvironment(Settings& s, std::string& error) {
    static const std::pair<const char*, const char*> map[] = {
        {"EMAILSENDER_SERVER", "server"}, {"EMAILSENDER_PORT", "port"},
        {"EMAILSENDER_USER", "username"}, {"EMAILSENDER_PASSWORD", "password"},
        {"EMAILSENDER_SECURITY", "security"}, {"EMAILSENDER_FROM", "from"},
        {"EMAILSENDER_FROM_NAME", "from_name"}, {"EMAILSENDER_HELO", "helo"},
        {"EMAILSENDER_TIMEOUT", "timeout"}, {"EMAILSENDER_RETRIES", "retries"},
        {"EMAILSENDER_CA_BUNDLE", "ca_bundle"},
    };
    for (const auto& m : map) {
        const char* v = std::getenv(m.first);
        if (v && *v) {
            std::string e;
            if (!applySetting(s, m.second, v, e)) { error = std::string(m.first) + " : " + e; return false; }
        }
    }
    return true;
}

std::string defaultConfigPath() {
    if (const char* p = std::getenv("EMAILSENDER_CONFIG"); p && *p) return p;
    if (exists("emailsender.conf")) return "emailsender.conf";
    if (const char* home = std::getenv("HOME")) {
        std::string p = std::string(home) + "/.config/emailsender.conf";
        if (exists(p)) return p;
    }
    return "";
}
