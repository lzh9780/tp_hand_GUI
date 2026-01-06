#pragma once
#include <QWidget>
#include <QTimer>
#include <QVector3D>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <OgreVector3.h>
#include <OgreQuaternion.h>
#include <OgreMovableObject.h>
#include <OgreMaterialManager.h>

namespace Ogre
{
    class Root;
    class RenderWindow;
    class SceneManager;
    class SceneNode;
    class Camera;
    class Viewport;
    class ManualObject;
    class Entity;
}

#include "UrdfLoader.h"

// Per-joint runtime info
enum class BuiltJointKind
{
    Fixed,
    Revolute,
    Continuous,
    Prismatic
};

struct BuiltJoint
{
    Ogre::SceneNode *originNode{nullptr}; // joint origin pose
    Ogre::SceneNode *axisNode{nullptr};   // rotates/translates child subtree
    UrdfJoint spec;
    float pos{0.f}; // rad for rot, meters for prismatics
};

struct RobotInstance
{
    std::string name;                                             // "cmd" / "fb" etc.
    Ogre::SceneNode *root{nullptr};                               // root node for this instance
    std::unordered_map<std::string, Ogre::SceneNode *> linkNodes; // link -> node
    std::unordered_map<std::string, BuiltJoint> joints;           // joint -> nodes/spec
    bool transparent{false};                                      // render as ghost?
};

// Forward decl for RTSS
namespace Ogre
{
    namespace RTShader
    {
        class ShaderGenerator;
    }
}

// Minimal RTSS technique resolver that works across Ogre 1.12 builds
class RtssTechniqueResolver : public Ogre::MaterialManager::Listener
{
public:
    explicit RtssTechniqueResolver(Ogre::RTShader::ShaderGenerator *sg) : mSG(sg) {}
    Ogre::Technique *handleSchemeNotFound(
        uint16_t schemeIndex,
        const Ogre::String &schemeName,
        Ogre::Material *originalMaterial,
        unsigned short lodIndex,
        const Ogre::Renderable *rend) override;

private:
    Ogre::RTShader::ShaderGenerator *mSG{nullptr};
};

class OgreWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OgreWidget(QWidget *parent = nullptr);
    ~OgreWidget() override;

    struct JointInfo
    {
        std::string name;
        JointType type;
        float lower; // if no limits: lower==upper to signal "no clamp"
        float upper;
        float value; // current pos (rad or m)
    };

    // Load URDF file (absolute or relative). Returns true on success.
    bool loadURDF(const std::string &urdf_path);

    // Set joint position (rad or m). Ignored if name not found.
    void setJointPosition(const std::string &joint, float value);
    void setJointPositions(const std::unordered_map<std::string, float> &qmap);

    // Simple camera API (focus/orbit target)
    void setCameraTarget(const QVector3D &t);
    void resetView(); // sensible default view

    // Enumerate all joints (order is unspecified)
    std::vector<JointInfo> jointsInfo() const;
    // Optional getter
    bool getJointPosition(const std::string &name, float &out) const;

    bool loadURDFDual(const std::string &urdf_path,
                      const std::string &cmdName = "cmd",
                      const std::string &fbName = "fb");

    void setJointPosition(const std::string &instance,
                          const std::string &joint, float value);
    void setJointPositions(const std::string &instance,
                           const std::unordered_map<std::string, float> &q);

protected:
    // Qt lifecycle
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;

    // Mouse navigation
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private slots:
    void onFrame();

private:
    // --- Ogre bootstrap ---
    void initOgre();
    void selectAndInitRenderSystem();
    void enableRTSS(); // Runtime Shader System (GL3Plus friendly)

    // --- Scene building / teardown ---
    void clearScene();
    void buildSceneFromURDF(const std::string &urdf_path); // uses mModel
    void buildLinkNode(const UrdfLink &L);
    void attachVisual(Ogre::SceneNode *linkNode, const UrdfVisual &V, int idx, const std::string &urdf_dir);

    void clearInstances(); // remove all instances but keep grid/camera/light
    void buildInstance(const std::string &instance,
                       bool transparent,
                       const std::string &urdf_dir);

    void attachVisualToInstance(RobotInstance &R,
                                const std::string &linkName,
                                const UrdfVisual &V,
                                int idx,
                                const std::string &urdf_dir);

    void applyJointPose(RobotInstance &R, BuiltJoint &bj);
    void setNodeTreeMaterial(Ogre::SceneNode *node, const std::string &matName);
    void ensureBaseMaterials(); // "DefaultWhite" + "GhostWhite"

    // --- Geometry helpers ---
    Ogre::ManualObject *createBoxMO(const std::string &name, float sx, float sy, float sz);
    Ogre::ManualObject *createCylinderMO(const std::string &name, float radius, float length, int seg = 32);
    Ogre::ManualObject *createSphereMO(const std::string &name, float radius, int rings = 16, int segs = 24);
    Ogre::ManualObject *createGridMO(const std::string &name, int halfLines = 20, float spacing = 0.1f); // RViz-ish grid

    // Mesh loader: try native Ogre (.mesh), else Assimp → ManualObject
    Ogre::MovableObject *createAnyMeshOrManual(const std::string &name,
                                               const std::string &absPath,
                                               const std::array<float, 3> &scale);

    // --- Math helpers ---
    static Ogre::Quaternion fromRPY(float r, float p, float y);
    static Ogre::Vector3 normAxis(const std::array<float, 3> &a);

    // --- Joint application ---
    void applyJointPose(BuiltJoint &bj);

    // --- Paths ---
    static std::string resolvePathRelative(const std::string &baseFile, const std::string &uri);

    // --- Camera control state ---
    void updateCamera();
    QPoint mLastMouse;
    bool mLDown{false}, mMDown{false}, mRDown{false};
    // Orbit camera parameters (target in world, spherical angles)
    QVector3D mTarget{0.f, 0.f, 0.f};
    float mDistance{1.5f}; // meters
    float mYaw{0.0f};      // around +Z (radians)
    float mPitch{0.3f};    // radians, clamp[-1.4, +1.4]
    float mPanSpeed{0.0025f};
    float mOrbitSpeed{0.0055f};
    float mZoomSpeed{0.12f};

private:
    // Ogre
    std::unique_ptr<Ogre::Root> mRoot;
    Ogre::RenderWindow *mWindow{nullptr};
    Ogre::SceneManager *mScene{nullptr};
    Ogre::Camera *mCamera{nullptr};
    Ogre::Viewport *mViewport{nullptr};
    Ogre::SceneNode *mWorld{nullptr};
    QTimer mTimer;
    bool mOgreInited{false};
    Ogre::RTShader::ShaderGenerator *mSG{nullptr};
    RtssTechniqueResolver* mRtssListener{nullptr};

    // URDF
    UrdfModel mModel;
    std::unordered_map<std::string, Ogre::SceneNode *> mLinkNodes; // link name -> node with visuals
    std::unordered_map<std::string, BuiltJoint> mBuiltJoints;
    std::unordered_map<std::string, RobotInstance> mRobots; // instance name -> data
};
