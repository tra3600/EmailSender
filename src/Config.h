#ifndef EMAILSENDER_CONFIG_H
#define EMAILSENDER_CONFIG_H

#include <string>

#include "EmailSender.h"

struct Settings {
    SmtpConfig smtp;
    Address from;
    long delayMs = 0;  // pause entre deux messages en mode batch
};

// Fichier de configuration "clé = valeur" (# ou ; pour les commentaires).
// Clés : server, port, username, password, security, helo, timeout, connect_timeout, retries,
//        retry_delay_ms, verify_tls, ca_bundle, from, from_name, delay_ms
bool loadConfigFile(const std::string& path, Settings& s, std::string& error);

// Variables d'environnement : EMAILSENDER_SERVER, _PORT, _USER, _PASSWORD, _SECURITY, _FROM, _FROM_NAME,
// _HELO, _TIMEOUT, _RETRIES, _CA_BUNDLE.
bool applyEnvironment(Settings& s, std::string& error);

// Applique une clé/valeur ; utilisé par le fichier de config et la CLI.
bool applySetting(Settings& s, const std::string& key, const std::string& value, std::string& error);

// Chemin de config par défaut : $EMAILSENDER_CONFIG, ./emailsender.conf ou ~/.config/emailsender.conf (si présent).
std::string defaultConfigPath();

#endif
