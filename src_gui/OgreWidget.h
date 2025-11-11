#pragma once
#include <QWidget>
#include <QTimer>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <OgreVector3.h>
#include <OgreQuaternion.h>

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
    // class Vector3;
    // class Quaternion;
}

#include "UrdfLoader.h" // The general loader you wrote

// Internal representation of a built joint in the scene
struct BuiltJoint
{
    Ogre::SceneNode *originNode{nullptr}; // applies joint origin pose
    Ogre::SceneNode *axisNode{nullptr};   // rotates/translates along joint axis
    UrdfJoint spec;                       // full URDF joint spec (type/axis/limits)
    float pos{0.0f};                      // angle (rad) for revolute/continuous; meters for prismatic
};

class OgreWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OgreWidget(QWidget *parent = nullptr);
    ~OgreWidget() override;

    // Ogre init is deferred to showEvent(); you can load a URDF any time
    bool loadURDF(const std::string &path);

    // Set individual joint (angle in radians for revolute/continuous, meters for prismatic)
    void setJointPosition(const std::string &jointName, float position);

    // Bulk update
    void setJointPositions(const std::unordered_map<std::string, float> &q);

    // Query joint names/types (handy for building UI)
    std::vector<std::pair<std::string, JointType>> listJoints() const;

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void onFrame();

private:
    // Ogre init / scene build
    void initOgre();
    void clearScene();
    void buildSceneFromURDF();
    void buildLinkNode(const UrdfLink &link);
    void attachVisual(Ogre::SceneNode *linkNode, const UrdfVisual &visual, int idx);

    // Geometry helpers
    Ogre::ManualObject *createBoxMO(const std::string &name, float sx, float sy, float sz);
    Ogre::ManualObject *createCylinderMO(const std::string &name, float radius, float length, int segments = 24);
    Ogre::ManualObject *createSphereMO(const std::string &name, float radius, int rings = 16, int segs = 24);
    Ogre::Entity *createMeshEntityOrNull(const std::string &name, const std::string &meshFile);

    // Math helpers
    static Ogre::Quaternion fromRPY(float roll, float pitch, float yaw);
    static Ogre::Vector3 normAxis(const std::array<float, 3> &a);

    // Apply a single joint transform (pos already clamped if needed)
    void applyJointPose(BuiltJoint &bj);

private:
    // Ogre
    std::unique_ptr<Ogre::Root> mRoot;
    Ogre::RenderWindow *mWindow{nullptr};
    Ogre::SceneManager *mScene{nullptr};
    Ogre::Camera *mCamera{nullptr};
    Ogre::Viewport *mViewport{nullptr};
    bool mOgreInited{false};

    QTimer mTimer;

    // URDF model
    UrdfModel mModel;

    // Link name -> node (the node that holds that link's visuals)
    std::unordered_map<std::string, Ogre::SceneNode *> mLinkNodes;

    // Joint name -> nodes/spec
    std::unordered_map<std::string, BuiltJoint> mBuiltJoints;
};
