#include "OgreWidget.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>
#include <QResizeEvent>

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <cmath>

// Ogre core
#include <OgreRoot.h>
#include <OgreRenderSystem.h>
#include <OgreRenderWindow.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreViewport.h>
#include <OgreCamera.h>
#include <OgreEntity.h>
#include <OgreManualObject.h>
#include <OgreStringConverter.h>
#include <OgreMaterialManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreTechnique.h>
#include <OgreLight.h>
#include <OgreVector3.h>
#include <OgreQuaternion.h>
#include <OgreString.h>

// RTSS (Runtime Shader System) – needed for GL3Plus fixed-function materials
#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreRTShaderSystem.h> // RTSS core + listener
#include <OgreSubEntity.h>

// Assimp for STL/OBJ/DAE
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using std::string;
namespace fs = std::filesystem;

static void addRtssResourceLocations();

// -------------------- helpers --------------------
Ogre::Quaternion OgreWidget::fromRPY(float r, float p, float y)
{
    Ogre::Quaternion qx(Ogre::Radian(r), Ogre::Vector3::UNIT_X);
    Ogre::Quaternion qy(Ogre::Radian(p), Ogre::Vector3::UNIT_Y);
    Ogre::Quaternion qz(Ogre::Radian(y), Ogre::Vector3::UNIT_Z);
    return qz * qy * qx; // URDF order
}
Ogre::Vector3 OgreWidget::normAxis(const std::array<float, 3> &a)
{
    Ogre::Vector3 v(a[0], a[1], a[2]);
    if (v.length() < 1e-8f)
        v = Ogre::Vector3::UNIT_Z;
    v.normalise();
    return v;
}
string OgreWidget::resolvePathRelative(const string &baseFile, const string &uri)
{
    if (uri.empty())
        return uri;
    // Basic: treat package:// and http(s):// as given; otherwise resolve relative to URDF dir.
    if (uri.rfind("package://", 0) == 0 || uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0)
        return uri;
    fs::path p(uri);
    if (p.is_absolute())
        return p.string();
    fs::path out = fs::path(baseFile).parent_path() / p;
    return out.lexically_normal().string();
}

// -------------------- ctor/dtor --------------------
OgreWidget::OgreWidget(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setUpdatesEnabled(false);
    connect(&mTimer, &QTimer::timeout, this, &OgreWidget::onFrame);
}
OgreWidget::~OgreWidget()
{
    if (mRtssListener)
    {
        Ogre::MaterialManager::getSingleton().removeListener(mRtssListener);
        delete mRtssListener;
        mRtssListener = nullptr;
    }
    mTimer.stop();
    mRoot.reset();
}

// -------------------- Qt lifecycle --------------------
void OgreWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!mOgreInited)
    {
        initOgre();
        mOgreInited = true;
        resetView();
        mTimer.start(16);
    }
}
void OgreWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (mWindow)
        mWindow->resize(width(), height());
    if (mCamera && mViewport)
        mCamera->setAspectRatio(float(std::max(1, width())) / std::max(1, height()));
}
void OgreWidget::onFrame()
{
    if (mRoot)
        mRoot->renderOneFrame();
}

// -------------------- Ogre init --------------------
void OgreWidget::initOgre()
{
    // No plugins.cfg; load plugins programmatically
    mRoot = std::make_unique<Ogre::Root>("", "", "log/ogre.log");
    selectAndInitRenderSystem();

    // Create render window bound to this QWidget
    Ogre::NameValuePairList params;
    params["externalWindowHandle"] = Ogre::StringConverter::toString(static_cast<std::uintptr_t>(winId()));
    mWindow = mRoot->createRenderWindow("QtOgreWindow",
                                        std::max(1, width()),
                                        std::max(1, height()),
                                        false, &params);

    // Scene + camera
    mScene = mRoot->createSceneManager(Ogre::ST_GENERIC);
    mCamera = mScene->createCamera("MainCamera");
    mCamera->setNearClipDistance(0.01f);
    mCamera->setFarClipDistance(2000.0f);
    mCamera->setAutoAspectRatio(true);
    mViewport = mWindow->addViewport(mCamera);
    mViewport->setBackgroundColour(Ogre::ColourValue(0.8f, 0.8f, 0.8f));

    mWorld = mScene->getRootSceneNode()->createChildSceneNode("WorldFixip");
    mWorld->pitch(Ogre::Degree(-90.0f));

    addRtssResourceLocations();

    // 1) Enable RTSS first
    enableRTSS();
    // 2) Tell the viewport to use the RTSS scheme (this is the one GL3Plus will request)
    mViewport->setMaterialScheme(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
    // 3) Create our basic materials AFTER RTSS is active
    ensureBaseMaterials();
    // 4) Seed RTSS once for the materials we’ll use so a shader technique is available immediately
    if (auto *sg = Ogre::RTShader::ShaderGenerator::getSingletonPtr())
    {
        auto seed = [&](const Ogre::String &matName)
        {
            Ogre::MaterialPtr m = Ogre::MaterialManager::getSingleton().getByName(matName);
            if (!m.isNull())
            {
                sg->createShaderBasedTechnique(
                    *m,
                    Ogre::MaterialManager::DEFAULT_SCHEME_NAME,
                    Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME,
                    /*overProgrammable=*/false);
                sg->validateMaterial(
                    Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME,
                    matName);
            }
        };
        seed("DefaultWhite");
        seed("GhostWhite");
        seed("GridUnlit");
    }

    // Light
    auto *light = mScene->createLight("MainLight");
    auto *lnode = mScene->getRootSceneNode()->createChildSceneNode();
    lnode->setPosition(3, 5, 8);
    lnode->attachObject(light);

    // RViz-like grid (on XY plane, Z up)
    auto *grid = createGridMO("GroundGrid", 30, 0.05f);
    mWorld->createChildSceneNode()->attachObject(grid);
}

void OgreWidget::selectAndInitRenderSystem()
{
    // Try GL3Plus first, then GL
    std::vector<string> plugins;
#if defined(Q_OS_WIN)
    plugins = {"RenderSystem_GL3Plus", "RenderSystem_GL", "Plugin_RTSS"};
#else
    // Linux paths fallback if OGRE_PLUGIN_DIR env isn’t set
    plugins = {
        "RenderSystem_GL3Plus", "RenderSystem_GL", "Plugin_RTSS",
        "/usr/lib/x86_64-linux-gnu/OGRE/RenderSystem_GL3Plus",
        "/usr/lib/x86_64-linux-gnu/OGRE/RenderSystem_GL",
        "/usr/lib/x86_64-linux-gnu/OGRE/Plugin_RTSS"};
#endif
    bool any = false;
    for (auto &p : plugins)
    {
        try
        {
            mRoot->loadPlugin(p);
            any = true;
        }
        catch (...)
        {
        }
    }
    if (!any)
        throw std::runtime_error("Failed to load any Ogre render plugins.");

    auto rs = mRoot->getAvailableRenderers();
    if (rs.empty())
        throw std::runtime_error("No available renderers.");
    Ogre::RenderSystem *chosen = nullptr;
    for (auto *r : rs)
    {
        auto n = r->getName();
        if (n.find("GL3") != string::npos || n.find("GL3+") != string::npos)
        {
            chosen = r;
            break;
        }
    }
    if (!chosen)
        chosen = rs.front();
    mRoot->setRenderSystem(chosen);
    // Don’t set unknown options like “Colour Depth”. Defaults are fine.
    mRoot->initialise(false);
}

void OgreWidget::enableRTSS()
{
    using namespace Ogre::RTShader;

    // Load plugin if available (safe if already loaded)
    try
    {
        mRoot->loadPlugin("Plugin_RTSS");
    }
    catch (...)
    {
    }

    // Init generator singleton
    if (!ShaderGenerator::getSingletonPtr())
        ShaderGenerator::initialize();

    auto *sg = ShaderGenerator::getSingletonPtr();
    sg->addSceneManager(mScene); // IMPORTANT: register your SceneManager

    // Install our listener once
    if (!mRtssListener)
    {
        mRtssListener = new RtssTechniqueResolver(sg);
        Ogre::MaterialManager::getSingleton().addListener(mRtssListener);
    }

    // Make RTSS the active scheme so GL3Plus uses generated shaders
    Ogre::MaterialManager::getSingleton()
        .setActiveScheme(ShaderGenerator::DEFAULT_SCHEME_NAME);
}

// -------------------- Public API --------------------
bool OgreWidget::loadURDF(const std::string &urdf_path)
{
    UrdfModel model;
    if (!UrdfLoader::loadFile(urdf_path, model))
    {
        qWarning() << "URDF load failed:" << QString::fromStdString(urdf_path);
        return false;
    }
    clearScene();
    mModel = std::move(model);
    buildSceneFromURDF(urdf_path);
    resetView();
    return true;
}

void OgreWidget::setJointPosition(const std::string &name, float value)
{
    auto it = mBuiltJoints.find(name);
    if (it == mBuiltJoints.end())
        return;
    auto &bj = it->second;
    if (bj.spec.type == JointType::Revolute || bj.spec.type == JointType::Prismatic)
    {
        if (bj.spec.lower <= bj.spec.upper)
        {
            value = std::max(bj.spec.lower, std::min(bj.spec.upper, value));
        }
    }
    bj.pos = value;
    applyJointPose(bj);
}
void OgreWidget::setJointPositions(const std::unordered_map<std::string, float> &qmap)
{
    for (auto &kv : qmap)
        setJointPosition(kv.first, kv.second);
}
void OgreWidget::setCameraTarget(const QVector3D &t)
{
    mTarget = t;
    updateCamera();
}
void OgreWidget::resetView()
{
    mTarget = QVector3D(0, 0, 0);
    mDistance = 1.0f;
    mYaw = -0.7f;
    mPitch = 0.35f;
    updateCamera();
}

// -------------------- Scene build --------------------
void OgreWidget::clearScene()
{
    if (!mScene)
        return;
    mBuiltJoints.clear();
    mLinkNodes.clear();

    // Destroy all children under root (keep light manager itself alive)
    auto *root = mWorld;
    auto kids = root->getChildren();
    for (auto *n : kids)
        root->removeChild(n);
    for (auto *n : kids)
        mScene->destroySceneNode(static_cast<Ogre::SceneNode *>(n));

    // Re-create grid after clear
    auto *grid = createGridMO("NewGroundGrid", 30, 0.05f);
    mWorld->createChildSceneNode()->attachObject(grid);
}

void OgreWidget::buildSceneFromURDF(const std::string &urdf_path)
{
    if (!mScene || mModel.links.empty())
        return;
    // Create link nodes + visuals
    for (const auto &kv : mModel.links)
        buildLinkNode(kv.second);

    // Parent links via joints
    for (auto &kv : mModel.joints)
    {
        UrdfJoint &J = kv.second;
        auto pit = mLinkNodes.find(J.parent), cit = mLinkNodes.find(J.child);
        if (pit == mLinkNodes.end() || cit == mLinkNodes.end())
        {
            qWarning() << "Unknown joint links:" << J.name.c_str();
            continue;
        }
        auto *parent = pit->second;
        auto *child = cit->second;

        if (child->getParentSceneNode())
            child->getParentSceneNode()->removeChild(child);

        auto *originNode = parent->createChildSceneNode(J.name + "_origin");
        originNode->setPosition(J.origin_xyz[0], J.origin_xyz[1], J.origin_xyz[2]);
        originNode->setOrientation(fromRPY(J.origin_rpy[0], J.origin_rpy[1], J.origin_rpy[2]));
        auto *axisNode = originNode->createChildSceneNode(J.name + "_axis");
        axisNode->addChild(child);

        BuiltJoint bj;
        bj.originNode = originNode;
        bj.axisNode = axisNode;
        bj.spec = J;
        bj.pos = 0.f;
        mBuiltJoints[J.name] = bj;
        applyJointPose(mBuiltJoints[J.name]);
    }

    // Load visuals for each link (URDF file dir for path resolution)
    const std::string dir = fs::path(urdf_path).parent_path().string();
    for (const auto &kv : mModel.links)
    {
        auto *node = mLinkNodes[kv.first];
        int vi = 0;
        for (const auto &vis : kv.second.visuals)
        {
            attachVisual(node, vis, vi++, dir);
        }
    }
}

void OgreWidget::buildLinkNode(const UrdfLink &L)
{
    auto *node = mWorld->createChildSceneNode(L.name);
    mLinkNodes[L.name] = node;
}

void OgreWidget::attachVisual(Ogre::SceneNode *linkNode, const UrdfVisual &V, int idx, const std::string &urdf_dir)
{
    auto *vnode = linkNode->createChildSceneNode(linkNode->getName() + "_vis_" + std::to_string(idx));
    vnode->setPosition(V.xyz[0], V.xyz[1], V.xyz[2]);
    vnode->setOrientation(fromRPY(V.rpy[0], V.rpy[1], V.rpy[2]));

    const std::string baseName = linkNode->getName() + "_v" + std::to_string(idx);
    switch (V.geometryType)
    {
    case GeometryType::Box:
    {
        float sx = std::max(V.size[0], 1e-6f), sy = std::max(V.size[1], 1e-6f), sz = std::max(V.size[2], 1e-6f);
        auto *mo = createBoxMO(baseName + "_box", sx, sy, sz);
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Cylinder:
    {
        float r = std::max(V.size[0], 1e-6f), L = std::max(V.size[1], 1e-6f);
        auto *mo = createCylinderMO(baseName + "_cyl", r, L, 48);
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Sphere:
    {
        float r = std::max(V.size[0], 1e-6f);
        auto *mo = createSphereMO(baseName + "_sph", r, 16, 24);
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Mesh:
    {
        std::array<float, 3> s{1.f, 1.f, 1.f};
        if constexpr (true)
        { // if your UrdfVisual has .scale, use it; else keep 1,1,1
          // s = V.scale;
        }
        std::string full = resolvePathRelative(urdf_dir + "/dummy", V.meshFile);
        if (auto *mov = createAnyMeshOrManual(baseName + "_mesh", full, s))
        {
            vnode->attachObject(mov);
        }
        else
        {
            auto *mo = createBoxMO(baseName + "_fallback", 0.02f, 0.02f, 0.02f);
            vnode->attachObject(mo);
        }
    }
    break;
    }
}

// -------------------- Joints --------------------
void OgreWidget::applyJointPose(BuiltJoint &bj)
{
    auto t = bj.spec.type;
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
}

// -------------------- Geometry (ManualObject) --------------------
Ogre::ManualObject *OgreWidget::createBoxMO(const std::string &name, float sx, float sy, float sz)
{
    auto *mo = mScene->createManualObject(name);
    mo->begin("DefaultWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    const float x = sx * 0.5f, y = sy * 0.5f, z = sz * 0.5f;
    int v = 0;
    auto quad = [&](Ogre::Vector3 a, Ogre::Vector3 b, Ogre::Vector3 c, Ogre::Vector3 d, Ogre::Vector3 n)
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
    quad(V3(-x, -y, z), V3(x, -y, z), V3(x, y, z), V3(-x, y, z), V3(0, 0, 1));
    quad(V3(x, -y, -z), V3(-x, -y, -z), V3(-x, y, -z), V3(x, y, -z), V3(0, 0, -1));
    quad(V3(-x, y, z), V3(x, y, z), V3(x, y, -z), V3(-x, y, -z), V3(0, 1, 0));
    quad(V3(-x, -y, -z), V3(x, -y, -z), V3(x, -y, z), V3(-x, -y, z), V3(0, -1, 0));
    quad(V3(x, -y, z), V3(x, -y, -z), V3(x, y, -z), V3(x, y, z), V3(1, 0, 0));
    quad(V3(-x, -y, -z), V3(-x, -y, z), V3(-x, y, z), V3(-x, y, -z), V3(-1, 0, 0));
    mo->end();
    return mo;
}

Ogre::ManualObject *OgreWidget::createCylinderMO(const std::string &name, float r, float L, int seg)
{
    auto *mo = mScene->createManualObject(name);
    mo->begin("DefaultWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    const float h = L * 0.5f;
    int v = 0;
    for (int i = 0; i < seg; ++i)
    {
        float a0 = float(i) * Ogre::Math::TWO_PI / seg, a1 = float(i + 1) * Ogre::Math::TWO_PI / seg;
        Ogre::Vector3 p00(r * std::cos(a0), r * std::sin(a0), -h);
        Ogre::Vector3 p01(r * std::cos(a0), r * std::sin(a0), h);
        Ogre::Vector3 p10(r * std::cos(a1), r * std::sin(a1), -h);
        Ogre::Vector3 p11(r * std::cos(a1), r * std::sin(a1), h);
        Ogre::Vector3 n0(std::cos(a0), std::sin(a0), 0), n1(std::cos(a1), std::sin(a1), 0);
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
    auto cap = [&](float z, bool flip)
    {
        Ogre::Vector3 nz(0, 0, flip ? -1 : 1);
        int c = v;
        mo->position(0, 0, z);
        mo->normal(nz);
        v += 1;
        for (int i = 0; i <= seg; ++i)
        {
            float a = float(i) * Ogre::Math::TWO_PI / seg;
            mo->position(r * std::cos(a), r * std::sin(a), z);
            mo->normal(nz);
            v += 1;
        }
        for (int i = 1; i <= seg; ++i)
        {
            int A = c, B = c + i, C = c + i + 1;
            if (flip)
                mo->triangle(A, C, B);
            else
                mo->triangle(A, B, C);
        }
    };
    cap(h, false);
    cap(-h, true);
    mo->end();
    return mo;
}

Ogre::ManualObject *OgreWidget::createSphereMO(const std::string &name, float rad, int rings, int segs)
{
    auto *mo = mScene->createManualObject(name);
    mo->begin("DefaultWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    for (int r = 0; r <= rings; ++r)
    {
        float v = float(r) / rings, phi = v * Ogre::Math::PI;
        for (int s = 0; s <= segs; ++s)
        {
            float u = float(s) / segs, th = u * Ogre::Math::TWO_PI;
            Ogre::Vector3 n(std::sin(phi) * std::cos(th), std::sin(phi) * std::sin(th), std::cos(phi));
            mo->position(n * rad);
            mo->normal(n);
        }
    }
    auto idx = [&](int r, int s)
    { return r * (segs + 1) + s; };
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < segs; ++s)
        {
            int i0 = idx(r, s), i1 = idx(r, s + 1), i2 = idx(r + 1, s), i3 = idx(r + 1, s + 1);
            mo->triangle(i0, i2, i1);
            mo->triangle(i1, i2, i3);
        }
    }
    mo->end();
    return mo;
}

Ogre::ManualObject *OgreWidget::createGridMO(const std::string &name, int halfLines, float spacing)
{
    auto *mo = mScene->createManualObject(name);
    mo->begin("GridUnlit", Ogre::RenderOperation::OT_LINE_LIST);
    const int N = halfLines;
    const float S = spacing;
    // Grid on XY plane (Z up), from -N..N
    for (int i = -N; i <= N; ++i)
    {
        float d = i * S;
        // lines parallel X
        mo->position(-N * S, d, 0);
        mo->normal(0, 0, 1);
        mo->position(N * S, d, 0);
        mo->normal(0, 0, 1);
        // lines parallel Y
        mo->position(d, -N * S, 0);
        mo->normal(0, 0, 1);
        mo->position(d, N * S, 0);
        mo->normal(0, 0, 1);
    }
    mo->end();
    return mo;
}

// -------------------- Mesh loading --------------------
Ogre::MovableObject *OgreWidget::createAnyMeshOrManual(const std::string &name,
                                                       const std::string &absPath,
                                                       const std::array<float, 3> &scale)
{
    // 1) Try native .mesh by path or resource name
    try
    {
        // If absPath endswith .mesh and is in a resource location, this works:
        auto *ent = mScene->createEntity(name, absPath);
        return ent; // caller attaches to node and applies node scale
    }
    catch (...)
    { /* continue */
    }

    // 2) Assimp fallback
    Assimp::Importer importer;
    const aiScene *sc = importer.ReadFile(absPath,
                                          aiProcess_Triangulate |
                                              aiProcess_GenNormals |
                                              aiProcess_JoinIdenticalVertices |
                                              aiProcess_ImproveCacheLocality |
                                              aiProcess_SortByPType |
                                              aiProcess_OptimizeMeshes |
                                              aiProcess_OptimizeGraph);
    if (!sc || !sc->mRootNode)
    {
        qWarning() << "Assimp failed:" << absPath.c_str();
        return nullptr;
    }

    auto *mo = mScene->createManualObject(name + "_mo");
    mo->begin("DefaultWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    unsigned vbase = 0;

    std::function<void(const aiNode *, aiMatrix4x4)> visit = [&](const aiNode *node, aiMatrix4x4 parent)
    {
        aiMatrix4x4 M = parent * node->mTransformation;
        for (unsigned m = 0; m < node->mNumMeshes; ++m)
        {
            const aiMesh *mesh = sc->mMeshes[node->mMeshes[m]];
            for (unsigned v = 0; v < mesh->mNumVertices; ++v)
            {
                aiVector3D P = mesh->mVertices[v];
                aiVector3D N = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0, 0, 1);
                // Apply node transform (no full normal transform for brevity)
                aiVector3D P2 = M * P;
                mo->position(scale[0] * P2.x, scale[1] * P2.y, scale[2] * P2.z);
                mo->normal(N.x, N.y, N.z);
            }
            for (unsigned f = 0; f < mesh->mNumFaces; ++f)
            {
                const aiFace &face = mesh->mFaces[f];
                if (face.mNumIndices == 3)
                {
                    mo->triangle(vbase + face.mIndices[0],
                                 vbase + face.mIndices[1],
                                 vbase + face.mIndices[2]);
                }
            }
            vbase += mesh->mNumVertices;
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c)
            visit(node->mChildren[c], M);
    };
    visit(sc->mRootNode, aiMatrix4x4());

    mo->end();
    return mo;
}

// -------------------- Camera/nav --------------------
void OgreWidget::updateCamera()
{
    mPitch = std::clamp(mPitch, -1.40f, +1.40f);
    mDistance = std::max(0.05f, mDistance);

    // Target in world, orbit angles yaw (around Z) and pitch (around X in camera local)
    const float cy = std::cos(mYaw), sy = std::sin(mYaw);
    const float cp = std::cos(mPitch), sp = std::sin(mPitch);

    // Build a direction (Z-up world)
    Ogre::Vector3 dir(cp * cy, cp * sy, sp);
    Ogre::Vector3 eye = Ogre::Vector3(mTarget.x(), mTarget.y(), mTarget.z()) + (-dir * mDistance);

    mCamera->setPosition(eye);
    mCamera->lookAt(Ogre::Vector3(mTarget.x(), mTarget.y(), mTarget.z()));
}

void OgreWidget::mousePressEvent(QMouseEvent *e)
{
    mLastMouse = e->pos();
    if (e->button() == Qt::LeftButton)
        mLDown = true;
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)))
        mMDown = true;
    if (e->button() == Qt::RightButton)
        mRDown = true;
}
void OgreWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        mLDown = false;
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)))
        mMDown = false;
    if (e->button() == Qt::RightButton)
        mRDown = false;
}
void OgreWidget::mouseMoveEvent(QMouseEvent *e)
{
    QPoint d = e->pos() - mLastMouse;
    mLastMouse = e->pos();

    if (mLDown && !(e->modifiers() & Qt::ShiftModifier))
    {
        // Orbit
        mPitch += d.x() * mOrbitSpeed;
        mYaw -= d.y() * mOrbitSpeed;
        updateCamera();
    }
    else if (mMDown || (mLDown && (e->modifiers() & Qt::ShiftModifier)))
    {
        // Pan in camera plane
        float panScale = mPanSpeed * mDistance;
        // Build camera basis
        Ogre::Vector3 z = (mCamera->getPosition() - Ogre::Vector3(mTarget.x(), mTarget.y(), mTarget.z()));
        z.normalise();
        Ogre::Vector3 x = mCamera->getRight(); // Ogre gives camera right/up
        Ogre::Vector3 y = mCamera->getUp();
        Ogre::Vector3 t = Ogre::Vector3(mTarget.x(), mTarget.y(), mTarget.z()) - x * (d.x() * panScale) + y * (d.y() * panScale);
        mTarget = QVector3D(t.x, t.y, t.z);
        updateCamera();
    }
    else if (mRDown)
    {
        // Dolly (zoom)
        mDistance *= (1.f + d.y() * mZoomSpeed * 0.02f);
        updateCamera();
    }
}
void OgreWidget::wheelEvent(QWheelEvent *e)
{
    // Smooth zoom
    float steps = e->angleDelta().y() / 120.f; // 1 step = 120
    mDistance *= (1.f - steps * mZoomSpeed * 0.25f);
    updateCamera();
}

std::vector<OgreWidget::JointInfo> OgreWidget::jointsInfo() const
{
    std::vector<JointInfo> v;
    v.reserve(mBuiltJoints.size());
    for (const auto &kv : mBuiltJoints)
    {
        const auto &bj = kv.second;
        JointInfo ji;
        ji.name = kv.first;
        ji.type = bj.spec.type;
        ji.lower = bj.spec.lower;
        ji.upper = bj.spec.upper;
        ji.value = bj.pos;
        v.push_back(ji);
    }
    return v;
}

bool OgreWidget::getJointPosition(const std::string &name, float &out) const
{
    auto it = mBuiltJoints.find(name);
    if (it == mBuiltJoints.end())
        return false;
    out = it->second.pos;
    return true;
}

void OgreWidget::ensureBaseMaterials()
{
    using RGM = Ogre::ResourceGroupManager;
    if (!Ogre::MaterialManager::getSingleton().resourceExists("DefaultWhite"))
    {
        auto m = Ogre::MaterialManager::getSingleton().create(
            "DefaultWhite", RGM::DEFAULT_RESOURCE_GROUP_NAME);
        m->setReceiveShadows(true);
        m->getTechnique(0)->getPass(0)->setLightingEnabled(true);
    }
    if (!Ogre::MaterialManager::getSingleton().resourceExists("GhostWhite"))
    {
        auto m = Ogre::MaterialManager::getSingleton().create(
            "GhostWhite", RGM::DEFAULT_RESOURCE_GROUP_NAME);
        auto *p = m->getTechnique(0)->getPass(0);
        p->setLightingEnabled(true);
        p->setAmbient(0.4f, 0.8f, 1.0f);        // cyan-ish
        p->setDiffuse(0.4f, 0.8f, 1.0f, 0.35f); // alpha 0.35
        p->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
        p->setDepthWriteEnabled(false); // nicer overlay without z-fighting artifacts
    }
    if (!Ogre::MaterialManager::getSingleton().resourceExists("GridUnlit")) {
        auto m = Ogre::MaterialManager::getSingleton().create("GridUnlit", RGM::DEFAULT_RESOURCE_GROUP_NAME);
        auto* p = m->getTechnique(0)->getPass(0);
        p->setLightingEnabled(false);
        p->setDiffuse(0.65f, 0.65f, 0.65f, 1.0f);
        p->setAmbient(0.65f, 0.65f, 0.65f);
    }
}

void OgreWidget::clearInstances()
{
    // Destroy all instance roots but keep the grid/light that you created separately.
    for (auto &kv : mRobots)
    {
        if (kv.second.root)
        {
            kv.second.root->removeAndDestroyAllChildren();
            mScene->destroySceneNode(kv.second.root);
        }
    }
    mRobots.clear();
}

bool OgreWidget::loadURDFDual(const std::string &urdf_path,
                              const std::string &cmdName,
                              const std::string &fbName)
{
    UrdfModel model;
    if (!UrdfLoader::loadFile(urdf_path, model))
    {
        qWarning() << "URDF load failed:" << QString::fromStdString(urdf_path);
        return false;
    }
    mModel = std::move(model);
    clearInstances();

    const std::string dir = fs::path(urdf_path).parent_path().string();

    buildInstance(cmdName, /*transparent*/ false, dir);
    buildInstance(fbName, /*transparent*/ true, dir);

    resetView();
    return true;
}

void OgreWidget::buildInstance(const std::string &instance,
                               bool transparent,
                               const std::string &urdf_dir)
{
    RobotInstance R;
    R.name = instance;
    R.transparent = transparent;
    R.root = mWorld->createChildSceneNode(instance + "_root");

    // 1) Create link nodes (empty for now)
    for (const auto &kv : mModel.links)
    {
        auto *node = mWorld->createChildSceneNode(instance + "_" + kv.first);
        R.linkNodes[kv.first] = node;
    }

    // 2) Create joint chains: parentLink -> origin -> axis -> childLink
    for (auto &kv : mModel.joints)
    {
        const UrdfJoint &J = kv.second;
        auto pit = R.linkNodes.find(J.parent), cit = R.linkNodes.find(J.child);
        if (pit == R.linkNodes.end() || cit == R.linkNodes.end())
            continue;

        auto *parent = pit->second;
        auto *child = cit->second;
        if (child->getParentSceneNode())
            child->getParentSceneNode()->removeChild(child);

        auto *originNode = parent->createChildSceneNode(instance + "_" + J.name + "_origin");
        originNode->setPosition(J.origin_xyz[0], J.origin_xyz[1], J.origin_xyz[2]);
        originNode->setOrientation(fromRPY(J.origin_rpy[0], J.origin_rpy[1], J.origin_rpy[2]));

        auto *axisNode = originNode->createChildSceneNode(instance + "_" + J.name + "_axis");
        axisNode->addChild(child);

        BuiltJoint bj;
        bj.originNode = originNode;
        bj.axisNode = axisNode;
        bj.spec = J;
        bj.pos = 0.f;
        R.joints[J.name] = bj;
    }

    // 3) Attach visuals
    for (const auto &kv : mModel.links)
    {
        int vi = 0;
        for (const auto &vis : kv.second.visuals)
        {
            attachVisualToInstance(R, kv.first, vis, vi++, urdf_dir);
        }
    }

    // 4) If transparent, swap materials on the whole instance
    if (transparent)
    {
        setNodeTreeMaterial(R.root, "GhostWhite");
    }

    // 5) Apply default pose
    for (auto &kv : R.joints)
        applyJointPose(R, kv.second);

    mRobots[instance] = std::move(R);
}

void OgreWidget::attachVisualToInstance(RobotInstance &R,
                                        const std::string &linkName,
                                        const UrdfVisual &V,
                                        int idx,
                                        const std::string &urdf_dir)
{
    Ogre::SceneNode *linkNode = R.linkNodes.at(linkName);
    auto *vnode = linkNode->createChildSceneNode(
        R.name + "_" + linkName + "_vis_" + std::to_string(idx));
    vnode->setPosition(V.xyz[0], V.xyz[1], V.xyz[2]);
    vnode->setOrientation(fromRPY(V.rpy[0], V.rpy[1], V.rpy[2]));

    const std::string base = R.name + "_" + linkName + "_v" + std::to_string(idx);

    switch (V.geometryType)
    {
    case GeometryType::Box:
    {
        auto *mo = createBoxMO(base + "_box",
                               std::max(V.size[0], 1e-6f),
                               std::max(V.size[1], 1e-6f),
                               std::max(V.size[2], 1e-6f));
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Cylinder:
    {
        auto *mo = createCylinderMO(base + "_cyl",
                                    std::max(V.size[0], 1e-6f),
                                    std::max(V.size[1], 1e-6f), 48);
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Sphere:
    {
        auto *mo = createSphereMO(base + "_sph", std::max(V.size[0], 1e-6f), 16, 24);
        vnode->attachObject(mo);
    }
    break;
    case GeometryType::Mesh:
    {
        std::array<float, 3> s{1.f, 1.f, 1.f}; // use V.scale if you added it
        std::string full = resolvePathRelative(urdf_dir + "/dummy", V.meshFile);
        if (auto *mov = createAnyMeshOrManual(base + "_mesh", full, s))
        {
            vnode->attachObject(mov);
        }
        else
        {
            vnode->attachObject(createBoxMO(base + "_fallback", 0.02f, 0.02f, 0.02f));
        }
    }
    break;
    }
}

void OgreWidget::setNodeTreeMaterial(Ogre::SceneNode *node, const std::string &matName)
{
    // Replace material for any ManualObject or Entity under this node (recursively)
    auto objs = node->getAttachedObjects();
    for (auto *mo : objs)
    {
        if (auto *man = dynamic_cast<Ogre::ManualObject *>(mo))
        {
            // change each section’s material
            for (unsigned i = 0; i < man->getNumSections(); ++i)
            {
                man->getSection(i)->setMaterialName(matName.c_str());
            }
        }
        else if (auto *ent = dynamic_cast<Ogre::Entity *>(mo))
        {
            // change all sub-entities
            for (unsigned i = 0; i < ent->getNumSubEntities(); ++i)
            {
                ent->getSubEntity(i)->setMaterialName(matName.c_str());
            }
        }
    }
    auto children = node->getChildren();
    for (auto *c : children)
    {
        setNodeTreeMaterial(static_cast<Ogre::SceneNode *>(c), matName);
    }
}

void OgreWidget::applyJointPose(RobotInstance &R, BuiltJoint &bj)
{
    const Ogre::Vector3 axis = normAxis(bj.spec.axis);
    if (!bj.axisNode)
        return;
    switch (bj.spec.type)
    {
    case JointType::Fixed:
        bj.axisNode->setOrientation(Ogre::Quaternion::IDENTITY);
        bj.axisNode->setPosition(Ogre::Vector3::ZERO);
        break;
    case JointType::Revolute:
    case JointType::Continuous:
        bj.axisNode->setOrientation(Ogre::Quaternion(Ogre::Radian(bj.pos), axis));
        bj.axisNode->setPosition(Ogre::Vector3::ZERO);
        break;
    case JointType::Prismatic:
        bj.axisNode->setOrientation(Ogre::Quaternion::IDENTITY);
        bj.axisNode->setPosition(axis * bj.pos);
        break;
    default:
        break;
    }
}

void OgreWidget::setJointPosition(const std::string &instance,
                                  const std::string &joint, float value)
{
    auto itR = mRobots.find(instance);
    if (itR == mRobots.end())
        return;
    auto itJ = itR->second.joints.find(joint);
    if (itJ == itR->second.joints.end())
        return;

    auto &bj = itJ->second;
    if (bj.spec.type == JointType::Revolute || bj.spec.type == JointType::Prismatic)
    {
        if (bj.spec.lower <= bj.spec.upper)
        {
            value = std::max(bj.spec.lower, std::min(bj.spec.upper, value));
        }
    }
    bj.pos = value;
    applyJointPose(itR->second, bj);
}

void OgreWidget::setJointPositions(const std::string &instance,
                                   const std::unordered_map<std::string, float> &q)
{
    for (const auto &kv : q)
        setJointPosition(instance, kv.first, kv.second);
}

// === RtssTechniqueResolver impl ===
Ogre::Technique *RtssTechniqueResolver::handleSchemeNotFound(
    uint16_t /*schemeIndex*/,
    const Ogre::String &schemeName,
    Ogre::Material *originalMaterial,
    unsigned short /*lodIndex*/,
    const Ogre::Renderable * /*rend*/)
{
    using namespace Ogre;
    using namespace Ogre::RTShader;

    if (schemeName != ShaderGenerator::DEFAULT_SCHEME_NAME || !mSG)
        return nullptr;

    // Get a handle to the material
    const String &matName = originalMaterial->getName();
    MaterialPtr mat = MaterialManager::getSingleton().getByName(matName);
    if (mat.isNull())
        return nullptr;

    // *** KEY FIX: pass a Material& (not name, not pointer) ***
    const bool ok = mSG->createShaderBasedTechnique(
        *mat,
        MaterialManager::DEFAULT_SCHEME_NAME, // source scheme (fixed-function)
        ShaderGenerator::DEFAULT_SCHEME_NAME, // destination (RTSS) scheme
        /*overProgrammable*/ false);

    if (!ok)
        return nullptr;

    // Return the technique that matches the RTSS scheme
    for (unsigned i = 0; i < mat->getNumTechniques(); ++i)
    {
        Technique *t = mat->getTechnique(i);
        if (t && t->getSchemeName() == ShaderGenerator::DEFAULT_SCHEME_NAME)
            return t;
    }
    return nullptr;
}

static void addRtssResourceLocations()
{
  namespace fs = std::filesystem;
  using RGM = Ogre::ResourceGroupManager;

  // RTSS looks for headers (e.g., OgreUnifiedShader.h) in the OgreInternal group.
  const Ogre::String kGroup = "OgreInternal";

  // Common Linux installs
  const char* roots[] = {
    "/usr/share/OGRE/RTShaderLib",
    "/usr/share/OGRE/Media/RTShaderLib",
    "/usr/share/OGRE/Media/RTShaderLib/GLSL",
    "/usr/share/OGRE/Media/ShadowVolume",
    "/usr/local/share/OGRE/RTShaderLib",
  };
  const char* sub[] = { "", "/GLSL", "/GLSLES", "/GLSL120", "/GLSL150", "/materials" };

  for (auto root : roots) {
    for (auto s : sub) {
      fs::path p = fs::path(root) / s;
      if (fs::exists(p)) {
        RGM::getSingleton().addResourceLocation(p.string(), "FileSystem", kGroup, /*recursive=*/false);
      }
    }
  }

#ifdef _WIN32
  // Option 1: use env var if you set it (point to ...\share\ogre\Media)
  if (const char* media = std::getenv("OGRE_MEDIA_DIR")) {
    fs::path base = fs::path(media) / "RTShaderLib";
    for (auto s : sub) {
      fs::path p = base / s;
      if (fs::exists(p)) {
        RGM::getSingleton().addResourceLocation(p.string(), "FileSystem", kGroup, false);
      }
    }
  }
  // Option 2: vcpkg default (adjust if your triplet differs)
  {
    fs::path base("C:/vcpkg/installed/x64-windows/share/ogre/Media/RTShaderLib");
    if (fs::exists(base)) {
      for (auto s : sub) {
        fs::path p = base / s;
        if (fs::exists(p)) {
          RGM::getSingleton().addResourceLocation(p.string(), "FileSystem", kGroup, false);
        }
      }
    }
  }
#endif

  // Make sure the internal group is ready for RTSS to load its headers
  if (!RGM::getSingleton().isResourceGroupInitialised(kGroup)) {
    RGM::getSingleton().initialiseResourceGroup(kGroup);
  }
}
