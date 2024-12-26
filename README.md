# EmailSender

Pour créer un programme d'envoi automatique d'emails en C++, nous pouvons utiliser des bibliothèques qui facilitent l'envoi d'emails via des protocoles comme SMTP (Simple Mail Transfer Protocol). Une bibliothèque populaire pour cela est libcurl, qui est une bibliothèque client pour les protocoles HTTP, HTTPS, FTP, FTPS, SMTP, etc.

Voici un exemple de programme en C++ utilisant libcurl pour envoyer un email.

Prérequis
Installer libcurl : Assurez-vous que libcurl est installé sur votre système.
Configurer les informations d'authentification SMTP : Vous aurez besoin des informations de votre serveur SMTP (par exemple, serveur SMTP de Gmail, etc.).
Installation de libcurl
Suivez les instructions pour installer libcurl sur votre système. Sur une distribution Linux, vous pouvez installer libcurl avec :

sudo apt-get install libcurl4-openssl-dev

Explications
EmailSender Class:

Constructeur : Initialise les informations du serveur SMTP, le port, le nom d'utilisateur, et le mot de passe.
sendEmail : Utilise libcurl pour envoyer un email. Configure les options de curl pour le serveur SMTP, les informations d'authentification, le destinataire, le sujet, et le corps de l'email.
Main Function:

Configure les informations SMTP.
Crée une instance de EmailSender.
Envoie un email de test.
Compilation et Exécution
Assurez-vous que tous les fichiers sont dans le même répertoire, puis compilez et exécutez le programme avec :

g++ -o email_sender main.cpp EmailSender.cpp -lcurl
./email_sender

Note : Assurez-vous de remplacer les informations SMTP (serveur, port, nom d'utilisateur, et mot de passe) par vos propres informations. Pour des raisons de sécurité, il est recommandé de ne pas stocker les mots de passe en clair dans le code source. Vous pouvez utiliser des méthodes plus sécurisées pour gérer les informations sensibles.


