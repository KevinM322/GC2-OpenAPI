#include <winsock2.h>
#include <windows.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <iostream>
#include <string>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bthprops.lib")

// Utility function to convert Bluetooth address to SOCKADDR_BTH
SOCKADDR_BTH GetBluetoothSocketAddress(ULONGLONG bluetoothAddress) {
    SOCKADDR_BTH socketAddress = { 0 };
    socketAddress.addressFamily = AF_BTH;
    socketAddress.btAddr = bluetoothAddress;
    socketAddress.serviceClassId = SerialPortServiceClass_UUID;
    socketAddress.port = BT_PORT_ANY;
    return socketAddress;
}

SOCKET ConnectToDevice(BLUETOOTH_DEVICE_INFO& deviceInfo) {
    WSADATA wsaData;
    SOCKET btSocket = INVALID_SOCKET;
    SOCKADDR_BTH sa = { 0 };

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "WSAStartup failed" << std::endl;
        return INVALID_SOCKET;
    }

    // Create Bluetooth socket
    btSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (btSocket == INVALID_SOCKET) {
        std::cout << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
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
        return INVALID_SOCKET;
    }

    std::cout << "Connected successfully." << std::endl;

    return btSocket; // Return the socket descriptor
}

void ListenForMessages(SOCKET connectedSocket) {
    const int bufferSize = 1024;
    char buffer[bufferSize];
    int bytesReceived;

    // Check if the socket is valid
    if (connectedSocket == INVALID_SOCKET) {
        std::cout << "Invalid socket. Cannot listen for messages." << std::endl;
        return;
    }

    std::cout << "Listening for messages..." << std::endl;

    while (true) {
        // Clear the buffer
        memset(buffer, 0, bufferSize);

        // Receive data from the device
        bytesReceived = recv(connectedSocket, buffer, bufferSize, 0);

        if (bytesReceived > 0) {
            // Successfully received data
            std::cout << "Received message: " << std::string(buffer, 0, bytesReceived) << std::endl;
        }
        else if (bytesReceived == 0) {
            // Connection is closed
            std::cout << "Connection closed." << std::endl;
            break;
        }
        else {
            // Error occurred
            std::cout << "recv failed with error: " << WSAGetLastError() << std::endl;
            break;
        }
    }

    // Cleanup
    closesocket(connectedSocket);
    WSACleanup();
}


void SearchAndConnect() {
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
        return;
    }

    bool deviceConnected = false;

    do {
        if (wcsstr(deviceInfo.szName, L"GC2")) {
            std::wcout << L"Found device: " << deviceInfo.szName << std::endl;

            btSocket = ConnectToDevice(deviceInfo);

            if (btSocket != INVALID_SOCKET) {
                std::cout << "Device connected successfully." << std::endl;
                deviceConnected = true;
                break; // Exit after connecting to the first matching device
            }
            else {
                std::cout << "Failed to connect to device." << std::endl;
            }
        }
    } while (BluetoothFindNextDevice(deviceFindHandle, &deviceInfo) && !deviceConnected);

    BluetoothFindDeviceClose(deviceFindHandle);

    if (deviceConnected) {
        // Listen for messages from the connected device
        ListenForMessages(btSocket);
    }
}

int main() {
    SearchAndConnect();
    return 0;
}
