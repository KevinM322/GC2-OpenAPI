#include <fstream>
#include <sstream>
#include <winsock2.h>
#include <windows.h>
#include <ws2bth.h>
#include <chrono>
#include <bluetoothapis.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <ws2tcpip.h>
#include <sys/types.h>
#include <iomanip>
#include <map>
#include <limits>
#include <cmath> // For std::asin and std::sin

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bthprops.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

std::atomic<bool> keepRunning(true);

std::string createJsonString(int shotNumber, float speed, float spinAxis, float totalSpin, float backSpin, float sideSpin, float hla, float vla, float carryDistance) {
    std::ostringstream jsonStream;

    jsonStream << std::fixed << std::setprecision(2); // Set precision for floating-point numbers
    jsonStream << R"({"DeviceID": "GC2", "Units": "Yards", "ShotNumber": )" << shotNumber
        << R"(, "APIversion": "1", "BallData": { "Speed": )" << speed
        << R"(, "SpinAxis": )" << spinAxis
        << R"(, "TotalSpin": )" << totalSpin
        << R"(, "BackSpin": )" << backSpin
        << R"(, "SideSpin": )" << sideSpin
        << R"(, "HLA": )" << hla
        << R"(, "VLA": )" << vla
        << R"(, "CarryDistance": )" << carryDistance
        << R"(}, "ClubData": { "Speed": 0.0, "AngleOfAttack": 0.0, "FaceToTarget": 0.0, "Lie": 0.0, "Loft": 0.0, "Path": 0.0, "SpeedAtImpact": 0.0, "VerticalFaceImpact": 0.0, "HorizontalFaceImpact": 0.0, "ClosureRate": 0.0}, "ShotDataOptions": { "ContainsBallData": true, "ContainsClubData": false, "LaunchMonitorIsReady": true, "LaunchMonitorBallDetected": true, "IsHeartBeat": false}})";

    return jsonStream.str();
}
// Utility function to extract values from the event string
float ExtractValue(const std::string& message, const std::string& key) {
    size_t keyPos = message.find(key + "=");
    if (keyPos != std::string::npos) {
        size_t valueStart = keyPos + key.length() + 1;
        size_t valueEnd = message.find(',', valueStart);
        std::string valueStr = message.substr(valueStart, valueEnd - valueStart);
        return std::stof(valueStr);
    }
    return 0.0f; // Default to 0 if not found
}


int ExtractTM(const std::string& message) {
    std::size_t tmStart = message.find("TM=");
    if (tmStart != std::string::npos) {
        std::size_t tmEnd = message.find(',', tmStart);
        if (tmEnd != std::string::npos) {
            std::string tmValue = message.substr(tmStart + 3, tmEnd - tmStart - 3);
            try {
                return std::stoi(tmValue);
            }
            catch (const std::invalid_argument& ia) {
                std::cerr << "Invalid argument: " << ia.what() << '\n';
            }
            catch (const std::out_of_range& oor) {
                std::cerr << "Out of Range error: " << oor.what() << '\n';
            }
        }
    }
    return -1; // Return -1 if "TM" is not found or on error
}

SOCKET ConnectToBluetoothDevice() {
    while (keepRunning) {
        BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = { 0 };
        searchParams.dwSize = sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS);
        searchParams.fReturnAuthenticated = TRUE;
        searchParams.fReturnRemembered = FALSE;
        searchParams.fReturnUnknown = TRUE;
        searchParams.fReturnConnected = TRUE;
        searchParams.fIssueInquiry = TRUE;
        searchParams.cTimeoutMultiplier = 2;

        SOCKET btSocket = INVALID_SOCKET;

        BLUETOOTH_DEVICE_INFO deviceInfo = { 0 };
        deviceInfo.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);

        HBLUETOOTH_DEVICE_FIND deviceFindHandle = BluetoothFindFirstDevice(&searchParams, &deviceInfo);
        if (deviceFindHandle == NULL) {
            std::cout << "No devices found." << std::endl;
            return btSocket;
        }

        bool deviceConnected = false;

        do {
            if (wcsstr(deviceInfo.szName, L"Foresight_GC2")) {
                std::wcout << L"Found device: " << deviceInfo.szName << std::endl;

                WSADATA wsaData;
                SOCKADDR_BTH sa = { 0 };

                // Initialize Winsock
                if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                    std::cout << "WSAStartup failed" << std::endl;
                    break;
                }

                // Create Bluetooth socket
                btSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
                if (btSocket == INVALID_SOCKET) {
                    std::cout << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
                    WSACleanup();
                    break;
                }

                // Prepare the sockaddr structure for the connection
                sa.addressFamily = AF_BTH;
                sa.btAddr = deviceInfo.Address.ullLong;
                sa.serviceClassId = SerialPortServiceClass_UUID;
                sa.port = BT_PORT_ANY;

                // Connect to the device
                if (connect(btSocket, (SOCKADDR*)&sa, sizeof(sa)) == SOCKET_ERROR) {
                    std::cout << "Connect failed with error: " << WSAGetLastError() << std::endl;
                    closesocket(btSocket);
                    WSACleanup();
                    btSocket = INVALID_SOCKET;
                    break;
                }

                deviceConnected = TRUE;
            }
        } while (BluetoothFindNextDevice(deviceFindHandle, &deviceInfo) && !deviceConnected);

        BluetoothFindDeviceClose(deviceFindHandle);

        if (deviceConnected) {
            std::cout << "Connected to GC2 successfully." << std::endl;
            return btSocket;
        }
        else {
            std::cout << "Retrying Bluetooth connection..." << std::endl;
            return INVALID_SOCKET;
        }
    }
    return INVALID_SOCKET;
}

SOCKET ConnectToTCPServer(const std::string& ipAddress, int port) {
    WSADATA wsaData;
    SOCKET tcpSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return INVALID_SOCKET;
    }

    tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcpSocket == INVALID_SOCKET) {
        std::cerr << "Error creating TCP socket." << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    // Use inet_pton instead of inet_addr
    if (inet_pton(AF_INET, ipAddress.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address / Address not supported." << std::endl;
        closesocket(tcpSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

    if (connect(tcpSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "TCP Connection Failed." << std::endl;
        closesocket(tcpSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

    std::cout << "Connected to Open API." << std::endl;

    return tcpSocket;
}


void MonitorAndForward(SOCKET bluetoothSocket, SOCKET tcpSocket) {
    int prevTM = -1; // Store the previous "TM" value

    bool detectedFirstShot = FALSE;
    
    // Set both sockets to non-blocking mode
    u_long mode = 1; // 1 to enable non-blocking socket
    ioctlsocket(bluetoothSocket, FIONBIO, &mode);
    ioctlsocket(tcpSocket, FIONBIO, &mode); 
    
    char buffer[1024];
    fd_set readfds;
    int result;
    struct timeval tv;
    bool firstTCPError = TRUE;

    std::string targetIP;

    targetIP = "127.0.0.1";

    std::ofstream outFile("shots.dat", std::ios::app); // Open file for appending

    if (!outFile) {
        std::cerr << "Failed to open shots.dat for writing." << std::endl;
    }

    while (keepRunning) {
        FD_ZERO(&readfds);
        FD_SET(bluetoothSocket, &readfds);
        FD_SET(tcpSocket, &readfds);

        // Set timeout to 1 second
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        result = select(0, &readfds, NULL, NULL, &tv);
        if (result > 0) {
            // Check if the Bluetooth socket is ready to be read
            if (FD_ISSET(bluetoothSocket, &readfds)) {
                memset(buffer, 0, sizeof(buffer));
                int bytesReceived = recv(bluetoothSocket, buffer, sizeof(buffer), 0);
                if (bytesReceived > 0) {
                    // commenting out reporting every output std::cout << "Bluetooth: " << std::string(buffer, bytesReceived) << std::endl;
                    std::string message(buffer, bytesReceived);

                    int currentTM = ExtractTM(message); // Assume ExtractTM function exists
                    if (currentTM != prevTM && currentTM != -1) {
                        // For debug
                        // outFile << message << std::endl; // Save to file
                        
                        int shotNumber = ExtractValue(message, "ID");
                        float speed = ExtractValue(message, "SP");
                        float totalSpin = ExtractValue(message, "TS");
                        float backSpin = ExtractValue(message, "BS");
                        float sideSpin = ExtractValue(message, "SS");
                        float spinAxis = 0;
                        if (totalSpin != 0) {
                            spinAxis = std::asin(sideSpin / totalSpin) * 180.0 / M_PI; // Convert to degrees
                        }
                        float hla = ExtractValue(message, "AZ");
                        float vla = ExtractValue(message, "EL");
                        float carryDistance = ExtractValue(message, "CY");

                        auto now = std::chrono::system_clock::now();
                        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
                        std::tm now_tm;
                        localtime_s(&now_tm,&now_c);

                        if (detectedFirstShot == TRUE) {
                            
                            std::string jsonString = createJsonString(shotNumber, speed, spinAxis, totalSpin, backSpin, sideSpin, hla, vla, carryDistance);

                            if (speed == 0) {
                                std::cout << "Speed detected as zero. Ignoring misread. " << std::endl;
                            }
                            else {
                                // Format data string for file (CSV format)
                                std::ostringstream fileStream;
                                fileStream << std::put_time(&now_tm, "Date,%Y-%m-%d,Time,\"%I:%M:%S %p\","); // Date and Time
                                fileStream << std::fixed << std::setprecision(2);
                                fileStream << "Timestamp," << currentTM << ",Speed," << speed << ",Azimuth," << hla << ",Elevation," << vla
                                    << ",\"Total spin\"," << totalSpin << ",\"Spin axis\"," << spinAxis << ",\"Side spin\","
                                    << sideSpin << ",\"Back spin\"," << backSpin << ",Carry," << carryDistance;

                                // Save to file
                                outFile << fileStream.str() << std::endl;
                                std::cout << "Shot detected: " << "Time " << currentTM << ", Speed " << speed << ", Azimuth " << hla << ", Elevation " << vla
                                    << ", Total spin " << totalSpin << ", Spin axis " << spinAxis << ", Side spin "
                                    << sideSpin << ", Back spin " << backSpin << ", Carry " << carryDistance << std::endl;

                                // Send to TCP socket
                                if (send(tcpSocket, jsonString.c_str(), jsonString.length(), 0) == SOCKET_ERROR) {
                                    if (firstTCPError == TRUE) {
                                        std::cerr << "Failed to send data over TCP. Error: " << WSAGetLastError() << ". Retrying." << std::endl;
                                        firstTCPError = FALSE;
                                    }

                                    closesocket(tcpSocket);
                                    tcpSocket = INVALID_SOCKET;
                                    tcpSocket = ConnectToTCPServer(targetIP, 921);
                                }
                                else {
                                    firstTCPError = TRUE;
                                }
                            }
                        }
                        else {
                            // Ignoring first shot
                            std::cout << "Ready." << std::endl;
                            detectedFirstShot = TRUE;
                        }
                        prevTM = currentTM; // Update previous "TM" value
                    }
                }
                else if (bytesReceived <= 0) {
                    std::cerr << "Bluetooth connection lost or error occurred. Retrying..." << std::endl;
                    closesocket(bluetoothSocket);
                    bluetoothSocket = INVALID_SOCKET;
                    bluetoothSocket = ConnectToBluetoothDevice();
                }
            }

            // Check if the TCP socket is ready to be read
            if (FD_ISSET(tcpSocket, &readfds)) {
                memset(buffer, 0, sizeof(buffer));
                int bytesReceived = recv(tcpSocket, buffer, sizeof(buffer), 0);
                if (bytesReceived > 0) {
                    // Ignore output from TCP connection
                    // std::cout << "TCP: " << std::string(buffer, bytesReceived) << std::endl;
                }
                else if (bytesReceived <= 0) {
                    std::cerr << "TCP connection lost or error occurred. Retrying..." << std::endl;
                    closesocket(tcpSocket);
                    tcpSocket = INVALID_SOCKET;
                    tcpSocket = ConnectToTCPServer(targetIP, 921);
                }
            }
        } else if (result == 0) {
            // Timeout with no data, loop continues
        }
        else {
            // Select error
            if (bluetoothSocket != INVALID_SOCKET) {
                std::cerr << "Select call failed with error: " << WSAGetLastError() << std::endl;
                closesocket(bluetoothSocket);
                bluetoothSocket = INVALID_SOCKET;
            }
                
            bluetoothSocket = ConnectToBluetoothDevice();
        }

        if (GetAsyncKeyState('q') & 0x8000 || GetAsyncKeyState('Q') & 0x8000) {
            keepRunning = FALSE;
        }

        if (GetAsyncKeyState('i') & 0x8000 || GetAsyncKeyState('I') & 0x8000) {
            std::cout << "Enter target IP address. Default is 127.0.0.1. ";
            
            std::getline(std::cin, targetIP);
            closesocket(tcpSocket);
            tcpSocket = INVALID_SOCKET;
            tcpSocket = ConnectToTCPServer(targetIP, 921);
        }
    }
}


int main() {
    std::cout << "Press 'q' to quit." << std::endl
        << "Press 'i' to enter alternate IP address. The IP address should be the computer that is running GS Pro. The default IP address is the local computer (127.0.0.1)." << std::endl
        << "Press '+' / '-' to select a club. The club will be saved in the shots.dat file but is not sent to the simulator software." << std::endl
        << "(Due to a bug in how the program processes input, try holding down the button to provide input. For example, hold down 'i' until the prompt to change IP address comes up.)" << std::endl
        << "To use with GS Pro, connect using Open API. The order of when you open everything should not matter." << std::endl;
    SOCKET bluetoothSocket = ConnectToBluetoothDevice();
    SOCKET tcpSocket = ConnectToTCPServer("127.0.0.1", 921);

    std::thread forwardThread(MonitorAndForward, bluetoothSocket, tcpSocket);
    forwardThread.join();
    
    // Cleanup
    if (bluetoothSocket != INVALID_SOCKET) closesocket(bluetoothSocket);
    if (tcpSocket != INVALID_SOCKET) closesocket(tcpSocket);
    WSACleanup();

    return 0;
}
