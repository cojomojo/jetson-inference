/*
    Copyright (C) Antles Pollen Supplies, Inc - All Rights Reserved
    Unauthorized copying of this file, via any medium is strictly prohibited
    Proprietary and confidential

    Camera.hpp: Camera class header file
    Author: Cody Balos <cjbalos@gmail.com>, February 2018
*/

#ifndef CAMERA_API_CAMERA_NODE_H
#define CAMERA_API_CAMERA_NODE_H

#include <string>

class CameraNode
{

public:
    CameraNode(std::string serialNumber, std::string description = "no description"):
        serialNumber(serialNumber), description(description) { };

    std::string serialNumber;
    std::string description;

};

#endif
