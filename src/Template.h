#ifndef EMAILSENDER_TEMPLATE_H
#define EMAILSENDER_TEMPLATE_H

#include <map>
#include <string>
#include <vector>

#include "Message.h"

using Vars = std::map<std::string, std::string>;

// Remplace {{variable}} et {{variable|valeur par défaut}}.
// escapeHtml : échappe les valeurs substituées (pour un corps HTML).
// missing : reçoit les variables inconnues sans valeur par défaut (remplacées par "").
std::string renderTemplate(const std::string& tpl, const Vars& vars, bool escapeHtml = false,
                           std::vector<std::string>* missing = nullptr);

std::string htmlEscape(const std::string& s);

struct Recipient {
    Address address;
    Vars vars;
};

// Analyse un CSV RFC 4180 (séparateur , ou ; détecté, BOM toléré).
std::vector<std::vector<std::string>> parseCsv(const std::string& text);

// Charge un CSV de destinataires. Colonne obligatoire : email (ou to / mail / adresse).
// Colonne facultative : name / nom. Toutes les colonnes deviennent des variables de template.
bool loadRecipientsCsv(const std::string& path, std::vector<Recipient>& out, std::string& error);

#endif
