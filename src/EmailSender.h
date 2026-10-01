#ifndef EMAILSENDER_H
#define EMAILSENDER_H

#include <functional>
#include <string>
#include <vector>

#include "Message.h"

enum class Security {
    Auto,      // 465 => SSL implicite, 587 => STARTTLS obligatoire, autre => STARTTLS si possible
    None,      // aucun chiffrement (déconseillé)
    StartTls,  // STARTTLS obligatoire
    Ssl        // TLS implicite (smtps://)
};

bool parseSecurity(const std::string& text, Security& out);
const char* securityName(Security s);

struct SmtpConfig {
    std::string server = "localhost";
    int port = 587;
    std::string username;  // vide => pas d'authentification
    std::string password;
    Security security = Security::Auto;
    std::string heloDomain;       // domaine annoncé dans EHLO (défaut : nom d'hôte local)
    long timeoutSec = 60;         // durée maximale d'un envoi
    long connectTimeoutSec = 20;
    int maxRetries = 2;           // nouvelles tentatives sur erreur transitoire
    long retryDelayMs = 1000;     // délai initial, doublé à chaque tentative
    bool verifyPeer = true;       // vérification du certificat TLS
    bool verifyHost = true;
    std::string caBundle;         // fichier CA personnalisé
    bool verbose = false;         // trace du dialogue SMTP sur stderr (peut exposer des données)
};

struct SendResult {
    bool ok = false;
    std::string error;
    std::string messageId;
    int attempts = 0;
    long smtpCode = 0;  // dernier code de réponse SMTP, si connu
};

class EmailSender {
public:
    explicit EmailSender(SmtpConfig config);
    // Compatibilité avec l'ancienne API.
    EmailSender(const std::string& smtpServer, int port, const std::string& username, const std::string& password);

    // Envoi d'un message complet (HTML, pièces jointes, Cc/Bcc, ...). Ne lève pas d'exception.
    SendResult send(const Email& email);
    // Envoi simple texte (ancienne API). From = username.
    bool sendEmail(const std::string& to, const std::string& subject, const std::string& body);
    // Envoi d'un message déjà construit.
    SendResult sendRaw(const BuiltMessage& message);
    // Vérifie la connexion, le chiffrement et l'authentification sans envoyer de message.
    SendResult testConnection();

    const SmtpConfig& config() const { return config_; }
    const std::string& lastError() const { return lastError_; }

private:
    SmtpConfig config_;
    std::string lastError_;
};

#endif  // EMAILSENDER_H
