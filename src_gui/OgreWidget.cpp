#include "OgreWidget.h"
#include <QDebug>
#include <QResizeEvent>

#include <OgreRoot.h>
#include <OgreConfigFile.h>
#include <OgreRenderWindow.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreCamera.h>
#include <OgreViewport.h>
#include <OgreManualObject.h>
#include <OgreEntity.h>
#include <OgreLight.h>
#include <OgreMeshManager.h>
#include <OgreStringConverter.h>
#include <OgreMaterialManager.h>
#include <OgreTechnique.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ---------- Math helpers ----------
Ogre::Quaternion OgreWidget::fromRPY(float r, float p, float y)
{
    Ogre::Quaternion qx(Ogre::Radian(r), Ogre::Vector3::UNIT_X);
    Ogre::Quaternion qy(Ogre::Radian(p), Ogre::Vector3::UNIT_Y);
    Ogre::Quaternion qz(Ogre::Radian(y), Ogre::Vector3::UNIT_Z);
    return qz * qy * qx; // URDF is roll->pitch->yaw
}

Ogre::Vector3 OgreWidget::normAxis(const std::array<float, 3> &a)
{
    Ogre::Vector3 v(a[0], a[1], a[2]);
    if (v.length() < 1e-8f)
        v = Ogre::Vector3::UNIT_Z;
    v.normalise();
    return v;
}

// ---------- Qt lifecycle ----------
OgreWidget::OgreWidget(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setUpdatesEnabled(false);
    connect(&mTimer, &QTimer::timeout, this, &OgreWidget::onFrame);
}

OgreWidget::~OgreWidget()
{
    mTimer.stop();
    mRoot.reset(); // destroys all Ogre objects in the right order
}

void OgreWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!mOgreInited)
    {
        initOgre();
        mOgreInited = true;
        mTimer.start(16); // ~60 FPS
    }
}

void OgreWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (mWindow)
        mWindow->resize(width(), height());
    if (mCamera && mViewport)
    {
        mCamera->setAspectRatio(static_cast<float>(std::max(1, width())) / std::max(1, height()));
    }
}

void OgreWidget::onFrame()
{
    if (mRoot)
        mRoot->renderOneFrame();
}

// ---------- Ogre init ----------
void OgreWidget::initOgre()
{
    // Don’t pass plugins.cfg (we’ll load plugins manually)
    mRoot = std::make_unique<Ogre::Root>("", "", "ogre.log");

    // ---- 1) Load renderer plugins explicitly ----
#if defined(Q_OS_WIN)
    // Names match DLLs in the PATH (from your Ogre install)
    const std::vector<std::string> pluginCandidates = {
        "RenderSystem_GL3Plus", "RenderSystem_GL"};
#elif defined(Q_OS_MAC)
    const std::vector<std::string> pluginCandidates = {
        "RenderSystem_GL" // macOS 1.x package typically exposes GL
    };
#else
    // Linux: either the bare name (if OGRE_PLUGIN_DIR is set),
    // or full paths under /usr/lib/x86_64-linux-gnu/OGRE
    const std::vector<std::string> pluginCandidates = {
        "RenderSystem_GL3Plus",
        "RenderSystem_GL",
        "/usr/lib/x86_64-linux-gnu/OGRE/RenderSystem_GL3Plus",
        "/usr/lib/x86_64-linux-gnu/OGRE/RenderSystem_GL"};
#endif

    bool loadedAny = false;
    for (const auto &p : pluginCandidates)
    {
        try
        {
            mRoot->loadPlugin(p);
            loadedAny = true;
        }
        catch (...)
        { /* try next */
        }
    }
    if (!loadedAny)
    {
        throw std::runtime_error(
            "OGRE: could not load any RenderSystem plugin. "
            "Install GL/GL3Plus plugins or set OGRE_PLUGIN_DIR.");
    }

    // ---- 2) Choose a render system ----
    auto rlist = mRoot->getAvailableRenderers();
    if (rlist.empty())
    {
        throw std::runtime_error("OGRE: no available renderers after loading plugins.");
    }

    // Prefer GL3Plus if present, else first available
    Ogre::RenderSystem *rsChosen = nullptr;
    for (auto *rs : rlist)
    {
        auto name = rs->getName();
        if (name.find("GL3+") != std::string::npos || name.find("GL3Plus") != std::string::npos)
        {
            rsChosen = rs;
            break;
        }
    }
    if (!rsChosen)
        rsChosen = rlist.front();
    mRoot->setRenderSystem(rsChosen);

    // Optionally set a sane video mode (prevents config dialog needs)
    // rsChosen->setConfigOption("Colour Depth", "32");
    rsChosen->setConfigOption("FSAA", "0");
    // rsChosen->setConfigOption("VSync", "Yes"); // if you like

    // ---- 3) Initialise without creating a window ----
    mRoot->initialise(false);

    // ---- 4) Create the render window bound to the Qt widget ----
    Ogre::NameValuePairList params;
#if defined(Q_OS_WIN)
    params["externalWindowHandle"] =
        Ogre::StringConverter::toString(static_cast<std::uintptr_t>(winId()));
#elif defined(Q_OS_MAC)
    // On macOS Ogre 1.x, this is often enough:
    params["parentWindowHandle"] =
        Ogre::StringConverter::toString(static_cast<std::uintptr_t>(winId()));
#else
    // Linux (GLX/Wayland): parentWindowHandle usually works
    params["parentWindowHandle"] =
        Ogre::StringConverter::toString(static_cast<std::uintptr_t>(winId()));
#endif

    mWindow = mRoot->createRenderWindow("QtOgreWindow",
                                        std::max(1, width()),
                                        std::max(1, height()),
                                        false, &params);

    // ---- 5) Create scene/camera/viewport ----
    mScene = mRoot->createSceneManager(Ogre::ST_GENERIC);
    mCamera = mScene->createCamera("MainCamera");
    mCamera->setNearClipDistance(0.01f);
    mCamera->setFarClipDistance(2000.0f);
    mCamera->setAutoAspectRatio(true);

    mViewport = mWindow->addViewport(mCamera);
    mViewport->setBackgroundColour(Ogre::ColourValue(0.08f, 0.09f, 0.10f));

    // Light
    auto *light = mScene->createLight("MainLight");
    auto *lnode = mScene->getRootSceneNode()->createChildSceneNode();
    lnode->setPosition(3, 5, 8);
    lnode->attachObject(light);

    // Simple default material (so our ManualObjects have something valid)
    if (!Ogre::MaterialManager::getSingleton().resourceExists("DefaultWhite"))
    {
        auto m = Ogre::MaterialManager::getSingleton().create(
            "DefaultWhite", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
        m->setReceiveShadows(true);
        m->getTechnique(0)->getPass(0)->setLightingEnabled(true);
    }
}

// ---------- Public API ----------
bool OgreWidget::loadURDF(const std::string &path)
{
    UrdfModel model;
    if (!UrdfLoader::loadFile(path, model))
    {
        qWarning() << "URDF load failed:" << QString::fromStdString(path);
        return false;
    }
    clearScene();
    mModel = std::move(model);
    buildSceneFromURDF();
    return true;
}

void OgreWidget::setJointPosition(const std::string &jointName, float position)
{
    auto it = mBuiltJoints.find(jointName);
    if (it == mBuiltJoints.end())
        return;
    auto &bj = it->second;

    // Clamp if joint has limits (revolute/prismatic). Continuous has no limits.
    if (bj.spec.type == JointType::Revolute || bj.spec.type == JointType::Prismatic)
    {
        if (bj.spec.lower <= bj.spec.upper)
        {
            if (position < bj.spec.lower)
                position = bj.spec.lower;
            if (position > bj.spec.upper)
                position = bj.spec.upper;
        }
    }
    bj.pos = position;
    applyJointPose(bj);
}

void OgreWidget::setJointPositions(const std::unordered_map<std::string, float> &q)
{
    for (const auto &kv : q)
        setJointPosition(kv.first, kv.second);
}

std::vector<std::pair<std::string, JointType>> OgreWidget::listJoints() const
{
    std::vector<std::pair<std::string, JointType>> out;
    out.reserve(mBuiltJoints.size());
    for (const auto &kv : mBuiltJoints)
        out.emplace_back(kv.first, kv.second.spec.type);
    return out;
}

// ---------- Scene building ----------
void OgreWidget::clearScene()
{
    if (!mScene)
        return;
    mBuiltJoints.clear();
    mLinkNodes.clear();

    // Tear down all children of root scene node (keeps camera/light if attached elsewhere)
    auto *root = mScene->getRootSceneNode();
    auto children = root->getChildren();
    for (auto *n : children)
        root->removeChild(n);
    for (auto *n : children)
        mScene->destroySceneNode(static_cast<Ogre::SceneNode *>(n));
}

void OgreWidget::buildSceneFromURDF()
{
    if (!mScene || mModel.links.empty())
        return;

    // 1) Create a node for each link and attach visuals
    for (const auto &kv : mModel.links)
        buildLinkNode(kv.second);

    // 2) Build joint chains: parentLinkNode -> originNode -> axisNode -> childLinkNode
    //    Move child link node under axisNode and store BuiltJoint for runtime control
    for (auto &kv : mModel.joints)
    {
        UrdfJoint &J = kv.second;
        if (mLinkNodes.find(J.parent) == mLinkNodes.end() || mLinkNodes.find(J.child) == mLinkNodes.end())
        {
            qWarning() << "Joint refers to unknown link(s):"
                       << QString::fromStdString(J.name);
            continue;
        }
        auto *parentLinkNode = mLinkNodes[J.parent];
        auto *childLinkNode = mLinkNodes[J.child];

        // Detach child from root (or wherever) then reattach under new chain
        if (childLinkNode->getParentSceneNode())
            childLinkNode->getParentSceneNode()->removeChild(childLinkNode);

        auto *originNode = parentLinkNode->createChildSceneNode(J.name + "_origin");
        originNode->setPosition(J.origin_xyz[0], J.origin_xyz[1], J.origin_xyz[2]);
        originNode->setOrientation(fromRPY(J.origin_rpy[0], J.origin_rpy[1], J.origin_rpy[2]));

        auto *axisNode = originNode->createChildSceneNode(J.name + "_axis");
        axisNode->addChild(childLinkNode);

        BuiltJoint bj;
        bj.originNode = originNode;
        bj.axisNode = axisNode;
        bj.spec = J;
        bj.pos = 0.0f;
        mBuiltJoints[J.name] = bj;

        // Apply default pose (0)
        applyJointPose(mBuiltJoints[J.name]);
    }

    // 3) Place root link if we can infer it; otherwise everything remains under scene root
    if (!mModel.root_link.empty())
    {
        // Nothing special needed if we already parented everything by joints.
        // If a link has no parent joint, it remains a direct child of scene root.
    }

    // 4) Optional: put camera to see the model roughly centered
    mCamera->lookAt(0, 0, 0);
}

void OgreWidget::buildLinkNode(const UrdfLink &link)
{
    auto *node = mScene->getRootSceneNode()->createChildSceneNode(link.name);
    mLinkNodes[link.name] = node;

    int idx = 0;
    for (const auto &vis : link.visuals)
    {
        attachVisual(node, vis, idx++);
    }
}

void OgreWidget::attachVisual(Ogre::SceneNode *linkNode, const UrdfVisual &visual, int idx)
{
    auto *vnode = linkNode->createChildSceneNode(linkNode->getName() + "_vis_" + std::to_string(idx));
    vnode->setPosition(visual.xyz[0], visual.xyz[1], visual.xyz[2]);
    vnode->setOrientation(fromRPY(visual.rpy[0], visual.rpy[1], visual.rpy[2]));

    const std::string baseName = linkNode->getName() + "_v" + std::to_string(idx);
    switch (visual.geometryType)
    {
    case GeometryType::Box:
    {
        float sx = std::max(visual.size[0], 1e-6f);
        float sy = std::max(visual.size[1], 1e-6f);
        float sz = std::max(visual.size[2], 1e-6f);
        auto *mo = createBoxMO(baseName + "_box", sx, sy, sz);
        // mo->setMaterialName("DefaultWhite");
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Cylinder:
    {
        float r = std::max(visual.size[0], 1e-6f);
        float L = std::max(visual.size[1], 1e-6f);
        auto *mo = createCylinderMO(baseName + "_cyl", r, L, 32);
        // mo->setMaterialName("DefaultWhite");
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Sphere:
    {
        float r = std::max(visual.size[0], 1e-6f);
        auto *mo = createSphereMO(baseName + "_sph", r, 16, 24);
        // mo->setMaterialName("DefaultWhite");
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Mesh:
    {
        auto *ent = createMeshEntityOrNull(baseName + "_mesh", visual.meshFile);
        if (ent)
        {
            vnode->attachObject(ent);
        }
        else
        {
            // Fallback marker: tiny box if mesh cannot be loaded
            auto *mo = createBoxMO(baseName + "_mesh_fallback", 0.02f, 0.02f, 0.02f);
            // mo->setMaterialName("DefaultWhite");
            vnode->attachObject(mo);
        }
    }
    break;
    }
}

// ---------- Joint pose ----------
void OgreWidget::applyJointPose(BuiltJoint &bj)
{
    const auto t = bj.spec.type;
    const Ogre::Vector3 axis = normAxis(bj.spec.axis);

    if (!bj.axisNode)
        return;

    if (t == JointType::Fixed)
    {
        bj.axisNode->setOrientation(Ogre::Quaternion::IDENTITY);
        bj.axisNode->setPosition(Ogre::Vector3::ZERO);
        return;
    }

    if (t == JointType::Revolute || t == JointType::Continuous)
    {
        bj.axisNode->setOrientation(Ogre::Quaternion(Ogre::Radian(bj.pos), axis));
        bj.axisNode->setPosition(Ogre::Vector3::ZERO);
        return;
    }

    if (t == JointType::Prismatic)
    {
        bj.axisNode->setOrientation(Ogre::Quaternion::IDENTITY);
        bj.axisNode->setPosition(axis * bj.pos);
        return;
    }

    // Planar etc. could be added here if you extend UrdfLoader support
}

// ---------- Geometry creation ----------
Ogre::ManualObject *OgreWidget::createBoxMO(const std::string &name, float sx, float sy, float sz)
{
    auto *mo = mScene->createManualObject(name);
    mo->begin("DefaultWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);

    const float x = sx * 0.5f, y = sy * 0.5f, z = sz * 0.5f;
    int v = 0; // running vertex count

    auto addQuad = [&](const Ogre::Vector3 &a, const Ogre::Vector3 &b,
                       const Ogre::Vector3 &c, const Ogre::Vector3 &d,
                       const Ogre::Vector3 &n)
    {
        mo->position(a);
        mo->normal(n);
        mo->position(b);
        mo->normal(n);
        mo->position(c);
        mo->normal(n);
        mo->position(d);
        mo->normal(n);
        mo->triangle(v + 0, v + 1, v + 2);
        mo->triangle(v + 0, v + 2, v + 3);
        v += 4;
    };

    using V3 = Ogre::Vector3;
    addQuad(V3(-x, -y, z), V3(x, -y, z), V3(x, y, z), V3(-x, y, z), V3(0, 0, 1));      // +Z
    addQuad(V3(x, -y, -z), V3(-x, -y, -z), V3(-x, y, -z), V3(x, y, -z), V3(0, 0, -1)); // -Z
    addQuad(V3(-x, y, z), V3(x, y, z), V3(x, y, -z), V3(-x, y, -z), V3(0, 1, 0));      // +Y
    addQuad(V3(-x, -y, -z), V3(x, -y, -z), V3(x, -y, z), V3(-x, -y, z), V3(0, -1, 0)); // -Y
    addQuad(V3(x, -y, z), V3(x, -y, -z), V3(x, y, -z), V3(x, y, z), V3(1, 0, 0));      // +X
    addQuad(V3(-x, -y, -z), V3(-x, -y, z), V3(-x, y, z), V3(-x, y, -z), V3(-1, 0, 0)); // -X

    mo->end();
    return mo;
}

Ogre::ManualObject *OgreWidget::createCylinderMO(const std::string &name, float radius, float length, int segments)
{
    auto *mo = mScene->createManualObject(name);
    mo->begin("DefaultWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);

    const float halfL = length * 0.5f;
    int v = 0;

    // Side
    for (int i = 0; i < segments; ++i)
    {
        float a0 = Ogre::Math::TWO_PI * i / segments;
        float a1 = Ogre::Math::TWO_PI * (i + 1) / segments;
        Ogre::Vector3 p00(radius * std::cos(a0), radius * std::sin(a0), -halfL);
        Ogre::Vector3 p01(radius * std::cos(a0), radius * std::sin(a0), halfL);
        Ogre::Vector3 p10(radius * std::cos(a1), radius * std::sin(a1), -halfL);
        Ogre::Vector3 p11(radius * std::cos(a1), radius * std::sin(a1), halfL);
        Ogre::Vector3 n0(std::cos(a0), std::sin(a0), 0);
        Ogre::Vector3 n1(std::cos(a1), std::sin(a1), 0);

        mo->position(p00);
        mo->normal(n0);
        mo->position(p10);
        mo->normal(n1);
        mo->position(p11);
        mo->normal(n1);
        mo->position(p01);
        mo->normal(n0);
        mo->triangle(v + 0, v + 1, v + 2);
        mo->triangle(v + 0, v + 2, v + 3);
        v += 4;
    }

    auto addCap = [&](float z, bool flip)
    {
        Ogre::Vector3 nz(0, 0, flip ? -1 : 1);
        int center = v;
        mo->position(0, 0, z);
        mo->normal(nz);
        v += 1;
        for (int i = 0; i <= segments; ++i)
        {
            float a = Ogre::Math::TWO_PI * i / segments;
            Ogre::Vector3 p(radius * std::cos(a), radius * std::sin(a), z);
            mo->position(p);
            mo->normal(nz);
            v += 1;
        }
        for (int i = 1; i <= segments; ++i)
        {
            int a = center;
            int b = center + i;
            int c = center + i + 1;
            if (flip)
                mo->triangle(a, c, b);
            else
                mo->triangle(a, b, c);
        }
    };
    addCap(halfL, false);
    addCap(-halfL, true);

    mo->end();
    return mo;
}

Ogre::ManualObject *OgreWidget::createSphereMO(const std::string &name, float radius, int rings, int segs)
{
    auto *mo = mScene->createManualObject(name);
    mo->begin("DefaultWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);

    // vertex grid
    for (int r = 0; r <= rings; ++r)
    {
        float v = float(r) / rings;
        float phi = v * Ogre::Math::PI;
        for (int s = 0; s <= segs; ++s)
        {
            float u = float(s) / segs;
            float theta = u * Ogre::Math::TWO_PI;
            Ogre::Vector3 n(std::sin(phi) * std::cos(theta),
                            std::sin(phi) * std::sin(theta),
                            std::cos(phi));
            mo->position(n * radius);
            mo->normal(n);
        }
    }

    // indices
    auto idx = [&](int r, int s)
    { return r * (segs + 1) + s; };
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < segs; ++s)
        {
            int i0 = idx(r, s);
            int i1 = idx(r, s + 1);
            int i2 = idx(r + 1, s);
            int i3 = idx(r + 1, s + 1);
            mo->triangle(i0, i2, i1);
            mo->triangle(i1, i2, i3);
        }
    }

    mo->end();
    return mo;
}

Ogre::Entity *OgreWidget::createMeshEntityOrNull(const std::string &name, const std::string &meshFile)
{
    // Expect Ogre-native mesh names registered in resource locations (e.g., .mesh)
    try
    {
        if (meshFile.empty())
            return nullptr;
        // If your URDF has "package://...", resolve paths before calling here.
        return mScene->createEntity(name, meshFile);
    }
    catch (...)
    {
        qWarning() << "Failed to load mesh:" << QString::fromStdString(meshFile)
                   << "Make sure it's an Ogre .mesh or a supported resource.";
        return nullptr;
    }
}
