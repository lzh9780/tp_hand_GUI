#include "UrdfLoader.h"
#include <tinyxml2.h>
#include <sstream>
#include <cmath>
#include <iostream>

using namespace tinyxml2;

// Utility function to parse XYZ coordinates
std::array<float, 3> UrdfLoader::parseXYZ(const char *s)
{
    std::array<float, 3> v{0.0f, 0.0f, 0.0f};
    if (!s)
        return v;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    sscanf(s, "%f %f %f", &x, &y, &z);
    v = {x, y, z};
    return v;
}

// Utility function to parse Roll-Pitch-Yaw (RPY) angles
std::array<float, 3> UrdfLoader::parseRPY(const char *s)
{
    return parseXYZ(s);
}

// Parse joint type from string (e.g., revolute, prismatic)
JointType UrdfLoader::parseJointType(const char *type)
{
    if (!type)
        return JointType::Fixed;
    std::string jointType(type);
    if (jointType == "revolute")
        return JointType::Revolute;
    if (jointType == "prismatic")
        return JointType::Prismatic;
    if (jointType == "continuous")
        return JointType::Continuous;
    if (jointType == "fixed")
        return JointType::Fixed;
    return JointType::Fixed;
}

// Parse geometry type (e.g., box, cylinder, mesh)
GeometryType UrdfLoader::parseGeometryType(const char *type)
{
    if (type == nullptr)
        return GeometryType::Box;
    std::string geometryType(type);
    if (geometryType == "box")
        return GeometryType::Box;
    if (geometryType == "cylinder")
        return GeometryType::Cylinder;
    if (geometryType == "sphere")
        return GeometryType::Sphere;
    if (geometryType == "mesh")
        return GeometryType::Mesh;
    return GeometryType::Box;
}

// Loads a URDF file and fills the model structure
bool UrdfLoader::loadFile(const std::string &path, UrdfModel &out)
{
    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS)
        return false;
    auto *robot = doc.FirstChildElement("robot");
    if (!robot)
        return false;

    // Parse links
    for (auto *link = robot->FirstChildElement("link"); link; link = link->NextSiblingElement("link"))
    {
        UrdfLink L;
        L.name = link->Attribute("name") ? link->Attribute("name") : "";
        // std::cout << L.name << "\n";

        // Parse visual elements for the link
        for (auto *visual = link->FirstChildElement("visual"); visual; visual = visual->NextSiblingElement("visual"))
        {
            UrdfVisual V;
            auto *geometry = visual->FirstChildElement("geometry");
            if (geometry)
            {
                auto *box = geometry->FirstChildElement("box");
                if (box)
                {
                    V.geometryType = GeometryType::Box;
                    V.size = parseXYZ(box->Attribute("size"));
                }
                auto *cylinder = geometry->FirstChildElement("cylinder");
                if (cylinder)
                {
                    V.geometryType = GeometryType::Cylinder;
                    V.size[0] = atof(cylinder->Attribute("radius"));
                    V.size[1] = atof(cylinder->Attribute("length"));
                }
                auto *sphere = geometry->FirstChildElement("sphere");
                if (sphere)
                {
                    V.geometryType = GeometryType::Sphere;
                    V.size[0] = atof(sphere->Attribute("radius"));
                }
                auto *mesh = geometry->FirstChildElement("mesh");
                if (mesh)
                {
                    V.geometryType = GeometryType::Mesh;
                    V.meshFile = mesh->Attribute("filename") ? mesh->Attribute("filename") : "";
                }
            }

            auto *origin = visual->FirstChildElement("origin");
            if (origin)
            {
                V.xyz = parseXYZ(origin->Attribute("xyz"));
                V.rpy = parseRPY(origin->Attribute("rpy"));
            }
            L.visuals.push_back(V);
        }

        // Parse collision elements (optional)
        for (auto *collision = link->FirstChildElement("collision"); collision; collision = collision->NextSiblingElement("collision"))
        {
            UrdfVisual C;
            auto *geometry = collision->FirstChildElement("geometry");
            if (geometry)
            {
                auto *box = geometry->FirstChildElement("box");
                if (box)
                {
                    C.geometryType = GeometryType::Box;
                    C.size = parseXYZ(box->Attribute("size"));
                }
                // Handle other geometry types (cylinder, sphere, mesh)
            }
            auto *origin = collision->FirstChildElement("origin");
            if (origin)
            {
                C.xyz = parseXYZ(origin->Attribute("xyz"));
                C.rpy = parseRPY(origin->Attribute("rpy"));
            }
            L.collisions.push_back(C);
        }

        // Parse inertia (optional)
        auto *inertia = link->FirstChildElement("inertial");
        if (inertia)
        {
            auto *massElem = inertia->FirstChildElement("mass");
            if (massElem)
                L.inertia.mass = atof(massElem->Attribute("value"));
            auto *inertiaElem = inertia->FirstChildElement("inertia");
            if (inertiaElem)
            {
                L.inertia.ixx = parseXYZ(inertiaElem->Attribute("ixx"));
                L.inertia.com = parseXYZ(inertiaElem->Attribute("com"));
            }
        }

        out.links[L.name] = L;
    }

    // Parse joints
    for (auto *joint = robot->FirstChildElement("joint"); joint; joint = joint->NextSiblingElement("joint"))
    {
        UrdfJoint J;
        J.name = joint->Attribute("name") ? joint->Attribute("name") : "";
        J.type = parseJointType(joint->Attribute("type"));

        auto *parent = joint->FirstChildElement("parent");
        auto *child = joint->FirstChildElement("child");
        if (parent && parent->Attribute("link"))
            J.parent = parent->Attribute("link");
        if (child && child->Attribute("link"))
            J.child = child->Attribute("link");

        auto *origin = joint->FirstChildElement("origin");
        if (origin)
        {
            J.origin_xyz = parseXYZ(origin->Attribute("xyz"));
            J.origin_rpy = parseRPY(origin->Attribute("rpy"));
        }

        auto *axis = joint->FirstChildElement("axis");
        if (axis)
            J.axis = parseXYZ(axis->Attribute("xyz"));

        auto *limit = joint->FirstChildElement("limit");
        if (limit)
        {
            if (limit->Attribute("lower"))
                sscanf(limit->Attribute("lower"), "%f", &J.lower);
            if (limit->Attribute("upper"))
                sscanf(limit->Attribute("upper"), "%f", &J.upper);
            if (limit->Attribute("damping"))
                sscanf(limit->Attribute("damping"), "%f", &J.damping);
            if (limit->Attribute("friction"))
                sscanf(limit->Attribute("friction"), "%f", &J.friction);
        }

        out.joints[J.name] = J;
    }

    // Determine root link
    for (auto &link : out.links)
    {
        if (out.joints.find(link.first) == out.joints.end())
        {
            out.root_link = link.first;
            break;
        }
    }

    return true;
}
