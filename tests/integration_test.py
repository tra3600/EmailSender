#!/usr/bin/env python3
"""Test d'intégration : lance un faux serveur SMTP local et pilote le binaire emailsender."""
import base64
import email
import os
import socketserver
import subprocess
import sys
import tempfile
import threading
from email import policy

CLI = sys.argv[1]
USER, PASSWORD = "bot@example.com", "s3cret"


class State:
    messages = []          # (mail_from, [rcpts], raw bytes)
    auth_ok = []
    fail_first_data = 0    # nombre de réponses 451 à renvoyer avant d'accepter
    reject_rcpt = None     # adresse rejetée en 550


class Handler(socketserver.StreamRequestHandler):
    def send(self, line):
        self.wfile.write((line + "\r\n").encode())

    def handle(self):
        self.send("220 fake ESMTP")
        mail_from, rcpts = None, []
        while True:
            line = self.rfile.readline()
            if not line:
                return
            cmd = line.decode().strip()
            up = cmd.upper()
            if up.startswith("EHLO"):
                self.wfile.write(b"250-fake\r\n250-AUTH PLAIN LOGIN\r\n250 8BITMIME\r\n")
            elif up.startswith("AUTH PLAIN"):
                tokens = cmd.split()
                if len(tokens) < 3:  # curl envoie d'abord "AUTH PLAIN" puis attend 334
                    self.send("334 ")
                    tokens = ["AUTH", "PLAIN", self.rfile.readline().decode().strip()]
                parts = base64.b64decode(tokens[2]).split(b"\0")
                ok = parts[-2:] == [USER.encode(), PASSWORD.encode()]
                State.auth_ok.append(ok)
                self.send("235 ok" if ok else "535 bad credentials")
            elif up.startswith("AUTH LOGIN"):
                self.send("334 VXNlcm5hbWU6")
                u = base64.b64decode(self.rfile.readline().strip()).decode()
                self.send("334 UGFzc3dvcmQ6")
                p = base64.b64decode(self.rfile.readline().strip()).decode()
                ok = (u, p) == (USER, PASSWORD)
                State.auth_ok.append(ok)
                self.send("235 ok" if ok else "535 bad credentials")
            elif up.startswith("MAIL FROM"):
                mail_from = cmd[10:].strip().strip("<>").split(">")[0]
                rcpts = []
                self.send("250 ok")
            elif up.startswith("RCPT TO"):
                r = cmd[8:].strip().strip("<>").split(">")[0]
                if r == State.reject_rcpt:
                    self.send("550 no such user")
                else:
                    rcpts.append(r)
                    self.send("250 ok")
            elif up == "DATA":
                if State.fail_first_data > 0:
                    State.fail_first_data -= 1
                    self.send("451 try again later")
                    continue
                self.send("354 go")
                data = b""
                while True:
                    l = self.rfile.readline()
                    if l == b".\r\n":
                        break
                    data += l[1:] if l.startswith(b"..") else l
                State.messages.append((mail_from, rcpts, data))
                self.send("250 queued")
            elif up == "RSET":
                self.send("250 ok")
            elif up == "NOOP":
                self.send("250 ok")
            elif up == "QUIT":
                self.send("221 bye")
                return
            else:
                self.send("502 unsupported")


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


server = Server(("127.0.0.1", 0), Handler)
port = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()

tmp = tempfile.mkdtemp()
failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)
        print("ÉCHEC :", msg)


def run(*args, stdin=None):
    cmd = [CLI, "-s", "127.0.0.1", "-p", str(port), "--security", "none", "-u", USER,
           "--password-stdin" if stdin is None else "--password-stdin", "-f", "Bot <bot@example.com>",
           "--retries", "2"] + list(args)
    env = dict(os.environ, EMAILSENDER_CONFIG="")
    return subprocess.run(cmd, input=(PASSWORD + "\n"), capture_output=True, text=True, env=env, timeout=60)


def reset():
    State.messages.clear()
    State.auth_ok.clear()
    State.fail_first_data = 0
    State.reject_rcpt = None


# 1. test de connexion
r = run("--test-connection")
check(r.returncode == 0, "test-connection : " + r.stderr)

# 2. mauvais mot de passe
bad = subprocess.run([CLI, "-s", "127.0.0.1", "-p", str(port), "--security", "none", "-u", USER,
                      "--password-stdin", "--test-connection"], input="wrong\n", capture_output=True, text=True,
                     env=dict(os.environ, EMAILSENDER_CONFIG=""))
check(bad.returncode == 1, "mauvais mot de passe doit échouer")

# 3. message complet : HTML + texte auto, pièce jointe, inline, cc, bcc, accents
att = os.path.join(tmp, "rapport.txt")
open(att, "w").write("contenu du rapport\n")
img = os.path.join(tmp, "logo.png")
open(img, "wb").write(b"\x89PNG\r\n\x1a\nFAKE")
reset()
r = run("-t", "Zoé <zoe@example.com>, bob@example.com", "--cc", "carol@example.com", "--bcc", "hidden@example.com",
        "-S", "Résumé du été", "--html", '<h1>Bonjour</h1><p>Voir <img src="cid:logo"> &amp; fin</p>',
        "-a", att, "--inline", img + ":logo", "--priority", "high", "--header", "X-Test: oui")
check(r.returncode == 0, "envoi complet : " + r.stderr)
check(len(State.messages) == 1, "un message reçu")
if State.messages:
    frm, rc, raw = State.messages[0]
    check(frm == "bot@example.com", "MAIL FROM = " + frm)
    check(sorted(rc) == ["bob@example.com", "carol@example.com", "hidden@example.com", "zoe@example.com"], "RCPT = %s" % rc)
    msg = email.message_from_bytes(raw, policy=policy.default)
    check(msg["Subject"] == "Résumé du été", "Subject = %r" % msg["Subject"])
    check("hidden@example.com" not in raw.decode("utf-8", "replace"), "Bcc ne doit pas fuiter")
    check(msg["X-Test"] == "oui", "en-tête perso")
    check(msg["X-Priority"].startswith("1"), "priorité")
    check(msg["Date"] and msg["Message-ID"], "Date et Message-ID")
    check(msg.get_body(("plain",)).get_content().strip().startswith("Bonjour"), "texte auto")
    check("Bonjour" in msg.get_body(("html",)).get_content(), "html")
    names = [p.get_filename() for p in msg.iter_attachments()]
    check("rapport.txt" in names, "pièce jointe, noms = %s" % names)
    related = [p for p in msg.walk() if p.get_content_type() == "image/png"]
    check(related and related[0]["Content-ID"] == "<logo>", "image inline")
    check(related and related[0].get_content() == b"\x89PNG\r\n\x1a\nFAKE", "contenu binaire intact")

# 4. ligne commençant par un point (dot-stuffing) et corps depuis stdin-file
reset()
bodyf = os.path.join(tmp, "body.txt")
open(bodyf, "w").write("ligne1\n.point seul\n..deux\n")
r = run("-t", "x@example.com", "-S", "dots", "--body-file", bodyf)
check(r.returncode == 0, "dot-stuffing : " + r.stderr)
if State.messages:
    body = email.message_from_bytes(State.messages[0][2], policy=policy.default).get_content()
    check(body == "ligne1\r\n.point seul\r\n..deux\r\n" or body == "ligne1\n.point seul\n..deux\n", "corps = %r" % body)

# 5. retry sur 451
reset()
State.fail_first_data = 1
r = run("-t", "x@example.com", "-S", "retry", "-b", "x", "--retries", "3")
check(r.returncode == 0 and len(State.messages) == 1, "retry après 451 : " + r.stderr)

# 6. rejet permanent 550
reset()
State.reject_rcpt = "nobody@example.com"
r = run("-t", "nobody@example.com", "-S", "x", "-b", "x")
check(r.returncode == 1 and not State.messages, "550 => échec exit 1")

# 7. batch avec variables et rapport
reset()
csv = os.path.join(tmp, "dest.csv")
open(csv, "w", encoding="utf-8").write("email;nom;ville\na@example.com;Anaïs;\nb@example.com;Bruno;Lyon\n")
rep = os.path.join(tmp, "report.csv")
r = run("--batch", csv, "-S", "Salut {{nom}}", "--html", "<p>{{nom}} de {{ville|nulle part}}</p>", "--report", rep)
check(r.returncode == 0 and len(State.messages) == 2, "batch : " + r.stderr)
if len(State.messages) == 2:
    m0 = email.message_from_bytes(State.messages[0][2], policy=policy.default)
    m1 = email.message_from_bytes(State.messages[1][2], policy=policy.default)
    check(m0["Subject"] == "Salut Anaïs", "sujet 1 = %r" % m0["Subject"])
    check("nulle part" in m0.get_body(("html",)).get_content(), "valeur par défaut")
    check("Lyon" in m1.get_body(("html",)).get_content(), "variable ville")
check(os.path.exists(rep) and open(rep).read().count("sent") == 2, "rapport CSV")

# 8. dry-run : aucun envoi + sauvegarde .eml
reset()
eml = os.path.join(tmp, "out.eml")
r = run("-t", "x@example.com", "-S", "dry", "-b", "corps", "--dry-run", "--save-eml", eml)
check(r.returncode == 0 and not State.messages and "Subject: dry" in r.stdout, "dry-run")
check(os.path.exists(eml) and b"Subject: dry" in open(eml, "rb").read(), "save-eml")

# 9. erreurs d'usage
r = run("-S", "x", "-b", "y")
check(r.returncode == 2, "sans destinataire => 2")
r = run("-t", "pas-une-adresse", "-S", "x", "-b", "y")
check(r.returncode == 2, "adresse invalide => 2")
r = run("-t", "x@example.com", "-S", "x\nBcc: evil@example.com", "-b", "y")
check(r.returncode == 1 and not State.messages, "injection d'en-tête refusée")

# 10. fichier de configuration
reset()
conf = os.path.join(tmp, "c.conf")
open(conf, "w").write(f"server=127.0.0.1\nport={port}\nsecurity=none\nusername={USER}\npassword={PASSWORD}\nfrom=Cfg <cfg@example.com>\n")
os.chmod(conf, 0o600)
r = subprocess.run([CLI, "-c", conf, "-t", "x@example.com", "-S", "cfg", "-b", "y"], capture_output=True, text=True)
check(r.returncode == 0 and State.messages and State.messages[0][0] == "cfg@example.com", "config : " + r.stderr)

server.shutdown()
if failures:
    print(len(failures), "échec(s)")
    sys.exit(1)
print("Tests d'intégration SMTP : OK")
