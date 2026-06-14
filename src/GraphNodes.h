#pragma once
#include "Vector.h"

class User {
   public:
    int indexID;
    Vector<int> connectedDevices;

    User(int id) {
        indexID = id;
    }
    ~User() {}
};

class Device {
   public:
    int indexID;
    Vector<int> connectedUsers;  // Truy vết ngược về User
    Vector<int> connectedApps;   // Truy vết tới Apps

    Device(int id) {
        indexID = id;
    }
    ~Device() {}
};

class App {
   public:
    int indexID;
    Vector<int> connectedDevices;    // Truy vết ngược về Device
    Vector<int> connectedResources;  // Truy vết tới Resource

    App(int id) {
        indexID = id;
    }
    ~App() {}
};

class Resource {
   public:
    int indexID;
    Vector<int> connectedApps;  // Truy vết ngược về App

    Resource(int id) {
        indexID = id;
    }
    ~Resource() {}
};
