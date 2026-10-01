#include "EmailSender.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

std::once_flag gCurlInit;

struct Upload {
    const std::string* data;
    size_t pos;
};

size_t readCallback(char* ptr, size_t size, size_t nmemb, void* userp) {
    auto* up = static_cast<Upload*>(userp);
    size_t room = size * nmemb;
    size_t n = std::min(room, up->data->size() - up->pos);
    if (n) {
        std::memcpy(ptr, up->data->data() + up->pos, n);
        up->pos += n;
    }
    return n;
}

bool isTransient(CURLcode code, long smtp) {
    if (smtp >= 400 && smtp < 500) return true;
    if (smtp >= 500) return false;
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
            return true;
        default:
            return false;
    }
}

std::string wrap(const std::string& addr) { return "<" + addr + ">"; }

struct Attempt {
    CURLcode code;
    long smtp;
    std::string error;
};

// Un unique essai : connexion + (optionnellement) transaction SMTP.
Attempt attempt(const SmtpConfig& cfg, const BuiltMessage* msg) {
    Attempt a{CURLE_FAILED_INIT, 0, "initialisation de libcurl impossible"};
    CURL* curl = curl_easy_init();
    if (!curl) return a;

    Security sec = cfg.security;
    if (sec == Security::Auto) sec = cfg.port == 465 ? Security::Ssl : Security::StartTls;
    bool tryTls = cfg.security == Security::Auto && cfg.port != 465 && cfg.port != 587;

    std::string url = (sec == Security::Ssl ? "smtps://" : "smtp://") + cfg.server + ":" + std::to_string(cfg.port);
    if (!cfg.heloDomain.empty()) url += "/" + cfg.heloDomain;

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, cfg.connectTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, cfg.verbose ? 1L : 0L);
    if (!cfg.username.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg.password.c_str());
    }
    if (sec == Security::StartTls)
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)(tryTls ? CURLUSESSL_TRY : CURLUSESSL_ALL));
    if (sec != Security::None) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cfg.verifyPeer ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, cfg.verifyHost ? 2L : 0L);
        if (!cfg.caBundle.empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, cfg.caBundle.c_str());
    }

    struct curl_slist* rcpts = nullptr;
    Upload up{nullptr, 0};
    std::string from;
    if (msg) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, cfg.timeoutSec);
        from = wrap(msg->envelopeFrom);
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());
        for (const auto& r : msg->recipients) rcpts = curl_slist_append(rcpts, wrap(r).c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpts);
        up.data = &msg->data;
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &up);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)msg->data.size());
    } else {
        curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
    }

    a.code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &a.smtp);
    a.error = errbuf[0] ? errbuf : curl_easy_strerror(a.code);
    curl_slist_free_all(rcpts);
    curl_easy_cleanup(curl);
    return a;
}

SendResult run(const SmtpConfig& cfg, const BuiltMessage* msg) {
    SendResult r;
    if (msg) r.messageId = msg->messageId;
    for (int n = 0;; ++n) {
        ++r.attempts;
        Attempt a = attempt(cfg, msg);
        r.smtpCode = a.smtp;
        if (a.code == CURLE_OK) {
            r.ok = true;
            r.error.clear();
            return r;
        }
        r.error = a.error + " (curl " + std::to_string((int)a.code) +
                  (a.smtp ? ", SMTP " + std::to_string(a.smtp) : std::string()) + ")";
        if (n >= cfg.maxRetries || !isTransient(a.code, a.smtp)) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.retryDelayMs << std::min(n, 10)));
    }
}

}  // namespace

bool parseSecurity(const std::string& text, Security& out) {
    std::string t;
    for (unsigned char c : text) t += (char)std::tolower(c);
    if (t == "auto") out = Security::Auto;
    else if (t == "none" || t == "no" || t == "plain") out = Security::None;
    else if (t == "starttls" || t == "tls") out = Security::StartTls;
    else if (t == "ssl" || t == "smtps") out = Security::Ssl;
    else return false;
    return true;
}

const char* securityName(Security s) {
    switch (s) {
        case Security::None: return "none";
        case Security::StartTls: return "starttls";
        case Security::Ssl: return "ssl";
        default: return "auto";
    }
}

EmailSender::EmailSender(SmtpConfig config) : config_(std::move(config)) {
    std::call_once(gCurlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

EmailSender::EmailSender(const std::string& smtpServer, int port, const std::string& username,
                         const std::string& password)
    : EmailSender([&] {
          SmtpConfig c;
          c.server = smtpServer;
          c.port = port;
          c.username = username;
          c.password = password;
          return c;
      }()) {}

SendResult EmailSender::sendRaw(const BuiltMessage& message) {
    SendResult r = run(config_, &message);
    lastError_ = r.error;
    return r;
}

SendResult EmailSender::send(const Email& email) {
    try {
        return sendRaw(buildMessage(email));
    } catch (const std::exception& e) {
        SendResult r;
        r.error = e.what();
        lastError_ = r.error;
        return r;
    }
}

bool EmailSender::sendEmail(const std::string& to, const std::string& subject, const std::string& body) {
    Email e;
    std::string err;
    Address::parse(config_.username, e.from, &err);
    Address rcpt;
    if (!Address::parse(to, rcpt, &err)) {
        lastError_ = err;
        std::cerr << "Destinataire invalide : " << err << std::endl;
        return false;
    }
    e.to.push_back(rcpt);
    e.subject = subject;
    e.textBody = body;
    SendResult r = send(e);
    if (!r.ok) std::cerr << "Échec de l'envoi : " << r.error << std::endl;
    return r.ok;
}

SendResult EmailSender::testConnection() {
    SendResult r = run(config_, nullptr);
    lastError_ = r.error;
    return r;
}
