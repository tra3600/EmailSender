#include <iostream>
#include "EmailSender.h"

int main() {
    // Configuration SMTP
    std::string smtpServer = "smtp.gmail.com";
    int port = 587;
    std::string username = "your-email@gmail.com";
    std::string password = "your-password";

    // Instanciation de l'EmailSender
    EmailSender emailSender(smtpServer, port, username, password);

    // Envoi de l'email
    std::string to = "recipient@example.com";
    std::string subject = "Test Email";
    std::string body = "This is a test email sent from a C++ program.";

    if (emailSender.sendEmail(to, subject, body)) {
        std::cout << "Email sent successfully!" << std::endl;
    } else {
        std::cerr << "Failed to send email." << std::endl;
    }

    return 0;
}