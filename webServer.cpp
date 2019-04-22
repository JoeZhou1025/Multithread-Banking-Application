/* 
 * A simple web-server.  
 * 
 * The web-server performs the following tasks:
 * 
 *     1. Accepts connection from a client.
 *     2. Processes cgi-bin GET request.
 *     3. If not cgi-bin, it responds with the specific file or a 404.
 * 
 * Copyright (C) 2018 zhouz23@miamiOH.edu
 */

#include <ext/stdio_filebuf.h>
#include <unistd.h>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <iomanip>
#include <mutex>

// Using namespaces to streamline code below
using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;
using TcpStreamPtr = std::shared_ptr<tcp::iostream>;

std::mutex gate;
std::unordered_map<std::string, double> bank;

// Forward declaration for method defined further below
void serveClient(std::istream& is, std::ostream& os);

void threadMain(TcpStreamPtr client) {
    std::lock_guard<std::mutex> guard(gate);
    serveClient(*client, *client);
}

/**
 * Runs the program as a server that listens to incoming connections.
 * 
 * @param port The port number on which the server should listen.
 */
void runServer(int port) {
    io_service service;
    // Create end point
    tcp::endpoint myEndpoint(tcp::v4(), port);
    // Create a socket that accepts connections
    tcp::acceptor server(service, myEndpoint);
    std::cout << "Server is listening on " << port
            << " & ready to process clients...\n";
    //  Process client connections one-by-one...forever
        while (true) {
            TcpStreamPtr client = std::make_shared<tcp::iostream>();
            server.accept(*client->rdbuf());
            std::thread thr(threadMain, client);
            thr.detach();
        }
}

/**
 * This method is a convenience method that extracts file or command
 * from a string of the form: "GET <path> HTTP/1.1"
 * 
 * @param req The request from which the file path is to be extracted.
 * @return The path to the file requested
 */
//  std::string getCommand(const std::string req) {
//    const size_t cmdIndex = req.find("trans=");
//    const size_t acctIndex = req.find("&acct=");
//    const std::string cmd = req.substr(cmdIndex, acctIndex);
//    return cmd;
//  }

std::string getAcct(const std::string req) {
    const size_t acctIndex = req.find("&acct=");
    const size_t amountIndex = req.find("&amount=");
    const std::string acct = req.substr(acctIndex + 6,
            amountIndex - 6 - acctIndex);
    return acct;
}

std::string getAmount(const std::string req) {
    const size_t amountIndex = req.find("&amount=");
    const std::string amount = req.substr(amountIndex + 8, req.length()
            - 8 - amountIndex);
    return amount;
}

/**
 * Obtain the mime type of data based on file extension.
 * 
 * @param path The path from where the file extension is to be determined.
 * 
 * @return The mime type associated with the contents of the file.
 */

std::string reset() {
    bank.clear();
    return "All accounts reset";
}

std::string create(std::string acct) {
    auto search = bank.find(acct);
    if (search != bank.end()) {
        return "Account " + acct + " already exists";
    } else {
        bank.insert({acct, 0.00});
        return "Account " + acct + " created";
    }
    return "";
}

std::string credit(std::string acct, std::string amount) {
    auto search = bank.find(acct);
    if (search != bank.end()) {
        double amt = std::stod(amount) + bank[acct];
        bank[acct] = amt;
        return "Account balance updated";
    } else {
        return "Account not found";
    }
    return "";
}

std::string debit(std::string acct, std::string amount) {
    auto search = bank.find(acct);
    if (search != bank.end()) {
        double amt = bank[acct] - std::stod(amount);
        bank[acct] = amt;
        return "Account balance updated";
    } else {
        return "Account not found";
    }
    return "";
}

std::string status(std::string acct) {
    auto search = bank.find(acct);
    if (search != bank.end()) {
        string s = "Account " + acct + ": $";
        ostringstream os;
        os << std::fixed << std::setprecision(2) << bank[acct];
        s += os.str();
        return s;
    } else {
        return "Account not found";
    }
    return "";
}

/** Helper method to send the data to client in chunks.
    
    This method is a helper method that is used to send data to the
    client line-by-line.

    \param[in] mimeType The Mime Type to be included in the header.

    \param[in] pid An optional PID for the child process.  If it is
    -1, it is ignored.  Otherwise it is used to determine the exit
    code of the child process and send it back to the client.
 */
void sendData(string line, std::ostream& os) {
    // First write the fixed HTTP header.
    std::string result;
    if (line.substr(6, 5) == "reset") {
        result = reset();
    } else if (line.substr(6, 6) == "create") {
        result = create(getAcct(line));
    } else if (line.substr(6, 6) == "credit") {
        result = credit(getAcct(line), getAmount(line));
    } else if (line.substr(6, 5) == "debit") {
        result = debit(getAcct(line), getAmount(line));
    } else if (line.substr(6, 6) == "status") {
        result = status(getAcct(line));
    }
    os << "HTTP/1.1 200 OK\r\n"
            << "Server: BankServer\r\n"
            << "Content-Length: " + std::to_string(result.length())
            + "\r\n"
            << "Connection: Close\r\n"
            << "Content-Type: text/plain\r\n\r\n" 
            << result;
    // Read line-by line from child-process and write results to
    // client.
}

std::string getFilePath(const std::string& req) {
    size_t spc1 = req.find(' '), spc2 = req.rfind(' ');
    std::string path = req.substr(spc1 + 2, spc2 - spc1 - 2);
    return path;
}

/**
 * Process HTTP request (from first line & headers) and
 * provide suitable HTTP response back to the client.
 * 
 * @param is The input stream to read data from client.
 * @param os The output stream to send data to client.
 */
void serveClient(std::istream& is, std::ostream & os) {
    // Read headers from client and print them. This server
    // does not really process client headers
    std::string line;
    // Read the GET request line.
    std::getline(is, line);
    const std::string path = getFilePath(line);
    while (std::getline(is, line), line != "\r") {
    }
    // Send contents of the file to the client.
    sendData(path, os);
}

/*
 * The main method that performs the basic task of accepting connections
 * from the user.
 */
int main(int argc, char** argv) {
    if (argc == 2) {
        // Setup the port number for use by the server
        const int port = std::stoi(argv[1]);
        runServer(port);
    } else if (argc == 3) {
        // Process 1 request from specified file for functional testing
        std::ifstream input(argv[1]);
        std::ofstream output(argv[2]);
        serveClient(input, output);
    } else {
        std::cerr << "Invalid command-line arguments specified.\n";
    }
    return 0;
}

// End of source code
