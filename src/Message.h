#ifndef EMAILSENDER_MESSAGE_H
#define EMAILSENDER_MESSAGE_H

#include <ctime>
#include <string>
#include <utility>
#include <vector>

struct Address {
    std::string name;   // nom affiché (UTF-8 accepté), optionnel
    std::string email;  // adresse nue : user@domaine

    bool empty() const { return email.empty(); }
    // Analyse "Nom <a@b.c>", "\"Nom, Prénom\" <a@b.c>" ou "a@b.c". Renvoie false si invalide.
    static bool parse(const std::string& text, Address& out, std::string* error = nullptr);
    // Analyse une liste séparée par des virgules ou des points-virgules.
    static bool parseList(const std::string& text, std::vector<Address>& out, std::string* error = nullptr);
    static bool isValidEmail(const std::string& email);
    // Forme prête pour un en-tête (guillemets / encoded-words si nécessaire).
    std::string format() const;
};

struct Attachment {
    std::string path;         // fichier à lire (ignoré si data non vide)
    std::string data;         // contenu en mémoire
    std::string filename;     // nom affiché ; par défaut le nom du fichier
    std::string contentType;  // deviné d'après l'extension si vide
    std::string contentId;    // non vide => pièce incorporée (<img src="cid:...">)
};

enum class Priority { Low, Normal, High };

struct Email {
    Address from;
    std::vector<Address> to, cc, bcc;
    Address replyTo;
    Address sender;  // en-tête Sender (optionnel)
    std::string returnPath;  // enveloppe MAIL FROM ; par défaut from.email
    std::string subject;
    std::string textBody;
    std::string htmlBody;
    std::vector<Attachment> attachments;
    Priority priority = Priority::Normal;
    bool requestReadReceipt = false;
    std::string inReplyTo;
    std::string references;
    std::vector<std::pair<std::string, std::string>> headers;  // en-têtes personnalisés
    std::time_t date = 0;  // 0 => maintenant
};

struct BuiltMessage {
    std::string data;                     // message RFC 5322 complet (CRLF), sans en-tête Bcc
    std::string messageId;
    std::string envelopeFrom;             // adresse nue pour MAIL FROM
    std::vector<std::string> recipients;  // To + Cc + Bcc dédoublonnés (adresses nues)
};

// Valide et construit le message MIME. Lève std::invalid_argument / std::runtime_error.
BuiltMessage buildMessage(const Email& email);

#endif
