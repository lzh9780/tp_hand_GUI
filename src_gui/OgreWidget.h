#pragma once
#include <QWidget>
#include <QTimer>
#include <QImage>
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

signals:
    // Emitted once when OGRE could not be initialised (missing GL driver, no
    // display, ...). The widget then stays blank instead of aborting the app.
    void initFailed(const QString &message);

public:
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
    void clearScene();

    // Set joint position (rad or m). Ignored if name not found.
    void setJointPosition(const std::string &joint, float value);
    void setJointPositions(const std::unordered_map<std::string, float> &qmap);

    // Navigation style, mirrors the RViz "Views" controller choices.
    enum class ViewStyle
    {
        Orbit, // RViz "Orbit": drag rotates around the focal point, scroll zooms
        FPS    // RViz "FPS (first person)": drag looks around, WASD moves
    };

    // --- Camera API (RViz-compatible semantics) ---
    // Frame convention: right-handed, Z up (ROS / RViz). Distances are meters,
    // so the focal point is stored in Ogre coordinates (X right, Y forward,
    // Z up) and matches the URDF numbers one to one.
    void setCameraTarget(const QVector3D &t); // = RViz "Focal Point"
    void resetView();                         // restore the default view
    void setViewStyle(ViewStyle s);           // Orbit (default) / FPS
    ViewStyle viewStyle() const { return mStyle; }

    // The camera always orbits the look-at point (RViz' "Focal Point"). Zoom and
    // orbit never move it; only Move/pan and these setters do.
    void setLookAtPoint(const QVector3D &p) { setCameraTarget(p); }
    QVector3D lookAtPoint() const { return mTarget; }
    // Point the camera at the loaded model's centre and frame it.
    void focusOnModel();

    // Show / hide the look-at point marker.
    void setFocalMarkerVisible(bool on);
    bool focalMarkerVisible() const { return mFocalMarkerNode != nullptr; }

    // Place the camera like the RViz fixed-frame views: the vector is the
    // viewing direction, i.e. the axis the camera looks along.
    //   front -> +X, back -> -X, left -> +Y, right -> -Y, top -> -Z.
    void setViewDirection(const QVector3D &dir);
    void lookFromFront() { setViewDirection(QVector3D(1.f, 0.f, 0.f)); }
    void lookFromBack() { setViewDirection(QVector3D(-1.f, 0.f, 0.f)); }
    void lookFromLeft() { setViewDirection(QVector3D(0.f, 1.f, 0.f)); }
    void lookFromRight() { setViewDirection(QVector3D(0.f, -1.f, 0.f)); }
    void lookFromTop() { setViewDirection(QVector3D(0.f, 0.f, -1.f)); }
    void lookFromBottom() { setViewDirection(QVector3D(0.f, 0.f, 1.f)); }

    // Named view preset, using the same names as RViz ("Orbit" / "FPS").
    void setViewPreset(const QString &name);

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
    // Blits the frame captured in onFrame() onto this widget. The Ogre surface
    // is offscreen, so this widget is a normal composited child and the panel
    // columns can float above it.
    void paintEvent(QPaintEvent *) override;

    // Mouse navigation
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;

private slots:
    void onFrame();

private:
    // --- Ogre bootstrap ---
    // Returns false when OGRE could not be brought up; every public entry
    // point then degrades to a no-op instead of dereferencing null state.
    bool ensureOgre();
    void failOgre(const QString &message);
    void shutdownOgre(); // ordered OGRE teardown; safe to call multiple times
    void initOgre();
    void selectAndInitRenderSystem();
    void enableRTSS();        // Runtime Shader System (GL3Plus friendly)
    bool loadRtssPlugin();    // resolve + load Plugin_RTSS through Root

    // --- Scene building / teardown ---
    void destroySubtree(Ogre::SceneManager* sm, Ogre::SceneNode* n);
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
    void recreateLighting();    // key + fill directional lights on the root node

    // --- Geometry helpers ---
    Ogre::ManualObject *createBoxMO(const std::string &name, float sx, float sy, float sz);
    Ogre::ManualObject *createCylinderMO(const std::string &name, float radius, float length, int seg = 32);
    Ogre::ManualObject *createSphereMO(const std::string &name, float radius, int rings = 16, int segs = 24);

    // RViz "Grid" display: fading cells, a bright line every 10 cells and the
    // red / green world axes overdrawn on top.
    void createGrid(const std::string &name, float cellSize = 1.0f, int cellsPerSide = 30);
    // RViz "Axes" display: red X, green Y, blue Z from the origin.
    void createAxes(float length = 0.05f);

    // Marker showing the camera's look-at point (the orbit pivot), drawn like
    // RViz' focal-point indicator: a small sphere with a three-axis cross.
    void createFocalMarker(float size = 0.02f);
    void syncFocalMarker();          // move it onto the current look-at point
    void resizeFocalMarkerToView();  // keep it ~constant size on screen
    // Look up / destroy a named child of mWorld. Node::getChild(String) throws
    // for an unknown name, so these scan getChildren() instead.
    Ogre::SceneNode *findWorldChild(const char *name) const;
    void destroyWorldChild(const char *name);

    // Size the grid / axes so they frame the loaded model the way RViz' default
    // Grid display (10 x 10 cells of 1 m) frames a freshly added robot.
    void fitGridToModel();

    // Mesh loader: try native Ogre (.mesh), else Assimp → ManualObject
    Ogre::MovableObject *createAnyMeshOrManual(const std::string &name,
                                               const std::string &absPath,
                                               const std::array<float, 3> &scale);

    // --- Math helpers ---
    static Ogre::Quaternion fromRPY(float r, float p, float y);
    static Ogre::Vector3 normAxis(const std::array<float, 3> &a);
    // Reach of the loaded model in the ROS frame (origin -> farthest visual).
    float modelReach() const;
    // Bounding box of the built scene, in the ROS frame. Reads the actual world
    // positions of the visual nodes rather than the URDF offsets, because a URDF
    // routinely gives every <visual> an origin of (0,0,0) and puts the real
    // placement on the joints (which are chained). Returns false if nothing is
    // built yet.
    bool modelBounds(Ogre::Vector3 &minRos, Ogre::Vector3 &maxRos) const;

    // --- Joint application ---
    void applyJointPose(BuiltJoint &bj);

    // --- Paths ---
    static std::string resolvePathRelative(const std::string &baseFile, const std::string &uri);

    // --- Camera control state (RViz-like) ---
    enum class DragMode
    {
        None,
        Orbit,  // left button
        Look,   // left button in FPS mode
        Move,   // middle button / shift + left: move the focal point
        Zoom    // right button
    };

    void updateCamera();
    // Current camera basis in the Z-up world frame (Z forward, Y left, X down)
    // expressed in Ogre coordinates; identical numbers to RViz' camera basis.
    void cameraBasis(Ogre::Vector3 &fwd, Ogre::Vector3 &left, Ogre::Vector3 &down) const;
    Ogre::Vector3 focalPoint() const;
    void setFocalPoint(const Ogre::Vector3 &p);
    void applySlew();   // consume the pending WASD displacement
    void clampPitch();  // keep the view away from the poles

    QPoint mLastMouse;
    DragMode mDragMode{DragMode::None};
    ViewStyle mStyle{ViewStyle::Orbit};
    bool mFocalDistSet{false}; // true once zoom/pan/move set a real distance

    // Orbit camera parameters (focal point in world, spherical angles)
    QVector3D mTarget{0.f, 0.f, 0.f};
    float mDistance{1.5f}; // meters
    float mYaw{0.0f};      // around +Z (radians)
    float mPitch{0.3f};    // radians, clamp[-1.4, +1.4]

    // Look-at point marker (the point the camera orbits around).
    Ogre::SceneNode *mFocalMarkerNode{nullptr};
    float mFocalMarkerSize{0.02f};

    // The marker is only shown while the user is navigating: it is a transient
    // aiming aid, not permanent scene furniture.
    bool mFocalMarkerEnabled{true};  // master switch (Settings checkbox)
    bool mFocalMarkerShown{false};   // currently built into the scene
    QTimer mFocalMarkerHideTimer;    // grace period after a drag ends
    void showFocalMarkerTemporarily();
    void hideFocalMarker();

    // Pending displacement queued by the keyboard and applied on the next frame.
    Ogre::Vector3 mSlew{Ogre::Vector3::ZERO};
    bool mKeyFwd{false}, mKeyBack{false}, mKeyLeft{false}, mKeyRight{false};
    bool mKeyUp{false}, mKeyDown{false};

    // Same tuning as RViz' default view controller.
    float mPanSpeed{0.002f};
    float mOrbitSpeed{0.002f};
    float mZoomSpeed{0.05f};
    float mFpsLookSpeed{0.005f};
    float mFpsMoveSpeed{0.05f}; // meters per frame while a WASD key is held

private:
    // Ogre
    std::unique_ptr<Ogre::Root> mRoot;
    /* Offscreen render target the scene is drawn into. It is a
       Ogre::RenderTexture, never a window, so it has no on-screen presence: the
       readback can only contain Ogre's output, not desktop pixels. */
    Ogre::RenderTarget *mWindow{nullptr};
    /* 1x1 hidden window that only exists to own the GL context. */
    Ogre::RenderWindow *mContextWindow{nullptr};
    Ogre::TexturePtr mRenderTexture;
    Ogre::SceneManager *mScene{nullptr};
    Ogre::Camera *mCamera{nullptr};
    Ogre::Viewport *mViewport{nullptr};
    Ogre::SceneNode *mWorld{nullptr};
    QTimer mTimer;
    // Frame captured from the offscreen Ogre surface, blitted by paintEvent().
    // mReadbackBuffer is Ogre's tightly packed (row-pitch == width*3) staging
    // copy; mFrameImage owns the padded scanlines QImage requires.
    QImage mFrameImage;
    std::vector<unsigned char> mReadbackBuffer;
    bool mFrameDirty{false};
    // Size the render target / readback buffer to the widget; true if resized.
    bool ensureReadbackSize();
    // Copy the rendered target into mFrameImage (flip + pitch correction).
    void readBackFrame();
    bool mOgreInited{false};
    Ogre::RTShader::ShaderGenerator *mSG{nullptr};
    RtssTechniqueResolver* mRtssListener{nullptr};
    bool mOgreFailed{false};

    // URDF
    UrdfModel mModel;
    std::unordered_map<std::string, Ogre::SceneNode *> mLinkNodes; // link name -> node with visuals
    std::unordered_map<std::string, BuiltJoint> mBuiltJoints;
    std::unordered_map<std::string, RobotInstance> mRobots; // instance name -> data
};
