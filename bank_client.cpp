/* 
 * A multithreaded client to test operations of a Banking web-server.
 *
 * This program generates concurrent requests to the Banking
 * web-server to test its operations.
 *
 * Copyright (C) 2018 raodm@miamiOH.edu
 */

#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <memory>
#include <fstream>
#include <iomanip>
#include <utility>
#include <vector>

// Setup a server socket to accept connections on the socket
using namespace boost::asio;
using namespace boost::asio::ip;

// A garbage-collected container to hold client I/O stream in threads
using TcpStreamPtr = std::shared_ptr<tcp::iostream>;

// A list of request & expected response pairs of strings
using ReqRespList = std::vector<std::pair<std::string, std::string>>;

/**
 * Helper method to extract the response message from the HTTP
 * response.
 */
std::string getResponse(std::istream& is) {
    std::string line;
    size_t contentLen = 0;  // set in loop below
    while (std::getline(is, line) && (line != "\r")) {
        // Process content-length header to get length of response
        if (line.substr(0, 16) == "Content-Length: ") {
            contentLen = std::stoi(line.substr(16));
        }
    }
    // Get the message at the end of the HTTP response.
    std::string msg;
    while (std::getline(is, line)) {
        msg += line;  // Accummulate response.
    }
    if (msg.size() != contentLen) {
        std::cerr << "Invalid content length reported by server!\n";
    }
    return msg;
}

/** Helper method to send a request to the server and process the
    response from the server.
*/
void processRequest(std::string port, const std::string& request,
                    const std::string& expectedResult) {
    ip::tcp::iostream server("localhost", port);
    if (!server) {
        // Can't connect to server
        std::cout << "Error connecting to server on port " << port << std::endl;
        return;
    }
    // First send the request to the server.
    server << "GET /" << request << " HTTP/1.1\r\n"
           << "Host: localhost:" << port << "\r\n"
           << "Connection: close\r\n\r\n";
    // Get the first line and ensure it is HTTP 200 OK.
    std::string line;
    if (std::getline(server, line), line != "HTTP/1.1 200 OK\r") {
        std::cerr << "Invalid header line from server!\n";
    } else {
        const std::string msg = getResponse(server);
        // Read rest of the message from the client.
        if (msg != expectedResult) {
            std::cerr << "Invalid msg from server. Expected: '"
                      << expectedResult << "' but got '"
                      << msg << "'\n";
        }
    }
}

/**
 * Run requests using 1 or more threads.
 */
void runRequests(const std::string& port, const ReqRespList& reqRespList,
                 const int numThreads) {
    // Submit requests to the server in batches of size 'numThreads'
    for (size_t stReq = 0; (stReq < reqRespList.size()); stReq += numThreads) {
        // Sumit a batch of requests to the server.
        std::vector<std::thread> thrList;
        for (int thr = 0; (thr < numThreads); thr++) {
            const int currReq = stReq + thr;  // Find index for this thread
            const std::string req  = reqRespList[currReq].first;
            const std::string resp = reqRespList[currReq].second;
            // Create a thread to submit request & validate response
            thrList.push_back(std::thread(processRequest, port, req, resp));
        }
        // Wait for all the threads to finish
        for (auto& t : thrList) { t.join(); }
    }
}

/**
 * Helper method to read line-by-line of transaction and expected
 * response and send it to server for testing.
 */
void processInputCmds(std::istream& input, const std::string& port) {
    // Read line-by-line of request-response pairs until a "run"
    // command is countered.
    std::string req, resp;   // request, response.
    ReqRespList testData;    // List of operations for testing.
    // Read line-by-line of input and process it.
    while (input >> std::quoted(req)) {
        if (req == "run") {
            // Load #threads and #repetitions from input
            int thrs, reps;
            input >> thrs >> reps;
            for (int rep = 0; (rep < reps); rep++) {
                runRequests(port, testData, thrs);
            }
            testData.clear();  // Clear out this batch of tests.
        } else {
            // Read response and add to pending list of tests.
            input >> std::quoted(resp);
            testData.push_back({req, resp});
        }
    }    
}

/**
 * The main method just checks to ensure necessary command-lien
 * arguments are specified and then 
 */
int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Specify InputFile and ServerPort\n";
        return 1;
    }
    // Open the input file to be used for testing.
    std::ifstream input(argv[1]);
    if (!input.good()) {
        std::cerr << "Unable to open input file: '" << argv[1] << ".\n";
        return 2;
    }
    // Have helper method process input commands from file
    processInputCmds(input, argv[2]);
    // All done.
    return 0;
}
