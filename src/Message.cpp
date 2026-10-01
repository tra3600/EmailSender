#include "Message.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "Mime.h"

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

// Insère des retours à la ligne repliés (RFC 5322 §2.2.3) pour tenir dans ~78 colonnes.
std::string foldHeader(const std::string& name, const std::string& value) {
    std::string out = name + ":";
    size_t lineLen = out.size();
    size_t i = 0;
    while (i < value.size()) {
        size_t j = i;
        while (j < value.size() && value[j] != ' ') ++j;
        std::string token = value.substr(i, j - i);
        bool hasBreak = token.find('\n') != std::string::npos;
        if (lineLen + 1 + token.size() > 78 && lineLen > 1 && !hasBreak) {
            out += "\r\n ";
            lineLen = 1;
        } else {
            out += ' ';
            lineLen += 1;
        }
        out += token;
        size_t nl = token.rfind('\n');
        lineLen = nl == std::string::npos ? lineLen + token.size() : token.size() - nl;
        i = j + 1;
    }
    out += "\r\n";
    return out;
}

void checkHeaderValue(const std::string& what, const std::string& v) {
    if (v.find('\r') != std::string::npos || v.find('\n') != std::string::npos)
        throw std::invalid_argument(what + " : retour à la ligne interdit (injection d'en-tête)");
}

bool validHeaderName(const std::string& n) {
    if (n.empty()) return false;
    for (unsigned char c : n)
        if (c <= 32 || c >= 127 || c == ':') return false;
    return true;
}

std::string formatList(const std::vector<Address>& list) {
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) out += (i ? ", " : "") + list[i].format();
    return out;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Impossible de lire la pièce jointe : " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool needsBase64(const std::string& s) {
    if (!mime::isAscii(s)) return true;
    size_t run = 0;
    for (char c : s) {
        if (c == '\n') run = 0;
        else if (++run > 900) return true;
    }
    return false;
}

std::string encodedBody(const std::string& body, std::string& cte) {
    std::string crlf = mime::normalizeCrlf(body);
    if (needsBase64(crlf)) {
        cte = "base64";
        return mime::base64Encode(crlf, true);
    }
    cte = "7bit";
    if (crlf.size() < 2 || crlf.compare(crlf.size() - 2, 2, "\r\n") != 0) crlf += "\r\n";
    return crlf;
}

std::string textPart(const std::string& subtype, const std::string& body) {
    std::string cte;
    std::string enc = encodedBody(body, cte);
    return "Content-Type: text/" + subtype + "; charset=UTF-8\r\n"
           "Content-Transfer-Encoding: " + cte + "\r\n\r\n" + enc;
}

std::string quoteParam(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string attachmentPart(const Attachment& a) {
    std::string content = a.data.empty() ? readFile(a.path) : a.data;
    std::string filename = a.filename.empty() ? mime::baseName(a.path) : a.filename;
    if (filename.empty()) filename = "attachment";
    checkHeaderValue("nom de pièce jointe", filename);
    std::string type = a.contentType.empty() ? mime::guessContentType(filename) : a.contentType;
    checkHeaderValue("Content-Type", type);
    std::string fname = quoteParam(mime::encodeHeaderWord(filename));
    std::string out = "Content-Type: " + type + "; name=" + fname + "\r\n"
                      "Content-Transfer-Encoding: base64\r\n";
    if (a.contentId.empty()) {
        out += "Content-Disposition: attachment; filename=" + fname + "\r\n";
    } else {
        checkHeaderValue("Content-ID", a.contentId);
        out += "Content-ID: <" + a.contentId + ">\r\n"
               "Content-Disposition: inline; filename=" + fname + "\r\n";
    }
    return out + "\r\n" + mime::base64Encode(content, true);
}

std::string multipart(const std::string& subtype, const std::vector<std::string>& parts, std::string& contentType) {
    std::string boundary = mime::generateBoundary();
    contentType = "multipart/" + subtype + "; boundary=\"" + boundary + "\"";
    std::string out;
    for (const auto& p : parts) out += "--" + boundary + "\r\n" + p + "\r\n";
    out += "--" + boundary + "--\r\n";
    return out;
}

// Représente une entité MIME : en-têtes Content-* + corps.
struct Entity {
    std::string headers;  // lignes se terminant par CRLF
    std::string body;
    std::string whole() const { return headers + "\r\n" + body; }
};

Entity wrapMultipart(const std::string& subtype, const std::vector<std::string>& parts) {
    std::string ct;
    Entity e;
    e.body = multipart(subtype, parts, ct);
    e.headers = "Content-Type: " + ct + "\r\n";
    return e;
}

Entity fromPart(const std::string& part) {
    size_t p = part.find("\r\n\r\n");
    return {part.substr(0, p + 2), part.substr(p + 4)};
}

}  // namespace

// ---------------------------------------------------------------- Address

bool Address::isValidEmail(const std::string& e) {
    if (e.empty() || e.size() > 254 || e.find_first_of(" \t\r\n<>,;\"()[]\\") != std::string::npos) return false;
    size_t at = e.find('@');
    if (at == std::string::npos || at != e.rfind('@') || at == 0 || at + 1 >= e.size()) return false;
    std::string domain = e.substr(at + 1);
    if (at > 64 || domain.front() == '.' || domain.back() == '.' || domain.find("..") != std::string::npos)
        return false;
    return true;
}

bool Address::parse(const std::string& text, Address& out, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    std::string t = trim(text);
    if (t.empty()) return fail("adresse vide");
    if (mime::hasControlChars(t)) return fail("caractères de contrôle dans l'adresse : " + t);
    Address a;
    size_t lt = t.rfind('<');
    if (lt != std::string::npos) {
        size_t gt = t.find('>', lt);
        if (gt == std::string::npos) return fail("'>' manquant : " + t);
        a.email = trim(t.substr(lt + 1, gt - lt - 1));
        std::string name = trim(t.substr(0, lt));
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
            std::string unq;
            for (size_t i = 1; i + 1 < name.size(); ++i) {
                if (name[i] == '\\' && i + 2 < name.size()) ++i;
                unq += name[i];
            }
            name = unq;
        }
        a.name = name;
    } else {
        a.email = t;
    }
    if (!isValidEmail(a.email)) return fail("adresse e-mail invalide : " + a.email);
    out = a;
    return true;
}

bool Address::parseList(const std::string& text, std::vector<Address>& out, std::string* error) {
    std::string cur;
    bool quoted = false, angle = false;
    std::vector<std::string> items;
    for (char c : text) {
        if (c == '"') quoted = !quoted;
        else if (!quoted && c == '<') angle = true;
        else if (!quoted && c == '>') angle = false;
        if (!quoted && !angle && (c == ',' || c == ';')) {
            items.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    items.push_back(cur);
    for (const auto& item : items) {
        if (trim(item).empty()) continue;
        Address a;
        if (!parse(item, a, error)) return false;
        out.push_back(a);
    }
    return true;
}

std::string Address::format() const {
    if (name.empty()) return email;
    std::string display;
    if (!mime::isAscii(name)) {
        display = mime::encodeHeaderWord(name);
    } else if (name.find_first_of("()<>[]:;@\\,.\"") != std::string::npos) {
        display = quoteParam(name);
    } else {
        display = name;
    }
    return display + " <" + email + ">";
}

// ------------------------------------------------------------ buildMessage

BuiltMessage buildMessage(const Email& m) {
    if (m.from.empty()) throw std::invalid_argument("Expéditeur (From) manquant");
    if (!Address::isValidEmail(m.from.email)) throw std::invalid_argument("Expéditeur invalide : " + m.from.email);
    if (m.to.empty() && m.cc.empty() && m.bcc.empty()) throw std::invalid_argument("Aucun destinataire");
    if (m.textBody.empty() && m.htmlBody.empty() && m.attachments.empty())
        throw std::invalid_argument("Message vide : fournissez un corps texte, HTML ou une pièce jointe");
    checkHeaderValue("Objet", m.subject);
    checkHeaderValue("In-Reply-To", m.inReplyTo);
    checkHeaderValue("References", m.references);
    checkHeaderValue("Return-Path", m.returnPath);

    BuiltMessage out;
    std::set<std::string> seen;
    for (const auto* list : {&m.to, &m.cc, &m.bcc})
        for (const auto& a : *list) {
            if (!Address::isValidEmail(a.email)) throw std::invalid_argument("Destinataire invalide : " + a.email);
            if (seen.insert(lower(a.email)).second) out.recipients.push_back(a.email);
        }
    out.envelopeFrom = m.returnPath.empty() ? m.from.email : m.returnPath;

    std::string domain = m.from.email.substr(m.from.email.find('@') + 1);
    out.messageId = mime::generateMessageId(domain);

    std::string hdr;
    hdr += foldHeader("Date", mime::rfc5322Date(m.date ? m.date : std::time(nullptr)));
    hdr += foldHeader("From", m.from.format());
    if (!m.sender.empty()) hdr += foldHeader("Sender", m.sender.format());
    if (!m.replyTo.empty()) hdr += foldHeader("Reply-To", m.replyTo.format());
    if (!m.to.empty()) hdr += foldHeader("To", formatList(m.to));
    if (!m.cc.empty()) hdr += foldHeader("Cc", formatList(m.cc));
    // Bcc volontairement absent des en-têtes ; les destinataires cachés ne figurent que dans l'enveloppe.
    if (m.to.empty() && m.cc.empty()) hdr += foldHeader("To", "undisclosed-recipients:;");
    hdr += foldHeader("Subject", mime::encodeHeaderWord(m.subject));
    hdr += foldHeader("Message-ID", out.messageId);
    if (!m.inReplyTo.empty()) hdr += foldHeader("In-Reply-To", m.inReplyTo);
    if (!m.references.empty()) hdr += foldHeader("References", m.references);
    hdr += "MIME-Version: 1.0\r\n";
    hdr += "X-Mailer: EmailSender\r\n";
    switch (m.priority) {
        case Priority::High:
            hdr += "X-Priority: 1 (Highest)\r\nImportance: High\r\nPriority: urgent\r\n";
            break;
        case Priority::Low:
            hdr += "X-Priority: 5 (Lowest)\r\nImportance: Low\r\nPriority: non-urgent\r\n";
            break;
        default:
            break;
    }
    if (m.requestReadReceipt) {
        Address receipt = m.replyTo.empty() ? m.from : m.replyTo;
        hdr += foldHeader("Disposition-Notification-To", receipt.format());
    }
    static const std::set<std::string> reserved = {"date", "from", "to", "cc", "bcc", "subject", "message-id",
                                                    "mime-version", "content-type", "content-transfer-encoding"};
    for (const auto& h : m.headers) {
        if (!validHeaderName(h.first)) throw std::invalid_argument("Nom d'en-tête invalide : " + h.first);
        if (reserved.count(lower(h.first))) throw std::invalid_argument("En-tête réservé : " + h.first);
        checkHeaderValue("En-tête " + h.first, h.second);
        hdr += foldHeader(h.first, mime::encodeHeaderWord(h.second));
    }

    // Corps : text | html | alternative, puis related (inline), puis mixed (pièces jointes).
    Entity body;
    if (!m.textBody.empty() && !m.htmlBody.empty())
        body = wrapMultipart("alternative", {textPart("plain", m.textBody), textPart("html", m.htmlBody)});
    else if (!m.htmlBody.empty())
        body = fromPart(textPart("html", m.htmlBody));
    else if (!m.textBody.empty())
        body = fromPart(textPart("plain", m.textBody));

    std::vector<std::string> inlineParts, regularParts;
    for (const auto& a : m.attachments) (a.contentId.empty() ? regularParts : inlineParts).push_back(attachmentPart(a));

    bool haveBody = !body.headers.empty();
    if (!inlineParts.empty() && haveBody) {
        std::vector<std::string> parts = {body.whole()};
        parts.insert(parts.end(), inlineParts.begin(), inlineParts.end());
        body = wrapMultipart("related", parts);
    } else {
        regularParts.insert(regularParts.end(), inlineParts.begin(), inlineParts.end());
    }
    if (!regularParts.empty()) {
        std::vector<std::string> parts;
        if (haveBody) parts.push_back(body.whole());
        parts.insert(parts.end(), regularParts.begin(), regularParts.end());
        body = wrapMultipart("mixed", parts);
    }

    out.data = hdr + body.headers + "\r\n" + body.body;
    return out;
}
