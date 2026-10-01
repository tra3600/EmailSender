#include <cstdlib>
#include <iostream>

#include "Config.h"
#include "Message.h"
#include "Mime.h"
#include "Template.h"

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK FAILED: " #cond << std::endl; \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static bool contains(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

static Email basic() {
    Email e;
    Address::parse("Alice <alice@example.com>", e.from);
    Address::parseList("bob@example.com", e.to);
    e.subject = "Hello";
    e.textBody = "Body";
    return e;
}

int main() {
    // Base64
    CHECK(mime::base64Encode("", false) == "");
    CHECK(mime::base64Encode("f", false) == "Zg==");
    CHECK(mime::base64Encode("fo", false) == "Zm8=");
    CHECK(mime::base64Encode("foo", false) == "Zm9v");
    { std::string bin("héllo wörld \xff\x00z", 17); CHECK(mime::base64Decode(mime::base64Encode(bin, true)) == bin); }
    std::string longB64 = mime::base64Encode(std::string(200, 'x'), true);
    CHECK(contains(longB64, "\r\n"));

    // RFC 2047
    CHECK(mime::encodeHeaderWord("plain") == "plain");
    std::string enc = mime::encodeHeaderWord("Réunion à 14h – ordre du jour très détaillé et accentué");
    CHECK(enc.rfind("=?UTF-8?B?", 0) == 0);
    CHECK(contains(enc, "\r\n =?UTF-8?B?"));

    // CRLF
    CHECK(mime::normalizeCrlf("a\nb\r\nc\rd") == "a\r\nb\r\nc\r\nd");

    // Types MIME
    CHECK(mime::guessContentType("x.PDF") == "application/pdf");
    CHECK(mime::guessContentType("x.unknown") == "application/octet-stream");

    // Adresses
    Address a;
    CHECK(Address::parse("Jean Dupont <jean@ex.fr>", a) && a.name == "Jean Dupont" && a.email == "jean@ex.fr");
    CHECK(Address::parse("\"Dupont, Jean\" <jean@ex.fr>", a) && a.name == "Dupont, Jean");
    CHECK(!Address::parse("pas-une-adresse", a));
    CHECK(!Address::parse("a@b@c.fr", a));
    CHECK(!Address::parse("a@ex.fr\r\nBcc: evil@x.com", a));
    std::vector<Address> list;
    CHECK(Address::parseList("a@x.fr, \"B, C\" <b@x.fr>; c@x.fr", list) && list.size() == 3);
    { Address q; q.name = "Dupont, Jean"; q.email = "j@x.fr"; CHECK(q.format() == "\"Dupont, Jean\" <j@x.fr>"); }

    // Message simple
    {
        BuiltMessage m = buildMessage(basic());
        CHECK(contains(m.data, "From: Alice <alice@example.com>\r\n"));
        CHECK(contains(m.data, "To: bob@example.com\r\n"));
        CHECK(contains(m.data, "Subject: Hello\r\n"));
        CHECK(contains(m.data, "Date: "));
        CHECK(contains(m.data, "Message-ID: <"));
        CHECK(contains(m.data, "MIME-Version: 1.0\r\n"));
        CHECK(contains(m.data, "Content-Type: text/plain; charset=UTF-8\r\n"));
        CHECK(m.envelopeFrom == "alice@example.com");
        CHECK(m.recipients.size() == 1);
    }

    // Cc / Bcc : Bcc absent des en-têtes mais présent dans l'enveloppe, doublons retirés
    {
        Email e = basic();
        Address::parseList("carol@example.com, BOB@example.com", e.cc);
        Address::parseList("secret@example.com", e.bcc);
        BuiltMessage m = buildMessage(e);
        CHECK(contains(m.data, "Cc: carol@example.com, BOB@example.com"));
        CHECK(!contains(m.data, "secret@example.com"));
        CHECK(m.recipients.size() == 3);
    }

    // Unicode : objet + corps
    {
        Email e = basic();
        e.subject = "Élève : résultat";
        e.textBody = "Bonjour, ça va ?";
        BuiltMessage m = buildMessage(e);
        CHECK(contains(m.data, "Subject: =?UTF-8?B?"));
        CHECK(contains(m.data, "Content-Transfer-Encoding: base64"));
    }

    // Multipart alternative + pièce jointe + inline
    {
        Email e = basic();
        e.htmlBody = "<p>Salut <img src=\"cid:logo\"></p>";
        Attachment att;
        att.data = "hello";
        att.filename = "note.txt";
        e.attachments.push_back(att);
        Attachment img;
        img.data = "PNGDATA";
        img.filename = "logo.png";
        img.contentId = "logo";
        e.attachments.push_back(img);
        BuiltMessage m = buildMessage(e);
        CHECK(contains(m.data, "multipart/mixed"));
        CHECK(contains(m.data, "multipart/related"));
        CHECK(contains(m.data, "multipart/alternative"));
        CHECK(contains(m.data, "Content-Disposition: attachment; filename=\"note.txt\""));
        CHECK(contains(m.data, "Content-ID: <logo>"));
        CHECK(contains(m.data, "text/html"));
    }

    // Priorité, accusé, en-têtes perso
    {
        Email e = basic();
        e.priority = Priority::High;
        e.requestReadReceipt = true;
        e.headers.push_back({"List-Unsubscribe", "<mailto:stop@example.com>"});
        BuiltMessage m = buildMessage(e);
        CHECK(contains(m.data, "X-Priority: 1"));
        CHECK(contains(m.data, "Disposition-Notification-To: Alice <alice@example.com>"));
        CHECK(contains(m.data, "List-Unsubscribe: <mailto:stop@example.com>"));
    }

    // Sécurité : injection d'en-têtes et erreurs
    {
        Email e = basic();
        e.subject = "Hi\r\nBcc: evil@example.com";
        bool threw = false;
        try { buildMessage(e); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);

        e = basic();
        e.headers.push_back({"Bad Name", "x"});
        threw = false;
        try { buildMessage(e); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);

        e = basic();
        e.attachments.push_back(Attachment{"/nonexistent/file.bin", "", "", "", ""});
        threw = false;
        try { buildMessage(e); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);

        e = basic();
        e.to.clear();
        threw = false;
        try { buildMessage(e); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);

        e = basic();
        e.textBody.clear();
        threw = false;
        try { buildMessage(e); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }

    // Templates
    {
        Vars v = {{"nom", "Zoé <b>"}, {"vide", ""}};
        std::vector<std::string> missing;
        CHECK(renderTemplate("Salut {{ nom }} !", v) == "Salut Zoé <b> !");
        CHECK(renderTemplate("{{nom}}", v, true) == "Zoé &lt;b&gt;");
        CHECK(renderTemplate("{{vide|ami}} {{inconnu|x}}", v) == "ami x");
        CHECK(renderTemplate("a{{inconnu}}b", v, false, &missing) == "ab" && missing.size() == 1 && missing[0] == "inconnu");
        CHECK(renderTemplate("non fermé {{x", v) == "non fermé {{x");
    }

    // CSV
    {
        auto rows = parseCsv("\xEF\xBB\xBF" "email;nom\r\na@x.fr;\"Dupont; Jean\"\r\n\r\nb@x.fr;\"Dit \"\"Bob\"\"\"\r\n");
        CHECK(rows.size() == 3);
        CHECK(rows[1][1] == "Dupont; Jean");
        CHECK(rows[2][1] == "Dit \"Bob\"");
        auto rows2 = parseCsv("email,nom\na@x.fr,\"ligne1\nligne2\"");
        CHECK(rows2.size() == 2 && rows2[1][1] == "ligne1\nligne2");
    }

    // Configuration
    {
        Settings s;
        std::string err;
        CHECK(applySetting(s, "port", "465", err) && s.smtp.port == 465);
        CHECK(!applySetting(s, "port", "abc", err));
        CHECK(!applySetting(s, "port", "70000", err));
        CHECK(applySetting(s, "security", "SSL", err) && s.smtp.security == Security::Ssl);
        CHECK(!applySetting(s, "security", "bogus", err));
        CHECK(applySetting(s, "from", "Moi <moi@ex.fr>", err) && s.from.name == "Moi");
        CHECK(!applySetting(s, "inconnu", "x", err));
        CHECK(applySetting(s, "verify_tls", "non", err) && !s.smtp.verifyPeer);
    }

    if (failures) {
        std::cerr << failures << " échec(s)" << std::endl;
        return 1;
    }
    std::cout << "Tous les tests unitaires passent." << std::endl;
    return 0;
}
