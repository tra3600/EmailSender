#include "EmailSender.h"
#include <curl/curl.h>
#include <iostream>

EmailSender::EmailSender(const std::string& smtpServer, int port, const std::string& username, const std::string& password)
    : smtpServer(smtpServer), port(port), username(username), password(password) {}

bool EmailSender::sendEmail(const std::string& to, const std::string& subject, const std::string& body) {
    CURL* curl;
    CURLcode res = CURLE_OK;
    struct curl_slist* recipients = nullptr;
    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, smtpServer.c_str());
        curl_easy_setopt(curl, CURLOPT_PORT, port);
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, username.c_str());

        recipients = curl_slist_append(recipients, to.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        std::string payload = "To: " + to + "\r\n" +
                              "Subject: " + subject + "\r\n" +
                              "\r\n" + body + "\r\n";
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, nullptr);
        curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }

        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    }

    return (res == CURLE_OK);
}