#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "Config.h"
#include "EmailSender.h"
#include "Message.h"
#include "Mime.h"
#include "Template.h"

#define EMAILSENDER_VERSION "2.0.0"

namespace {

const char* kUsage = R"(EmailSender )" EMAILSENDER_VERSION R"( - envoi d'e-mails SMTP en ligne de commande

Usage :
  emailsender [options] --to dest@example.com --subject "Objet" --body "Texte"
  emailsender [options] --batch destinataires.csv --subject "Bonjour {{nom}}" --html-file modele.html
  emailsender --test-connection [options]

Serveur (priorité : défauts < fichier de config < variables EMAILSENDER_* < options) :
  -c, --config FICHIER      fichier de configuration (clé = valeur)
  -s, --server HOTE         serveur SMTP                       (défaut : localhost)
  -p, --port N              port                               (défaut : 587)
  -u, --user NOM            identifiant d'authentification
      --password-file F     lit le mot de passe dans un fichier (1re ligne)
      --password-stdin      lit le mot de passe sur l'entrée standard
      --ask-password        demande le mot de passe au terminal
      --security MODE       auto | none | starttls | ssl       (défaut : auto)
      --helo DOMAINE        domaine annoncé dans EHLO
      --timeout S           délai maximal par envoi            (défaut : 60)
      --retries N           nouvelles tentatives (erreurs transitoires) (défaut : 2)
      --insecure            n'authentifie pas le certificat TLS (déconseillé)
      --ca-bundle FICHIER   certificats CA personnalisés
      --verbose             trace le dialogue SMTP sur stderr

Message :
  -f, --from ADRESSE        expéditeur ("Nom <a@b.c>" accepté)
  -t, --to LISTE            destinataires (répétable, séparés par , ou ;)
      --cc LISTE            copie
      --bcc LISTE           copie cachée
      --reply-to ADRESSE    adresse de réponse
  -S, --subject TEXTE       objet
  -b, --body TEXTE          corps texte
      --body-file F         corps texte depuis un fichier ('-' = entrée standard)
      --html TEXTE          corps HTML
      --html-file F         corps HTML depuis un fichier
      --no-auto-text        ne génère pas de version texte à partir du HTML
  -a, --attach FICHIER      pièce jointe (répétable)
      --inline FICHIER[:CID] image incorporée, référencée par <img src="cid:CID"> (répétable)
      --priority P          low | normal | high
      --receipt             demande un accusé de lecture
      --header "Nom: val"   en-tête personnalisé (répétable)
      --in-reply-to ID      identifiant du message auquel on répond
      --references IDS      en-tête References

Envoi en masse :
      --batch CSV           un message par ligne (colonne email, nom, + variables {{col}})
      --var CLE=VALEUR      variable de template supplémentaire (répétable)
      --delay-ms N          pause entre deux messages
      --report FICHIER      écrit un rapport CSV (email, statut, erreur)

Divers :
      --dry-run             construit et affiche le message sans l'envoyer
      --save-eml CHEMIN     enregistre le message en .eml (en batch : un répertoire)
      --test-connection     teste connexion, TLS et authentification
  -q, --quiet               n'affiche que les erreurs
  -h, --help / --version

Variables d'environnement : EMAILSENDER_SERVER, _PORT, _USER, _PASSWORD, _SECURITY, _FROM,
_FROM_NAME, _HELO, _TIMEOUT, _RETRIES, _CA_BUNDLE, _CONFIG.
Codes de sortie : 0 succès, 1 échec d'envoi, 2 erreur d'usage.
)";

struct Options {
    std::string configPath;
    std::vector<std::pair<std::string, std::string>> settingOverrides;
    std::string passwordFile;
    bool passwordStdin = false, askPassword = false;
    std::vector<std::string> to, cc, bcc;
    std::string replyTo, subject, body, bodyFile, html, htmlFile;
    bool noAutoText = false;
    std::vector<std::string> attach, inlineFiles;
    std::string priority = "normal";
    bool receipt = false;
    std::vector<std::string> headers;
    std::string inReplyTo, references;
    std::string batch;
    Vars vars;
    std::string report, saveEml;
    bool dryRun = false, testConnection = false, quiet = false;
    bool haveBody = false, haveHtml = false;
};

struct UsageError {
    std::string message;
};

std::string readAll(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string readFileOrStdin(const std::string& path) {
    if (path == "-") return readAll(std::cin);
    std::ifstream f(path, std::ios::binary);
    if (!f) throw UsageError{"Impossible de lire le fichier : " + path};
    return readAll(f);
}

std::string stripTrailingNewline(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

std::string askPassword() {
    std::cerr << "Mot de passe SMTP : " << std::flush;
    termios oldt{};
    bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0;
    if (tty) {
        termios newt = oldt;
        newt.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }
    std::string pw;
    std::getline(std::cin, pw);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cerr << std::endl;
    }
    return stripTrailingNewline(pw);
}

// Conversion HTML -> texte minimale pour la partie text/plain.
std::string htmlToText(const std::string& html) {
    std::string out;
    size_t i = 0;
    auto lowerAt = [&](size_t pos, const char* tag) {
        size_t n = std::strlen(tag);
        if (pos + n > html.size()) return false;
        for (size_t k = 0; k < n; ++k)
            if (std::tolower((unsigned char)html[pos + k]) != tag[k]) return false;
        return true;
    };
    while (i < html.size()) {
        if (html[i] == '<') {
            if (lowerAt(i, "<style") || lowerAt(i, "<script")) {
                const char* end = lowerAt(i, "<style") ? "</style" : "</script";
                while (i < html.size() && !lowerAt(i, end)) ++i;
            }
            bool brk = lowerAt(i, "<br") || lowerAt(i, "</p") || lowerAt(i, "</div") || lowerAt(i, "</h") ||
                       lowerAt(i, "</tr") || lowerAt(i, "</li");
            while (i < html.size() && html[i] != '>') ++i;
            ++i;
            if (brk) out += '\n';
            else if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
        } else if (html[i] == '&') {
            static const std::pair<const char*, const char*> ents[] = {
                {"&nbsp;", " "}, {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}};
            bool done = false;
            for (const auto& e : ents)
                if (html.compare(i, std::strlen(e.first), e.first) == 0) {
                    out += e.second;
                    i += std::strlen(e.first);
                    done = true;
                    break;
                }
            if (!done) out += html[i++];
        } else {
            out += html[i++];
        }
    }
    // compacte les espaces / lignes vides
    std::string res;
    int nl = 0;
    for (char c : out) {
        if (c == '\n') { if (++nl <= 2) res += c; }
        else if (c == ' ' && (res.empty() || res.back() == ' ' || res.back() == '\n')) continue;
        else { nl = 0; res += c; }
    }
    return stripTrailingNewline(res);
}

bool splitKeyValue(const std::string& s, char sep, std::string& k, std::string& v) {
    size_t p = s.find(sep);
    if (p == std::string::npos) return false;
    k = s.substr(0, p);
    v = s.substr(p + 1);
    return true;
}

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i], inlineValue;
        bool hasInline = false;
        if (arg.rfind("--", 0) == 0) {
            size_t eq = arg.find('=');
            if (eq != std::string::npos) {
                inlineValue = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
                hasInline = true;
            }
        }
        auto value = [&]() -> std::string {
            if (hasInline) return inlineValue;
            if (i + 1 >= argc) throw UsageError{"Valeur manquante pour " + arg};
            return argv[++i];
        };
        auto setting = [&](const char* key) { o.settingOverrides.emplace_back(key, value()); };

        if (arg == "-h" || arg == "--help") { std::cout << kUsage; std::exit(0); }
        else if (arg == "--version") { std::cout << "EmailSender " EMAILSENDER_VERSION "\n"; std::exit(0); }
        else if (arg == "-c" || arg == "--config") o.configPath = value();
        else if (arg == "-s" || arg == "--server") setting("server");
        else if (arg == "-p" || arg == "--port") setting("port");
        else if (arg == "-u" || arg == "--user") setting("username");
        else if (arg == "--password-file") o.passwordFile = value();
        else if (arg == "--password-stdin") o.passwordStdin = true;
        else if (arg == "--ask-password") o.askPassword = true;
        else if (arg == "--security") setting("security");
        else if (arg == "--helo") setting("helo");
        else if (arg == "--timeout") setting("timeout");
        else if (arg == "--retries") setting("retries");
        else if (arg == "--insecure") o.settingOverrides.emplace_back("verify_tls", "false");
        else if (arg == "--ca-bundle") setting("ca_bundle");
        else if (arg == "--verbose") o.settingOverrides.emplace_back("__verbose", "1");
        else if (arg == "-f" || arg == "--from") setting("from");
        else if (arg == "-t" || arg == "--to") o.to.push_back(value());
        else if (arg == "--cc") o.cc.push_back(value());
        else if (arg == "--bcc") o.bcc.push_back(value());
        else if (arg == "--reply-to") o.replyTo = value();
        else if (arg == "-S" || arg == "--subject") o.subject = value();
        else if (arg == "-b" || arg == "--body") { o.body = value(); o.haveBody = true; }
        else if (arg == "--body-file") { o.bodyFile = value(); o.haveBody = true; }
        else if (arg == "--html") { o.html = value(); o.haveHtml = true; }
        else if (arg == "--html-file") { o.htmlFile = value(); o.haveHtml = true; }
        else if (arg == "--no-auto-text") o.noAutoText = true;
        else if (arg == "-a" || arg == "--attach") o.attach.push_back(value());
        else if (arg == "--inline") o.inlineFiles.push_back(value());
        else if (arg == "--priority") o.priority = value();
        else if (arg == "--receipt") o.receipt = true;
        else if (arg == "--header") o.headers.push_back(value());
        else if (arg == "--in-reply-to") o.inReplyTo = value();
        else if (arg == "--references") o.references = value();
        else if (arg == "--batch") o.batch = value();
        else if (arg == "--var") {
            std::string k, v, kv = value();
            if (!splitKeyValue(kv, '=', k, v)) throw UsageError{"--var attend CLE=VALEUR : " + kv};
            o.vars[k] = v;
        }
        else if (arg == "--delay-ms") setting("delay_ms");
        else if (arg == "--report") o.report = value();
        else if (arg == "--dry-run") o.dryRun = true;
        else if (arg == "--save-eml") o.saveEml = value();
        else if (arg == "--test-connection") o.testConnection = true;
        else if (arg == "-q" || arg == "--quiet") o.quiet = true;
        else throw UsageError{"Option inconnue : " + arg + " (voir --help)"};
    }
    return o;
}

void parseInto(const std::vector<std::string>& raw, std::vector<Address>& out, const char* what) {
    for (const auto& r : raw) {
        std::string err;
        if (!Address::parseList(r, out, &err)) throw UsageError{std::string(what) + " : " + err};
    }
}

std::string csvQuote(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}

bool saveEml(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << data;
    return (bool)f;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Settings settings;
        Options o = parseArgs(argc, argv);

        std::string err;
        std::string cfg = o.configPath.empty() ? defaultConfigPath() : o.configPath;
        if (!cfg.empty() && !loadConfigFile(cfg, settings, err)) throw UsageError{err};
        if (!applyEnvironment(settings, err)) throw UsageError{err};
        for (const auto& kv : o.settingOverrides) {
            if (kv.first == "__verbose") { settings.smtp.verbose = true; continue; }
            if (!applySetting(settings, kv.first, kv.second, err)) throw UsageError{err};
        }
        if (!o.passwordFile.empty()) {
            std::ifstream f(o.passwordFile);
            std::string pw;
            if (!f || !std::getline(f, pw)) throw UsageError{"Impossible de lire " + o.passwordFile};
            settings.smtp.password = stripTrailingNewline(pw);
        } else if (o.passwordStdin) {
            std::string pw;
            std::getline(std::cin, pw);
            settings.smtp.password = stripTrailingNewline(pw);
        } else if (o.askPassword) {
            settings.smtp.password = askPassword();
        }
        if (settings.from.empty() && Address::isValidEmail(settings.smtp.username))
            settings.from.email = settings.smtp.username;

        EmailSender sender(settings.smtp);

        if (o.testConnection) {
            SendResult r = sender.testConnection();
            if (!r.ok) {
                std::cerr << "Échec de la connexion à " << settings.smtp.server << ":" << settings.smtp.port
                          << " : " << r.error << std::endl;
                return 1;
            }
            std::cout << "Connexion OK vers " << settings.smtp.server << ":" << settings.smtp.port
                      << " (sécurité : " << securityName(settings.smtp.security) << ")" << std::endl;
            return 0;
        }

        // --- construction du message de base
        if (!o.batch.empty() && !o.to.empty()) throw UsageError{"--batch et --to sont exclusifs"};
        if (o.batch.empty() && o.to.empty() && o.cc.empty() && o.bcc.empty())
            throw UsageError{"Aucun destinataire : utilisez --to ou --batch (voir --help)"};

        Email base;
        base.from = settings.from;
        if (base.from.empty()) throw UsageError{"Expéditeur manquant : utilisez --from ou 'from' dans la config"};
        parseInto(o.to, base.to, "--to");
        parseInto(o.cc, base.cc, "--cc");
        parseInto(o.bcc, base.bcc, "--bcc");
        if (!o.replyTo.empty() && !Address::parse(o.replyTo, base.replyTo, &err)) throw UsageError{"--reply-to : " + err};
        base.requestReadReceipt = o.receipt;
        base.inReplyTo = o.inReplyTo;
        base.references = o.references;
        if (o.priority == "low") base.priority = Priority::Low;
        else if (o.priority == "high") base.priority = Priority::High;
        else if (o.priority != "normal") throw UsageError{"--priority : low | normal | high"};
        for (const auto& h : o.headers) {
            std::string k, v;
            if (!splitKeyValue(h, ':', k, v)) throw UsageError{"--header attend 'Nom: valeur' : " + h};
            size_t b = v.find_first_not_of(' ');
            base.headers.emplace_back(k, b == std::string::npos ? "" : v.substr(b));
        }

        std::string subject = o.subject;
        std::string text = !o.bodyFile.empty() ? readFileOrStdin(o.bodyFile) : o.body;
        std::string html = !o.htmlFile.empty() ? readFileOrStdin(o.htmlFile) : o.html;
        if (text.empty() && !html.empty() && !o.noAutoText) text = htmlToText(html);

        for (const auto& a : o.attach) {
            Attachment at;
            at.path = a;
            base.attachments.push_back(at);
        }
        for (const auto& spec : o.inlineFiles) {
            Attachment at;
            std::string path = spec, cid;
            size_t colon = spec.rfind(':');
            if (colon != std::string::npos && colon > 0 && spec.find('/', colon) == std::string::npos) {
                path = spec.substr(0, colon);
                cid = spec.substr(colon + 1);
            }
            at.path = path;
            at.contentId = cid.empty() ? mime::baseName(path) : cid;
            base.attachments.push_back(at);
        }

        // --- liste des envois
        std::vector<Recipient> recipients;
        bool templated = !o.batch.empty() || !o.vars.empty();
        if (!o.batch.empty()) {
            if (!loadRecipientsCsv(o.batch, recipients, err)) throw UsageError{err};
        } else {
            recipients.push_back({Address{}, {}});
        }
        if (!o.saveEml.empty() && recipients.size() > 1) mkdir(o.saveEml.c_str(), 0755);

        int sent = 0, failed = 0, index = 0;
        std::ofstream report;
        if (!o.report.empty()) {
            report.open(o.report);
            if (!report) throw UsageError{"Impossible d'écrire le rapport : " + o.report};
            report << "email,status,message_id,error\n";
        }

        for (const auto& rc : recipients) {
            ++index;
            Email e = base;
            std::string label;
            if (!o.batch.empty()) {
                e.to = {rc.address};
                label = rc.address.email;
            } else {
                label = e.to.empty() ? (e.cc.empty() ? e.bcc[0].email : e.cc[0].email) : e.to[0].email;
            }
            Vars vars = o.vars;
            for (const auto& kv : rc.vars) vars[kv.first] = kv.second;
            std::vector<std::string> missing;
            e.subject = templated ? renderTemplate(subject, vars, false, &missing) : subject;
            e.textBody = templated ? renderTemplate(text, vars, false, &missing) : text;
            e.htmlBody = templated ? renderTemplate(html, vars, true, &missing) : html;
            if (!missing.empty() && !o.quiet) {
                std::cerr << "Avertissement (" << label << ") : variables sans valeur :";
                for (const auto& m : missing) std::cerr << " {{" << m << "}}";
                std::cerr << std::endl;
            }

            SendResult r;
            try {
                BuiltMessage built = buildMessage(e);
                if (!o.saveEml.empty()) {
                    std::string path = recipients.size() > 1 ? o.saveEml + "/" + std::to_string(index) + ".eml" : o.saveEml;
                    if (!saveEml(path, built.data)) throw std::runtime_error("Impossible d'écrire " + path);
                }
                if (o.dryRun) {
                    std::cout << "----- [dry-run] " << label << " -----\n" << built.data << std::endl;
                    r.ok = true;
                    r.messageId = built.messageId;
                } else {
                    r = sender.sendRaw(built);
                }
            } catch (const std::exception& ex) {
                r.ok = false;
                r.error = ex.what();
            }

            if (r.ok) {
                ++sent;
                if (!o.quiet && !o.dryRun)
                    std::cout << "[" << index << "/" << recipients.size() << "] envoyé à " << label << " " << r.messageId << std::endl;
            } else {
                ++failed;
                std::cerr << "[" << index << "/" << recipients.size() << "] ÉCHEC pour " << label << " : " << r.error << std::endl;
            }
            if (report.is_open())
                report << csvQuote(label) << "," << (r.ok ? (o.dryRun ? "dry-run" : "sent") : "failed") << ","
                       << csvQuote(r.messageId) << "," << csvQuote(r.error) << "\n";
            if (settings.delayMs > 0 && index < (int)recipients.size())
                std::this_thread::sleep_for(std::chrono::milliseconds(settings.delayMs));
        }

        if (!o.quiet && recipients.size() > 1)
            std::cout << "Terminé : " << sent << " réussi(s), " << failed << " échec(s)." << std::endl;
        return failed ? 1 : 0;
    } catch (const UsageError& u) {
        std::cerr << "Erreur : " << u.message << std::endl;
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "Erreur : " << ex.what() << std::endl;
        return 1;
    }
}
