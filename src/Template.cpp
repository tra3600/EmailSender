#include "Template.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

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
}  // namespace

std::string htmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string renderTemplate(const std::string& tpl, const Vars& vars, bool escapeHtml,
                           std::vector<std::string>* missing) {
    std::string out;
    size_t i = 0;
    while (i < tpl.size()) {
        size_t open = tpl.find("{{", i);
        if (open == std::string::npos) {
            out += tpl.substr(i);
            break;
        }
        size_t close = tpl.find("}}", open + 2);
        if (close == std::string::npos) {
            out += tpl.substr(i);
            break;
        }
        out += tpl.substr(i, open - i);
        std::string expr = tpl.substr(open + 2, close - open - 2);
        std::string key = expr, fallback;
        bool hasFallback = false;
        size_t bar = expr.find('|');
        if (bar != std::string::npos) {
            key = expr.substr(0, bar);
            fallback = trim(expr.substr(bar + 1));
            hasFallback = true;
        }
        key = trim(key);
        auto it = vars.find(key);
        std::string value;
        if (it != vars.end() && !it->second.empty()) value = it->second;
        else if (hasFallback) value = fallback;
        else if (it == vars.end() && missing && std::find(missing->begin(), missing->end(), key) == missing->end())
            missing->push_back(key);
        out += escapeHtml ? htmlEscape(value) : value;
        i = close + 2;
    }
    return out;
}

std::vector<std::vector<std::string>> parseCsv(const std::string& textIn) {
    std::string text = textIn;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    // détection du séparateur sur la première ligne (hors guillemets)
    size_t commas = 0, semis = 0;
    bool q = false;
    for (char c : text) {
        if (c == '"') q = !q;
        else if (!q && (c == '\n' || c == '\r')) break;
        else if (!q && c == ',') ++commas;
        else if (!q && c == ';') ++semis;
    }
    char delim = semis > commas ? ';' : ',';

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false, any = false;
    auto endField = [&] { row.push_back(field); field.clear(); };
    auto endRow = [&] {
        endField();
        bool blank = row.size() == 1 && trim(row[0]).empty();
        if (!blank) rows.push_back(row);
        row.clear();
        any = false;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { field += '"'; ++i; }
                else inQuotes = false;
            } else field += c;
        } else if (c == '"') { inQuotes = true; any = true; }
        else if (c == delim) { endField(); any = true; }
        else if (c == '\r') { if (i + 1 < text.size() && text[i + 1] == '\n') ++i; endRow(); }
        else if (c == '\n') endRow();
        else { field += c; any = true; }
    }
    if (any || !field.empty() || !row.empty()) endRow();
    return rows;
}

bool loadRecipientsCsv(const std::string& path, std::vector<Recipient>& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "Impossible d'ouvrir le CSV : " + path; return false; }
    std::ostringstream ss;
    ss << f.rdbuf();
    auto rows = parseCsv(ss.str());
    if (rows.size() < 2) { error = "CSV vide ou sans ligne de données : " + path; return false; }

    std::vector<std::string> cols;
    for (const auto& h : rows[0]) cols.push_back(trim(h));
    int emailCol = -1, nameCol = -1;
    for (size_t i = 0; i < cols.size(); ++i) {
        std::string l = lower(cols[i]);
        if (emailCol < 0 && (l == "email" || l == "e-mail" || l == "mail" || l == "to" || l == "adresse")) emailCol = (int)i;
        if (nameCol < 0 && (l == "name" || l == "nom")) nameCol = (int)i;
    }
    if (emailCol < 0) { error = "Colonne 'email' introuvable dans l'en-tête du CSV"; return false; }

    for (size_t r = 1; r < rows.size(); ++r) {
        Recipient rc;
        for (size_t i = 0; i < cols.size() && i < rows[r].size(); ++i) rc.vars[cols[i]] = trim(rows[r][i]);
        std::string e;
        std::string addr = (size_t)emailCol < rows[r].size() ? trim(rows[r][emailCol]) : "";
        if (!Address::parse(addr, rc.address, &e)) {
            error = "Ligne " + std::to_string(r + 1) + " : " + e;
            return false;
        }
        if (nameCol >= 0 && (size_t)nameCol < rows[r].size() && rc.address.name.empty())
            rc.address.name = trim(rows[r][nameCol]);
        rc.vars["email"] = rc.address.email;
        out.push_back(std::move(rc));
    }
    return true;
}
