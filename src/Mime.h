#ifndef EMAILSENDER_MIME_H
#define EMAILSENDER_MIME_H

#include <ctime>
#include <string>

// Utilitaires MIME / RFC 5322 : encodages, dates, identifiants.
namespace mime {

std::string base64Encode(const std::string& data, bool wrapLines = true);
std::string base64Decode(const std::string& data);

bool isAscii(const std::string& s);
bool hasControlChars(const std::string& s);  // CR, LF ou autres caractères de contrôle

// Encode une valeur d'en-tête non-ASCII en "encoded-words" RFC 2047 (UTF-8, base64).
std::string encodeHeaderWord(const std::string& utf8);

// Remplace tout saut de ligne (LF, CR, CRLF) par CRLF.
std::string normalizeCrlf(const std::string& s);

std::string guessContentType(const std::string& filename);
std::string baseName(const std::string& path);

std::string rfc5322Date(std::time_t t);
std::string generateBoundary();
std::string generateMessageId(const std::string& domain);

}  // namespace mime

#endif
