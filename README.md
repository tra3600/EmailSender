# EmailSender

Client d'envoi d'e-mails SMTP en C++17 basé sur **libcurl**, utilisable comme **bibliothèque** ou comme **outil en ligne de commande**.

## Fonctionnalités

- **SMTP sécurisé** : STARTTLS, TLS implicite (smtps, port 465), choix automatique selon le port, vérification des certificats (CA personnalisable), authentification PLAIN/LOGIN/…
- **Messages MIME complets** : texte, HTML, `multipart/alternative` (version texte générée automatiquement depuis le HTML), pièces jointes, images incorporées (`cid:`), Unicode (objet et noms encodés RFC 2047, corps en base64 UTF-8).
- **En-têtes** : From (nom affiché), To, Cc, **Bcc** (jamais exposé dans les en-têtes), Reply-To, Sender, Return-Path, Date, Message-ID, In-Reply-To/References, priorité, accusé de lecture, en-têtes personnalisés.
- **Sécurité** : validation des adresses, blocage de l'injection d'en-têtes (CR/LF), mot de passe via variable d'environnement, fichier, stdin ou saisie masquée, avertissement si le fichier de config est lisible par tous.
- **Fiabilité** : délais configurables, nouvelles tentatives avec backoff exponentiel sur erreurs transitoires (réseau, SMTP 4xx), pas de nouvelle tentative sur erreurs définitives (5xx, authentification).
- **Envoi en masse** : un message par ligne d'un CSV, avec variables `{{nom}}` et valeurs par défaut `{{ville|ailleurs}}` (échappement HTML automatique), pause entre envois, rapport CSV.
- **Outils** : `--dry-run`, `--save-eml`, `--test-connection`, codes de sortie explicites.
- **Configuration** : fichier `clé = valeur`, variables d'environnement `EMAILSENDER_*`, options CLI (priorité croissante).
- **Tests** : tests unitaires + test d'intégration contre un faux serveur SMTP local (`ctest`).

## Compilation

```bash
sudo apt-get install build-essential cmake libcurl4-openssl-dev   # Debian/Ubuntu
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build        # facultatif
```

## Utilisation en ligne de commande

```bash
export EMAILSENDER_PASSWORD='mot-de-passe-d-application'

# Message simple
./build/emailsender -s smtp.gmail.com -u moi@gmail.com -f "Moi <moi@gmail.com>" \
    -t dest@example.com -S "Objet" -b "Bonjour !"

# HTML + pièces jointes + image incorporée + copie cachée
./build/emailsender -c emailsender.conf -t "Zoé <zoe@example.com>" --cc chef@example.com \
    --bcc archive@example.com -S "Rapport" --html-file examples/modele.html \
    -a rapport.pdf --inline logo.png:logo --priority high --receipt

# Envoi en masse depuis un CSV, avec rapport
./build/emailsender -c emailsender.conf --batch examples/destinataires.csv \
    -S "Bonjour {{nom}}" --html-file examples/modele.html --inline logo.png:logo \
    --delay-ms 500 --report rapport.csv

# Vérifier le serveur sans envoyer
./build/emailsender -c emailsender.conf --test-connection

# Voir le message sans l'envoyer
./build/emailsender -f a@b.fr -t c@d.fr -S Test -b Salut --dry-run
```

`emailsender --help` liste toutes les options. Codes de sortie : `0` succès, `1` échec d'envoi, `2` erreur d'usage.

Exemple de configuration : `examples/emailsender.conf`.
Variables d'environnement : `EMAILSENDER_SERVER`, `_PORT`, `_USER`, `_PASSWORD`, `_SECURITY`, `_FROM`, `_FROM_NAME`, `_HELO`, `_TIMEOUT`, `_RETRIES`, `_CA_BUNDLE`, `_CONFIG`.

> Gmail et la plupart des fournisseurs exigent un **mot de passe d'application** et non le mot de passe du compte.

## Utilisation comme bibliothèque

```cpp
#include "EmailSender.h"

SmtpConfig cfg;
cfg.server = "smtp.example.com";
cfg.port = 587;                 // STARTTLS automatique
cfg.username = "moi@example.com";
cfg.password = std::getenv("SMTP_PASSWORD");

EmailSender sender(cfg);

Email mail;
Address::parse("Moi <moi@example.com>", mail.from);
Address::parseList("dest@example.com, autre@example.com", mail.to);
mail.subject = "Bonjour";
mail.textBody = "Version texte";
mail.htmlBody = "<p>Version <b>HTML</b></p>";
Attachment a; a.path = "rapport.pdf"; mail.attachments.push_back(a);

SendResult r = sender.send(mail);
if (!r.ok) std::cerr << r.error << " (tentatives : " << r.attempts << ")\n";
```

L'ancienne API reste disponible : `EmailSender(server, port, user, password).sendEmail(to, subject, body)`.

## Structure

| Fichier | Rôle |
|---|---|
| `src/Mime.*` | base64, RFC 2047, dates, types MIME, identifiants |
| `src/Message.*` | adresses, construction du message MIME, validation |
| `src/EmailSender.*` | transport SMTP (libcurl), TLS, retries |
| `src/Template.*` | templates `{{var}}` et lecture CSV |
| `src/Config.*` | fichier de config et variables d'environnement |
| `src/main.cpp` | interface en ligne de commande |
| `tests/` | tests unitaires et d'intégration |
