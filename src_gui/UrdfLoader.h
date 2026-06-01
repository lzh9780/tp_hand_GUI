#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <array>
#include <tinyxml2.h>

// Geometry types
enum class GeometryType
{
    Box,
    Cylinder,
    Sphere,
    Mesh
};

// Visual object information (geometry)
struct UrdfVisual
{
    GeometryType geometryType;
    std::array<float, 3> size{0.0f, 0.0f, 0.0f}; // size for box, radius for sphere, etc.
    std::string meshFile;                        // Only used if geometry is a mesh

    // add start
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f}; // Only used if geometry is a mesh (mm->m needs 0.001)
    // add end

    std::array<float, 3> xyz{0.0f, 0.0f, 0.0f};  // Position
    std::array<float, 3> rpy{0.0f, 0.0f, 0.0f};  // Orientation (roll, pitch, yaw)
};

// Inertia (mass properties for links)
struct UrdfInertia
{
    float mass{1.0f};
    std::array<float, 3> ixx{0.0f, 0.0f, 0.0f}; // Inertia matrix diagonal
    std::array<float, 3> com{0.0f, 0.0f, 0.0f}; // Center of mass
};

// Link structure
struct UrdfLink
{
    std::string name;
    std::vector<UrdfVisual> visuals;
    std::vector<UrdfVisual> collisions; // Collision geometries (optional)
    UrdfInertia inertia;                // Inertia properties (optional)
};

// Joint types
enum class JointType
{
    Revolute,
    Prismatic,
    Continuous,
    Fixed,
    Planar
};

// Joint structure
struct UrdfJoint
{
    std::string name;
    JointType type{JointType::Fixed};
    std::string parent;
    std::string child;
    std::array<float, 3> origin_xyz{0.0f, 0.0f, 0.0f};
    std::array<float, 3> origin_rpy{0.0f, 0.0f, 0.0f};
    std::array<float, 3> axis{0.0f, 0.0f, 1.0f}; // For revolute and prismatic joints
    float lower{-3.141592f}, upper{3.141592f};   // Joint limits (for revolute and prismatic)
    float damping{0.0f}, friction{0.0f};         // Joint damping and friction
};

// URDF Model structure (holds all links and joints)
struct UrdfModel
{
    std::unordered_map<std::string, UrdfLink> links;
    std::unordered_map<std::string, UrdfJoint> joints;
    std::string root_link;
};

class UrdfLoader
{
public:
    // Loads a URDF file and returns a UrdfModel structure
    static bool loadFile(const std::string &path, UrdfModel &out);

private:
    // Helper functions to parse individual components
    static std::array<float, 3> parseXYZ(const char *s);
    static std::array<float, 3> parseRPY(const char *s);
    static JointType parseJointType(const char *type);
    static GeometryType parseGeometryType(const char *type);
};