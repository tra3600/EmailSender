#ifndef EMAILSENDER_H
#define EMAILSENDER_H

#include <string>

class EmailSender {
public:
    EmailSender(const std::string& smtpServer, int port, const std::string& username, const std::string& password);
    bool sendEmail(const std::string& to, const std::string& subject, const std::string& body);

private:
    std::string smtpServer;
    int port;
    std::string username;
    std::string password;
};

#endif // EMAILSENDER_H