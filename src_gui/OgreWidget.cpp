#include "OgreWidget.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QColor>

#include <cstdint>
#include <cstring>
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
#include <OgreTextureManager.h>
#include <OgreRenderTexture.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreResourceGroupManager.h>
#include <OgreRTShaderSystem.h> // RTSS core + listener
#include <OgreSubEntity.h>
#include <OgrePixelFormat.h>
#include <OgreHardwarePixelBuffer.h>

// Assimp for STL/OBJ/DAE
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using std::string;
namespace fs = std::filesystem;

// Opacity of the look-at point marker. Semi-transparent on purpose: it is an
// aiming aid drawn over the scene, so it must not hide the geometry it sits on.
static constexpr float kFocalMarkerAlpha = 0.2f;

// How long the marker lingers after the last mouse event. Long enough that it
// does not flicker between the individual events of one drag, short enough that
// it is gone by the time the user settles on a new view.
static constexpr int kFocalMarkerHoldMs = 700;

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
    // NOT WA_PaintOnScreen: the window is created hidden and rendered into an
    // offscreen buffer that paintEvent() blits onto this widget. A native
    // paint-on-screen surface would be composited by X11 *above* every sibling
    // Qt widget, which would make the floating panel columns impossible to draw
    // on top of the 3D view.
    setAttribute(Qt::WA_NoSystemBackground, true);
    // NOT WA_OpaquePaintEvent: the widget must always paint *something*. With the
    // opaque flag Qt is free to skip the erase/paint of areas that were never
    // invalidated, which left raw black holes in the window while no frame had
    // been rendered yet (nothing calls update() before the model is loaded, and
    // resizes only invalidate the newly exposed strip).
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setUpdatesEnabled(true);
    // The WASD / QE navigation in keyPressEvent() only fires when the widget
    // itself has focus, so it must be able to receive it at all.
    setFocusPolicy(Qt::StrongFocus);

    // The look-at point marker is transient: it shows up while navigating and
    // this timer takes it away again once the interaction stops.
    mFocalMarkerHideTimer.setSingleShot(true);
    mFocalMarkerHideTimer.setInterval(kFocalMarkerHoldMs);
    connect(&mFocalMarkerHideTimer, &QTimer::timeout, this, [this]
            {
                if (mDragMode == DragMode::None)
                    hideFocalMarker();
                else
                    mFocalMarkerHideTimer.start(); // still dragging, keep it up
            });

    // Scratch buffer for the offscreen readback: RGB888 matches Ogre's PF_R8G8B8
    // so copyContentsToMemory() is a straight row-by-row copy.
    mFrameImage = QImage(std::max(1, width()), std::max(1, height()),
                         QImage::Format_RGB888);

    connect(&mTimer, &QTimer::timeout, this, &OgreWidget::onFrame);
    // Ogre is brought up in showEvent(), once the widget has its real size.
}

void OgreWidget::failOgre(const QString &message)
{
    shutdownOgre();
    mOgreFailed = true;

    qWarning() << "OGRE initialisation failed:" << message;
    emit initFailed(message);
}

bool OgreWidget::ensureOgre()
{
    if (mOgreInited)
        return true;
    if (mOgreFailed)
        return false;

    QString errorMsg;
    bool failed = false;
    try
    {
        initOgre();
        mOgreInited = true;
    }
    catch (const Ogre::Exception &e)
    {
        errorMsg = QString::fromUtf8(e.getFullDescription().c_str());
        failed = true;
    }
    catch (const std::exception &e)
    {
        errorMsg = QString::fromUtf8(e.what());
        failed = true;
    }
    catch (...)
    {
        errorMsg = QStringLiteral("Unknown error while initialising OGRE.");
        failed = true;
    }

    // Only tear OGRE down once the catch blocks have been left: destroying the
    // root unloads the render-system plugin, which would invalidate type info
    // that the still-live exception object needs in order to unwind.
    if (failed)
    {
        failOgre(errorMsg);
        return false;
    }

    resetView();
    mTimer.start(16);
    return true;
}
OgreWidget::~OgreWidget()
{
    shutdownOgre();
}

void OgreWidget::shutdownOgre()
{
    if (!mOgreInited && !mRoot)
    {
        // Nothing was brought up (or it already failed and was torn down);
        // just make sure the timer can't tick into freed state.
        mTimer.stop();
        return;
    }

    // 1) Stop rendering and drop the frame timer first: onFrame() must never
    //    run while we tear the scene/root down.
    mTimer.stop();

    // 2) Detach the RTSS technique resolver BEFORE the ShaderGenerator
    //    singleton is destroyed. Material teardown below can still call back
    //    into handleSchemeNotFound(); it must never see a dangling mSG.
    if (mRtssListener)
    {
        if (Ogre::MaterialManager::getSingletonPtr())
            Ogre::MaterialManager::getSingleton().removeListener(mRtssListener);
        delete mRtssListener;
        mRtssListener = nullptr;
    }

    // 3) Destroy all scene content while the SceneManager and MaterialManager
    //    are still alive.
    mRobots.clear();
    mBuiltJoints.clear();
    mLinkNodes.clear();

    //    Release all materials we created / referenced so they do not linger
    //    into RTSS + MaterialManager shutdown.
    //    (The ShaderGenerator holds per-material references; releasing them
    //    here avoids use-after-free during singleton destruction.)
    if (mScene)
    {
        // Detach & destroy every manual/entity under the world node.
        if (mWorld)
            destroySubtree(mScene, mWorld);
        mWorld = nullptr;
    }

    // 4) Destroy the ShaderGenerator singleton BEFORE the Root, and after all
    //    materials/scene managers that reference it. Ordering here is what
    //    prevents the classic OGRE-at-exit segmentation fault.
    if (auto *sg = Ogre::RTShader::ShaderGenerator::getSingletonPtr())
    {
        if (mScene)
            sg->removeSceneManager(mScene);
        Ogre::RTShader::ShaderGenerator::destroy();
    }
    mSG = nullptr;

    // 5) Tear down scene manager + camera + viewport via the Root. The Root
    //    destructor destroys all SceneManagers, RenderWindows and the loaded
    //    plugins (see the "Unloading library RenderSystem_GL3Plus" line).
    mScene = nullptr;
    mCamera = nullptr;
    mViewport = nullptr;
    mWindow = nullptr;
    mContextWindow = nullptr;
    mRenderTexture.setNull();
    mRoot.reset();

    mOgreInited = false;
}

// -------------------- Qt lifecycle --------------------
void OgreWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    setFocus(Qt::OtherFocusReason);
    // The render window is created here, not in the constructor: at construction
    // time the widget still has its provisional size, and a render target created
    // for that size only ever covered a small rectangle of the final widget.
    ensureOgre();
}
void OgreWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);

    // The offscreen target is rebuilt at the new size on the next onFrame(), via
    // ensureReadbackSize(); a RenderTexture cannot simply be resized here.
    if (mCamera && mViewport)
        mCamera->setAspectRatio(float(std::max(1, width())) / float(std::max(1, height())));
    if (mViewport)
        mViewport->setDimensions(0.0f, 0.0f, 1.0f, 1.0f);

    // Repaint straight away so the freshly exposed area is not left unpainted
    // until the next frame tick.
    update();
}
void OgreWidget::onFrame()
{
    // Continuous movement while a WASD/QE key is held (RViz FPS mode moves the
    // camera that way; in Orbit mode the same keys nudge the focal point).
    Ogre::Vector3 fwd, left, down;
    cameraBasis(fwd, left, down);
    const float speed = (mStyle == ViewStyle::Orbit) ? mFpsMoveSpeed * 0.2f : mFpsMoveSpeed;

    if (mKeyFwd)
        mSlew += fwd * speed;
    if (mKeyBack)
        mSlew -= fwd * speed;
    if (mKeyLeft)
        mSlew += left * speed;
    if (mKeyRight)
        mSlew -= left * speed;
    if (mKeyUp)
        mSlew -= down * speed;
    if (mKeyDown)
        mSlew += down * speed;

    if (mSlew.squaredLength() > 1e-12f)
        updateCamera();

    if (!mRoot || !mWindow)
        return;

    // Keep the render target in sync with the widget every frame. The window is
    // created while the widget still has its initial size, and some platforms do
    // not honour a later resizeEvent reliably; without this the frame would be
    // rendered into a stale sub-rectangle and the rest of the widget showed as
    // raw black.
    if (ensureReadbackSize())
    {
        if (mViewport)
            mViewport->setDimensions(0.0f, 0.0f, 1.0f, 1.0f);
        if (mCamera)
            mCamera->setAspectRatio(float(mFrameImage.width()) / float(mFrameImage.height()));
    }

    mRoot->renderOneFrame();

    readBackFrame();
}

/*
 * Copy the freshly rendered frame into mFrameImage.
 *
 * Two details make this less trivial than a memcpy:
 *
 *  1. Row pitch — QImage scanlines are 4-byte aligned, so
 *     mFrameImage.bytesPerLine() is usually wider than width()*3. Ogre writes
 *     into a tightly packed PixelBox, so the copy is done row by row with the
 *     two different pitches. A single contiguous copy would shift every row a
 *     bit further and smear the image into wrong colours.
 *
 *  2. Orientation — copyContentsToMemory() returns the rows in the order the
 *     QImage expects (top-down), so no vertical flip is applied; see the note
 *     at the copy loop.
 */
void OgreWidget::readBackFrame()
{
    if (mFrameImage.isNull() || !mWindow)
        return;

    const int w = mFrameImage.width();
    const int h = mFrameImage.height();

    mReadbackBuffer.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u);

    // Ask for exactly the layout QImage uses: PF_BYTE_RGB == R,G,B bytes, which
    // is QImage::Format_RGB888.
    //
    // This must be the *requested* format, not the target's native one. The
    // offscreen texture is created as PF_BYTE_BGR, and when the destination
    // PixelBox repeats the target's format Ogre skips the conversion and returns
    // its internal layout (typically 4 bytes/pixel BGRA on GL). Reading that with
    // a 3-byte stride shifted every row and swapped the channels — the
    // upside-down, wrong-coloured image. Naming a different format forces Ogre to
    // convert into the tightly packed buffer we actually want.
    Ogre::PixelBox dst(static_cast<size_t>(w), static_cast<size_t>(h),
                       /*depth=*/1, Ogre::PF_BYTE_RGB, mReadbackBuffer.data());
    mWindow->copyContentsToMemory(dst, Ogre::RenderTarget::FB_AUTO);

    // Row-by-row copy, honouring QImage's scanline padding (bytesPerLine() is
    // usually wider than width()*3 because scanlines are 4-byte aligned, while
    // Ogre writes a tightly packed buffer).
    //
    // No vertical flip here: copyContentsToMemory() already hands the rows back in
    // top-down order for this target. An extra reversal made the whole scene
    // render upside down.
    const uchar *src = mReadbackBuffer.data();
    const int srcPitch = w * 3;
    for (int y = 0; y < h; ++y)
    {
        memcpy(mFrameImage.scanLine(y),
               src + static_cast<size_t>(y) * srcPitch,
               static_cast<size_t>(srcPitch));
    }

    mFrameDirty = true;

    // Repaint this widget (not the whole window) so the blit lands on screen and
    // the floating panels above it stay untouched.
    update();
}

/*
 * Make sure the offscreen target and the readback buffer match the widget.
 * Returns true when the size changed in this call.
 *
 * A RenderTexture cannot be resized in place: the texture has to be dropped and
 * recreated at the new size, and the viewport re-attached to the new target.
 */
bool OgreWidget::ensureReadbackSize()
{
    if (!mWindow || !mRenderTexture)
        return false;

    const int w = std::max(1, width());
    const int h = std::max(1, height());

    if (static_cast<int>(mWindow->getWidth()) != w ||
        static_cast<int>(mWindow->getHeight()) != h)
    {
        // Detach the viewport before the target goes away.
        if (mViewport)
        {
            mWindow->removeViewport(mViewport->getZOrder());
            mViewport = nullptr;
        }

        Ogre::TextureManager *texMgr = Ogre::TextureManager::getSingletonPtr();
        texMgr->remove(mRenderTexture->getName());
        mRenderTexture.setNull();
        mRenderTexture = texMgr->createManual(
            "QtOgreTarget", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            Ogre::TEX_TYPE_2D, static_cast<Ogre::uint>(w),
            static_cast<Ogre::uint>(h),
            /*mipmaps=*/0, Ogre::PF_BYTE_BGR, Ogre::TU_RENDERTARGET);
        mWindow = mRenderTexture->getBuffer()->getRenderTarget();

        if (mCamera)
        {
            mViewport = mWindow->addViewport(mCamera);
            mViewport->setBackgroundColour(Ogre::ColourValue(0.2f, 0.2f, 0.2f));
            mViewport->setMaterialScheme(
                Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
        }
    }

    if (mFrameImage.width() == w && mFrameImage.height() == h)
        return false;

    mFrameImage = QImage(w, h, QImage::Format_RGB888);
    mFrameDirty = false;
    return true;
}

void OgreWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    if (mFrameDirty && !mFrameImage.isNull())
    {
        // mFrameImage is already in Qt's top-down order, so it is blitted as-is.
        p.drawImage(rect(), mFrameImage);
    }
    else
    {
        // Nothing rendered yet (or Ogre failed to start): leave the background
        // the theme paints, so the panels stay readable over an empty view.
        p.fillRect(rect(), QColor(45, 48, 54));
    }
}

// -------------------- Ogre init --------------------
void OgreWidget::initOgre()
{
    // No plugins.cfg; load plugins programmatically
    mRoot = std::make_unique<Ogre::Root>("", "", "log/ogre.log");
    selectAndInitRenderSystem();

    // A GL context is still required before any GL resource (textures, materials)
    // can be created, but it must never be a *visible* surface. A hidden render
    // window is not enough on X11: the driver still maps a real GLX window, and
    // copyContentsToMemory() then returned whatever the X server had composited
    // into that screen region — the GUI itself, smeared and mirrored, changing as
    // the main window moved over it. This 1x1 window only exists to own the
    // context.
    {
        Ogre::NameValuePairList params;
        params["currentGLContext"] = "false";
        params["hidden"] = "true";
        params["border"] = "none";
        mContextWindow = mRoot->createRenderWindow("QtOgreContext",
                                                   /*width=*/1, /*height=*/1,
                                                   /*fullScreen=*/false, &params);
        mContextWindow->setHidden(true);
    }

    // The actual drawing target: an offscreen texture. It has no on-screen
    // representation at all, so the readback can only ever contain Ogre's own
    // output.
    mRenderTexture = mRoot->getTextureManager()->createManual(
        "QtOgreTarget", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        Ogre::TEX_TYPE_2D,
        static_cast<Ogre::uint>(std::max(1, width())),
        static_cast<Ogre::uint>(std::max(1, height())),
        /*mipmaps=*/0,
        Ogre::PF_BYTE_BGR,
        Ogre::TU_RENDERTARGET);
    mWindow = mRenderTexture->getBuffer()->getRenderTarget();

    // Scene + camera. The camera is NOT parented to a node: updateCamera()
    // positions/orients it explicitly in the Z-up world frame, so what the
    // target shows is exactly the RViz convention.
    mScene = mRoot->createSceneManager(Ogre::ST_GENERIC);
    mCamera = mScene->createCamera("MainCamera");
    mCamera->setNearClipDistance(0.01f);
    mCamera->setFarClipDistance(2000.0f);
    mCamera->setAutoAspectRatio(true);
    mViewport = mWindow->addViewport(mCamera);
    mViewport->setBackgroundColour(Ogre::ColourValue(0.2f, 0.2f, 0.2f));

    // Z-up world node. Unlike a "convert NED -> Y-up" helper, this node only
    // carries the fixed rotation of the RViz scene under the Y-up Ogre root:
    //   ros(X forward, Y left, Z up)  ->  ogre(X right, Y forward, Z up-up)
    // The space is rotated 90 deg about +X, which is right-handed, so URDF is
    // consumed as-is and the scene is Z-up / right-handed like RViz. All child
    // geometry, joint axes and origins keep their AR/URDF numbers untouched
    // (no per-mesh flip, no mirrored handedness).
    mWorld = mScene->getRootSceneNode()->createChildSceneNode("rviz_world");
    mWorld->setOrientation(Ogre::Quaternion(Ogre::Degree(90.0f), Ogre::Vector3::UNIT_X));

    addRtssResourceLocations();

    // The GL3Plus render system has no fixed-function pipeline, so anything that
    // has to be shaded needs an RTSS generated technique. The viewport therefore
    // asks for the RTSS scheme, and only the lit materials are registered with
    // it. The grid / axes / vertex-colour helpers stay on the default scheme:
    // letting RTSS rewrite them would replace their per-vertex colours with its
    // own lighting result, which left the grid showing two flat greys (its alpha
    // fade gone) and made the axes disappear completely.
    enableRTSS();
    mViewport->setMaterialScheme(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
    ensureBaseMaterials();

    // Seed a shader technique for the lit materials so the very first frame
    // already has a valid technique instead of a one-frame fallback.
    if (auto *sg = Ogre::RTShader::ShaderGenerator::getSingletonPtr())
    {
        auto seed = [&](const Ogre::String &matName)
        {
            Ogre::MaterialPtr m = Ogre::MaterialManager::getSingleton().getByName(matName);
            if (m.isNull())
                return;
            sg->createShaderBasedTechnique(
                *m,
                Ogre::MaterialManager::DEFAULT_SCHEME_NAME,
                Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME,
                /*overProgrammable=*/false);
            sg->validateMaterial(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME, matName);
        };
        // Seed every material the scene uses, so the first frame already has a
        // valid RTSS technique and nothing falls back for a frame.
        seed("DefaultWhite");
        seed("GhostWhite");
        seed("GridUnlit");
        seed("GridLineUnlit");
    }

    recreateLighting();

    // Ambient term used by every lit material. It is what keeps surfaces facing
    // away from the lights readable, in the same spirit as RViz' shaded
    // RobotModel display. Without an explicit value Ogre leaves a low default
    // and the dark grey URDF meshes render almost black.
    mScene->setAmbientLight(Ogre::ColourValue(0.75f, 0.75f, 0.75f));

    // Camera first, so the grid is already framed when the window appears.
    updateCamera();

    // RViz-like ground grid: XY plane, normal +Z (i.e. Z points up), placed a
    // hair below the origin so it never fights with the model. The cell size is
    // the RViz default and the extent is grown to fit the model on load.
    createGrid("GroundGrid", 1.0f, 30);
    createAxes(0.05f);
    // No focal marker here on purpose: it is created on demand while the user
    // navigates, so an idle view stays clean.
}

void OgreWidget::selectAndInitRenderSystem()
{
    // Try GL3Plus first, then GL
    std::vector<string> plugins;
#if defined(Q_OS_WIN)
    plugins = {"RenderSystem_GL3Plus", "RenderSystem_GL"};
#else
    // Full paths first: Root::loadPlugin("RenderSystem_GL3Plus") is resolved by
    // DynLib against the working directory, not against the OGRE plugin dir.
    plugins = {
        "/usr/lib/x86_64-linux-gnu/OGRE/RenderSystem_GL3Plus",
        "/usr/lib/x86_64-linux-gnu/OGRE/RenderSystem_GL",
    };
#endif
    // Load the FIRST candidate that works and stop there. The previous version
    // had no break, so it went on to call loadPlugin() for the remaining paths
    // as well; those files do not exist, the resulting InternalErrorException
    // escaped the catch(...) of the following block entirely and unwound out of
    // initOgre(), leaving shutdownOgre() to destroy a half-initialised Root
    // (SIGSEGV in Root::uninstallPlugin) - the "black window" this whole
    // investigation was about.
    bool any = false;
    for (auto &p : plugins)
    {
        try
        {
            mRoot->loadPlugin(p);
            any = true;
            break; // do not try the remaining candidates: they are alternatives
        }
        catch (const Ogre::Exception &e)
        {
            qDebug() << "Render system plugin" << p.c_str() << "unavailable:"
                     << e.getFullDescription().c_str();
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

    // NOTE: no Root::loadPlugin() call on Linux. RTSS is not a plugin on
    // Debian/Ubuntu - it is the plain library libOgreRTShaderSystem.so, which
    // this program already links against. See loadRtssPlugin() for details.
    loadRtssPlugin();

    // Initialize the generator singleton. By this point the render window has
    // been created, which is what makes RenderSystem_GL3Plus register the "glsl"
    // high level program factory; initialize() then picks "glsl" as its target
    // language automatically. Doing this before createRenderWindow() makes it
    // fall back to "null" (the default target language is Cg) and every
    // generated material ends up invalid, so the ordering above matters.
    if (!ShaderGenerator::getSingletonPtr())
        ShaderGenerator::initialize();
    mSG = ShaderGenerator::getSingletonPtr();
    if (!mSG)
    {
        qWarning() << "RTSS unavailable: falling back to plain fixed-function materials.";
        return;
    }

    mSG->addSceneManager(mScene); // IMPORTANT: register your SceneManager

    // Install our listener once
    if (!mRtssListener)
    {
        mRtssListener = new RtssTechniqueResolver(mSG);
        Ogre::MaterialManager::getSingleton().addListener(mRtssListener);
    }

    // Make RTSS the active scheme so GL3Plus uses generated shaders
    Ogre::MaterialManager::getSingleton()
        .setActiveScheme(ShaderGenerator::DEFAULT_SCHEME_NAME);
}

// Make sure the RTSS shader generators are available.
//
// On Debian/Ubuntu RTSS is NOT a plugin. There is no Plugin_RTSS.so anywhere on
// the system: RTSS ships as the plain library libOgreRTShaderSystem.so, which
// this target already links through ${OGRE_LIBRARIES}. Calling
// Root::loadPlugin("Plugin_RTSS") therefore throws InternalErrorException, and
// because that call used to sit outside any try/catch inside enableRTSS(), the
// exception unwound all the way out of initOgre() -> ensureOgre() ->
// failOgre(), which tore the whole renderer down. That is why the widget showed
// a black window with nothing but
//     "Loading library Plugin_RTSS.so.1.12.10"
// followed by an immediate "OGRE Shutdown" in log/ogre.log.
//
// Windows is the one platform where the OGRE SDK genuinely ships
// Plugin_RTSS.dll, so the plugin is still loaded there.
bool OgreWidget::loadRtssPlugin()
{
#if defined(Q_OS_WIN)
    const char *names[] = {"Plugin_RTSS", "Plugin_RTSS_d"};
    for (const char *n : names)
    {
        try
        {
            mRoot->loadPlugin(n);
            return true;
        }
        catch (const Ogre::Exception &)
        {
            // try the next candidate
        }
    }
    qWarning() << "Plugin_RTSS.dll not found; RTSS may be unavailable.";
    return false;
#else
    // Statically linked component: nothing to load. ShaderGenerator will pick
    // up the render system's "glsl" factory once a window exists.
    return true;
#endif
}

// -------------------- Public API --------------------
bool OgreWidget::loadURDF(const std::string &urdf_path)
{
    if (!ensureOgre())
        return false;

    UrdfModel model;
    if (!UrdfLoader::loadFile(urdf_path, model))
    {
        qWarning() << "URDF load failed:" << QString::fromStdString(urdf_path);
        return false;
    }
    clearScene();
    // clearScene() destroys the nodes but these maps / the loader-side maps keep
    // stale pointers and names around, so a second load would either reuse dangling
    // nodes or throw ItemIdentityException on the duplicate names.
    mRobots.clear();
    mBuiltJoints.clear();
    mLinkNodes.clear();
    mModel = std::move(model);
    buildSceneFromURDF(urdf_path);
    fitGridToModel();
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
void OgreWidget::setViewStyle(ViewStyle s)
{
    mStyle = s;
    // Nothing else to do: updateCamera() derives the eye pose from whichever
    // representation the active style uses, so the view never jumps.
}

// RViz names ("Orbit", "FPS (first person)") mapped onto the two styles here.
void OgreWidget::setViewPreset(const QString &name)
{
    const QString n = name.trimmed().toLower();
    const bool fps = n.startsWith(QStringLiteral("fps")) ||
                     n.contains(QStringLiteral("first person"));
    setViewStyle(fps ? ViewStyle::FPS : ViewStyle::Orbit);
}

// --- Camera math -----------------------------------------------------------
// The scene lives in the ROS frame (X forward, Y left, Z up = RViz), which the
// "rviz_world" node maps onto Ogre's frame (X right, Y forward, Z up) with a
// fixed +90 deg rotation about X. The camera is created unparented, so its
// values have to be expressed in Ogre's frame, but the numbers below are the
// plain RViz numbers:  Z forward, Y left, X down (the ROS convention rotated
// the same way). This keeps every angle identical to RViz.
//
//   RViz relative vector (x, y, z)  ->  Ogre (x, -z, y)
//
Ogre::Vector3 OgreWidget::focalPoint() const
{
    return Ogre::Vector3(mTarget.x(), -mTarget.z(), mTarget.y());
}

void OgreWidget::setFocalPoint(const Ogre::Vector3 &p)
{
    mTarget = QVector3D(p.x, p.z, -p.y);
}

// Forward / left / down basis of the current camera, in Ogre coordinates.
void OgreWidget::cameraBasis(Ogre::Vector3 &fwd, Ogre::Vector3 &left, Ogre::Vector3 &down) const
{
    // The whole basis is built in the ROS frame (X forward, Y left, Z up) where
    // yaw/pitch have the same meaning as in RViz, and only then converted to
    // Ogre coordinates. Building `left` directly in Ogre coordinates was the
    // bug: it produced a Z-up horizontal vector inside a Y-up camera basis, so
    // basis and look direction were not orthogonal (dot(fwd,left) reached 0.88
    // for the default view) and orbit / pan moved the camera to the wrong place.
    const float cy = std::cos(mYaw), sy = std::sin(mYaw);
    const float cp = std::cos(mPitch), sp = std::sin(mPitch);

    // mYaw / mPitch always describe the LOOK direction, in both styles, so the
    // two modes stay interchangeable and view presets behave identically:
    //   yaw   = 0    -> looking along ROS +X (RViz' "front")
    //   yaw   = +90  -> looking along ROS +Y
    //   pitch = +45  -> looking upwards, pitch = -45 -> looking downwards
    const Ogre::Vector3 lookRos(cp * cy, cp * sy, sp);

    // Right-handed basis in the ROS frame (X forward, Y left, Z up):
    //   forward = lookRos
    //   left    = up x forward
    //   down    = forward x left
    const Ogre::Vector3 upRos(0.f, 0.f, 1.f);
    Ogre::Vector3 leftRos = upRos.crossProduct(lookRos);
    if (leftRos.squaredLength() < 1e-12f)
        leftRos = Ogre::Vector3(0.f, 1.f, 0.f); // straight up/down: pick any
    leftRos.normalise();
    const Ogre::Vector3 downRos = lookRos.crossProduct(leftRos);

    // ROS (x forward, y left, z up) -> Ogre (x right, y forward, z up), i.e. the
    // world node's Rx(+90) transform: (x, y, z) -> (x, -z, y). This is the same
    // mapping focalPoint() uses, so the eye offset and the focal point stay in
    // agreement (getting these two out of sync was what broke the default view).
    auto toOgre = [](const Ogre::Vector3 &v) { return Ogre::Vector3(v.x, -v.z, v.y); };

    fwd = toOgre(lookRos);
    left = toOgre(leftRos);
    down = toOgre(downRos);
}

// Keep the view clear of the poles: RViz clamps to +/-89 deg.
void OgreWidget::clampPitch()
{
    const float lim = Ogre::Math::HALF_PI - Ogre::Degree(1.0f).valueRadians();
    if (mPitch > lim)
        mPitch = lim;
    else if (mPitch < -lim)
        mPitch = -lim;
}

// Keyboard displacement queued between frames (WASD / QE).
void OgreWidget::applySlew()
{
    if (mSlew.squaredLength() < 1e-12f)
        return;
    setFocalPoint(focalPoint() + mSlew);
    mSlew = Ogre::Vector3::ZERO;
}

void OgreWidget::setCameraTarget(const QVector3D &t)
{
    // RViz' "Focal Point" is given in the fixed frame: (X forward, Y left, Z up).
    mTarget = t;
    updateCamera();
}

// Master switch for the marker. Enabling it does not draw anything by itself:
// the marker appears while the user navigates (see showFocalMarkerTemporarily).
void OgreWidget::setFocalMarkerVisible(bool on)
{
    mFocalMarkerEnabled = on;
    if (!on)
        hideFocalMarker();
}

// Build the marker into the scene and let the hide timer take it away again.
void OgreWidget::showFocalMarkerTemporarily()
{
    if (mFocalMarkerEnabled)
    {
        float dist = 1.0f;
        if (mCamera)
        {
            const float d = (mCamera->getPosition() - focalPoint()).length();
            if (d > 1e-3f)
                dist = d;
        }
        createFocalMarker(dist * 0.02f);
        mFocalMarkerShown = true;
    }
    // Restart the grace period on every event so the marker stays up for the
    // whole gesture and a little beyond it.
    mFocalMarkerHideTimer.start(kFocalMarkerHoldMs);
}

// Remove the marker from the scene.
void OgreWidget::hideFocalMarker()
{
    if (!mFocalMarkerShown && !mFocalMarkerNode)
        return;
    destroyWorldChild("FocalMarkerNode");
    if (mScene->hasManualObject("FocalMarkerCross"))
        mScene->destroyManualObject("FocalMarkerCross");
    if (mScene->hasManualObject("FocalMarkerBall"))
        mScene->destroyManualObject("FocalMarkerBall");
    mFocalMarkerNode = nullptr;
    mFocalMarkerShown = false;
}

// Move the orbit pivot onto the model and back off far enough to frame it, so
// the camera rotates around the model instead of around the world origin.
void OgreWidget::focusOnModel()
{
    Ogre::Vector3 mn, mx;
    if (modelBounds(mn, mx))
    {
        const Ogre::Vector3 centre = (mn + mx) * 0.5f;
        const Ogre::Vector3 size = mx - mn;
        const float extent = std::max(std::max(size.x, size.y), size.z);

        mTarget = QVector3D(centre.x, centre.y, centre.z);
        const float fov = mCamera ? mCamera->getFOVy().valueRadians() : 0.7f;
        mDistance = std::max(extent / (2.f * std::tan(fov * 0.5f)) * 2.2f, 0.25f);
    }
    else
    {
        // Nothing built yet: orbit the world origin at a sane distance.
        mTarget = QVector3D(0.f, 0.f, 0.f);
        mDistance = 1.0f;
    }
    mFocalDistSet = true;
    updateCamera();
}
void OgreWidget::resetView()
{
    // RViz' default start-up angles: eye behind and above the look-at point,
    // looking down at the scene. mPitch is the look elevation, so looking down
    // is negative, and yaw = -35 deg puts the camera to the right of the point
    // as seen from ROS +X. The look-at point and distance are left to
    // focusOnModel() so both paths frame the scene identically.
    mYaw = Ogre::Degree(-35.0f).valueRadians();
    mPitch = Ogre::Degree(-45.0f).valueRadians();
    mSlew = Ogre::Vector3::ZERO;
    focusOnModel();
}

// Same mapping RViz uses for its fixed-frame camera buttons: the vector is the
// direction the camera looks along, in the Z-up ROS frame.
void OgreWidget::setViewDirection(const QVector3D &dir)
{
    const float x = dir.x(), y = dir.y(), z = dir.z();

    // RViz' XAxesMapping: yaw is derived from |x| vs |y| so the four horizontal
    // buttons give exactly the angles RViz reports.
    if (std::abs(x) > std::abs(y))
    {
        mYaw = (x >= 0.f) ? 0.f : Ogre::Math::PI;
    }
    else
    {
        mYaw = (y >= 0.f) ? -Ogre::Math::HALF_PI : Ogre::Math::HALF_PI;
    }

    // Vertical component: fully up/down is clamped one degree short of the pole
    // (same margin RViz' lookAt() applies) so the up vector stays defined.
    const float lim = Ogre::Math::HALF_PI - Ogre::Degree(1.0f).valueRadians();
    if (z <= -0.5f)
        mPitch = -lim;
    else if (z >= 0.5f)
        mPitch = lim;
    else
        mPitch = 0.f;

    // mPitch is the look elevation in both styles now, so no negation is needed
    // when switching between Orbit and FPS.
    if (!mFocalDistSet)
    {
        // Look straight up/down puts the focal point underneath the eye; pull
        // the camera back a bit so the model is not half clipped by the near
        // plane on the first switch to a side view.
        mDistance = 1.0f;
    }

    clampPitch();
    updateCamera();
}


// Farthest visual origin from the model origin, in the ROS frame. Used to pick
// a grid size / camera distance that suits hands (0.1 m) and full robots (1 m)
// without either disappearing or turning into a single visible cell.
float OgreWidget::modelReach() const
{
    // Same reasoning as modelBounds(): measure the built scene, because the URDF
    // visual origins are usually all zero and the real placement is on the
    // chained joints. Falls back to the URDF numbers when nothing is built yet.
    Ogre::Vector3 mn, mx;
    if (modelBounds(mn, mx))
    {
        float reach = 0.f;
        for (const auto &c : {mn, mx})
            reach = std::max(reach, c.length());
        return reach;
    }

    float reach = 0.f;
    for (const auto &kv : mModel.links)
        for (const auto &v : kv.second.visuals)
        {
            const float r = std::sqrt(v.xyz[0] * v.xyz[0] +
                                      v.xyz[1] * v.xyz[1] +
                                      v.xyz[2] * v.xyz[2]);
            reach = std::max(reach, r);
        }
    return reach;
}

// Bounding box of the built scene, in the ROS frame.
//
// This deliberately walks the *scene graph* instead of the URDF data. A URDF
// typically writes <origin xyz="0 0 0"/> on every <visual> and carries the real
// placement on the chained <joint> elements, so reading the visual offsets
// directly yields a degenerate zero-size box and the camera would frame nothing.
// The node transforms already combine link, joint and visual placement.
bool OgreWidget::modelBounds(Ogre::Vector3 &minRos, Ogre::Vector3 &maxRos) const
{
    bool any = false;
    for (const auto &kv : mLinkNodes) // every visual lives under a link node
    {
        const Ogre::SceneNode *linkNode = kv.second;
        if (!linkNode)
            continue;
        Ogre::SceneNode::ConstChildNodeIterator it = linkNode->getChildIterator();
        while (it.hasMoreElements())
        {
            const auto *child = static_cast<const Ogre::SceneNode *>(it.getNext());
            if (!child || child->getName().find("_vis_") == std::string::npos)
                continue;

            // World position, then Ogre -> ROS: (x, y, z) -> (x, z, -y).
            const Ogre::Vector3 p = child->_getDerivedPosition();
            const Ogre::Vector3 pRos(p.x, p.z, -p.y);
            if (!any)
            {
                minRos = maxRos = pRos;
                any = true;
            }
            else
            {
                minRos.makeFloor(pRos);
                maxRos.makeCeil(pRos);
            }
        }
    }
    return any;
}

// RViz' Grid display shows 10 x 10 cells of 1 m around the origin, which is far
// too coarse for a 0.2 m hand: the model would sit inside a single cell. This
// picks the largest cell size from a 1-2-5 style ladder that keeps the model
// spanning a handful of cells, so the grid reads like RViz does for a robot.
void OgreWidget::fitGridToModel()
{
    if (!mWorld)
        return;

    const float reach = modelReach();
    float cell = 1.0f;
    int cells = 30;
    if (reach > 1e-4f)
    {
        // 1-2-5 ladder, so the cell stays a round number at every scale:
        // 1 mm .. 100 m. A hand (~0.2 m) lands on 5 cm cells, a full robot
        // (~1 m) on 50 cm cells - in both cases the model spans a few cells,
        // which is how RViz' 10 x 1 m grid frames a robot.
        static const float ladder[] = {
            0.001f, 0.002f, 0.005f,
            0.01f, 0.02f, 0.05f,
            0.1f, 0.2f, 0.5f,
            1.f, 2.f, 5.f, 10.f, 20.f, 50.f, 100.f};
        cell = ladder[sizeof(ladder) / sizeof(ladder[0]) - 1];
        for (float c : ladder)
        {
            if (c >= reach * 0.15f) // model spans ~7 cells (RViz' ratio)
            {
                cell = c;
                break;
            }
        }
        // Aim for a grid that comfortably fills the view at the default camera
        // distance, and keep the cell count even so a line lands exactly on the
        // origin. The extent is expressed in view-widths rather than as a
        // multiple of the model, otherwise a small model gets a grid that ends
        // in the middle of the screen (the visible "seam").
        const float wantHalf = std::max(reach * 8.f, cell * 20.f);
        cells = int(std::ceil(wantHalf / cell)) * 2;
        cells = std::clamp(cells, 20, 200);
    }

    // Rebuild both displays; createGrid()/createAxes() replace the old nodes.
    destroyWorldChild("GroundGridNode");
    destroyWorldChild("WorldAxesNode");

    createGrid("GroundGrid", cell, cells);
    createAxes(std::max(reach * 0.35f, cell * 0.1f));
    // The focal marker is rebuilt on the next interaction; if it happens to be
    // on screen right now, hide it because the scale changed underneath it.
    hideFocalMarker();
}

// Two directional lights so a shaded surface stays readable from every angle,
// the way RViz' shaded RobotModel display does.
//
// The directions are given in the *world* (Ogre, Y-up) frame, so the light
// nodes hang off the root scene node and keep an identity rotation. That is
// deliberate: putting them under the 90 deg "rviz_world" node and calling
// SceneNode::setDirection() on them produced a mirrored node basis (Ogre
// derives the node's axis vectors from that call while the parent rotation is
// still pending), which made the render system's normal matrix degenerate and
// the whole viewport came out black.
void OgreWidget::recreateLighting()
{
    if (!mScene)
        return;

    auto make = [&](const char *name, const Ogre::Vector3 &dir, float intensity, float spec)
    {
        // The light object itself is the existence marker. A node created with
        // createSceneNode() gets an auto-generated name, so it never shows up in
        // getChild()/getChildSceneNode() lookups - using those to detect
        // "already created" raised ItemIdentityException and aborted initOgre().
        if (mScene->hasLight(name))
            return; // already there
        auto *light = mScene->createLight(name);
        light->setType(Ogre::Light::LT_DIRECTIONAL);
        // The light travels *along* this vector: pointing it downwards (-Y in
        // the Y-up world) is what actually illuminates the scene.
        light->setDirection(dir.normalisedCopy());
        light->setDiffuseColour(Ogre::ColourValue(intensity, intensity, intensity));
        light->setSpecularColour(Ogre::ColourValue(spec, spec, spec));

        // Root-owned node with an identity rotation: the direction lives on the
        // Light, and staying out of the rotated "rviz_world" node avoids the
        // mirrored-basis problem described above.
        auto *node = mScene->createSceneNode();
        node->attachObject(light);
    };

    // Key light from above / front-left, fill from below / back-right, plus a
    // soft top-down light. Three mild lights rather than one strong key light
    // keep every surface legible no matter how the model is oriented.
    // The ROS frame is rotated +90 deg about world X, so ROS +Z (up) maps to
    // world +Y: "from above" therefore means a world direction with -Y.
    make("KeyLight", Ogre::Vector3(-0.5f, -0.8f, 0.4f), 0.75f, 0.15f);
    make("FillLight", Ogre::Vector3(0.6f, 0.6f, -0.5f), 0.45f, 0.0f);
    make("TopLight", Ogre::Vector3(0.15f, -1.0f, -0.2f), 0.40f, 0.0f);
}

// -------------------- Scene build --------------------
void OgreWidget::clearScene()
{
    if (!mScene)
        return;
    if (!mWorld)
    {
        mWorld = mScene->getRootSceneNode()->createChildSceneNode("rviz_world");
        mWorld->setOrientation(Ogre::Quaternion(Ogre::Degree(90.0f), Ogre::Vector3::UNIT_X));
    }
    mRobots.clear();
    mBuiltJoints.clear();
    mLinkNodes.clear();

    // Destroy all children under root: models, grid, axes and lights alike.
    // Everything is recreated below / on the next load, which keeps this path
    // free of the "item already exists" traps of a partial teardown.
    auto *root = mWorld;
    auto kids = root->getChildren();
    for (auto *n : kids)
        destroySubtree(mScene, static_cast<Ogre::SceneNode*>(n));

    recreateLighting();

    // Back to the RViz default grid / axes for an empty scene. The focal marker
    // is left out: it reappears on the next navigation event.
    createGrid("GroundGrid", 1.0f, 30);
    createAxes(0.05f);
    mFocalMarkerShown = false;
}

// Destroy a named child of mWorld if it is there.
//
// Node::getChild(const String&) throws ItemIdentityException for an unknown
// name - it is not a lookup that can return null - so the children have to be
// scanned by hand instead. Returns the child, or nullptr if there is none.
Ogre::SceneNode *OgreWidget::findWorldChild(const char *name) const
{
    if (!mWorld)
        return nullptr;
    for (auto *n : mWorld->getChildren())
    {
        if (n && n->getName() == name)
            return static_cast<Ogre::SceneNode *>(n);
    }
    return nullptr;
}

void OgreWidget::destroyWorldChild(const char *name)
{
    if (!mScene)
        return;
    if (auto *n = findWorldChild(name))
        destroySubtree(mScene, n);
}

void OgreWidget::destroySubtree(Ogre::SceneManager* sm, Ogre::SceneNode* n) {
  if (!sm || !n) return;

  // Detach & destroy attached MovableObjects
  const auto& objs = n->getAttachedObjects();
  for (auto* mo : objs) {
    // Destroy by concrete type to remove from SceneManager registries
    if (auto* ent = dynamic_cast<Ogre::Entity*>(mo)) {
      n->detachObject(ent);
      sm->destroyEntity(ent);
    } else if (auto* man = dynamic_cast<Ogre::ManualObject*>(mo)) {
      n->detachObject(man);
      sm->destroyManualObject(man);
    } else {
      n->detachObject(mo);
      sm->destroyMovableObject(mo);
    }
  }

  // Recursively destroy children
  // Copy list first; it changes as we destroy
  auto children = n->getChildren();
  for (auto* c : children) {
    destroySubtree(sm, static_cast<Ogre::SceneNode*>(c));
  }

  // Finally destroy this node
  if (n->getParentSceneNode())
    n->getParentSceneNode()->removeChild(n);
  sm->destroySceneNode(n);
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

        if (mScene->hasSceneNode(J.name + "_origin"))
        {
            mScene->destroySceneNode(J.name + "_origin");
        }
        auto *originNode = parent->createChildSceneNode(J.name + "_origin");
        originNode->setPosition(J.origin_xyz[0], J.origin_xyz[1], J.origin_xyz[2]);
        originNode->setOrientation(fromRPY(J.origin_rpy[0], J.origin_rpy[1], J.origin_rpy[2]));
        if (mScene->hasSceneNode(J.name + "_axis"))
        {
            mScene->destroySceneNode(J.name + "_axis");
        }
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
    if (mScene->hasSceneNode(L.name))
    {
        mScene->destroySceneNode(L.name);
    }
    auto *node = mWorld->createChildSceneNode(L.name);
    mLinkNodes[L.name] = node;
}

void OgreWidget::attachVisual(Ogre::SceneNode *linkNode, const UrdfVisual &V, int idx, const std::string &urdf_dir)
{
    if (mScene->hasSceneNode(linkNode->getName() + "_vis_" + std::to_string(idx)))
    {
        mScene->destroySceneNode(linkNode->getName() + "_vis_" + std::to_string(idx));
    }
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
          s = V.scale;
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

// Defined below with the other material helpers; declared here because the
// grid / axes builders need it while they create their own materials.
static void ensureUnlitMaterial(const char *name, const Ogre::ColourValue &colour);
static void ensureTranslucentUnlitMaterial(const char *name, const Ogre::ColourValue &colour);

void OgreWidget::createGrid(const std::string &name, float cellSize, int cellsPerSide)
{
    if (mScene->hasManualObject(name))
        mScene->destroyManualObject(name);

    // RViz' Grid display:
    //   * extent  = cellsPerSide x cellSize
    //   * every "Line Count" (10) line is drawn in the brighter "Line" colour
    //     instead of the dim "Cell" colour
    //   * the world axes are provided separately by createAxes()
    const float half = cellSize * cellsPerSide;
    const int cells = std::max(1, int(std::ceil(half / cellSize))); // full cells per side
    const int halfLines = cells / 2;
    const float hDim = cells * cellSize * 0.5f;

    Ogre::ManualObject *mo = mScene->createManualObject(name);

    // Two passes: the dim "cell" lines and the bright "line" lines, matching
    // RViz' two grid colours. Per-vertex colour and alpha fading are not
    // available on GL3Plus (no fixed-function stage), so the fade is dropped
    // and the two colours are expressed as two separate RTSS sections.
    // The vertices keep the plain ROS numbers (x, y, z = 0); the "rviz_world"
    // node performs the Y-up/ROS mapping, so Z ends up pointing up.
    const int lineStep = 10;

    auto addLines = [&](bool bright)
    {
        mo->begin(bright ? "GridLineUnlit" : "GridUnlit", Ogre::RenderOperation::OT_LINE_LIST);
        for (int i = -halfLines; i <= halfLines; ++i)
        {
            const bool isMajor = (i % lineStep) == 0;
            if (isMajor != bright)
                continue;
            const float d = i * cellSize;

            mo->position(-hDim, d, 0.f);
            mo->normal(0, 0, 1);
            mo->position(hDim, d, 0.f);
            mo->normal(0, 0, 1);

            mo->position(d, -hDim, 0.f);
            mo->normal(0, 0, 1);
            mo->position(d, hDim, 0.f);
            mo->normal(0, 0, 1);
        }
        mo->end();
    };
    addLines(/*bright=*/false);
    addLines(/*bright=*/true);

    auto *node = mWorld->createChildSceneNode(name + "Node");
    // A hair below the origin so the model's own z = 0 faces win the depth test.
    node->setPosition(0.f, 0.f, -0.0005f * std::max(half, 1.f));
    node->attachObject(mo);
}

void OgreWidget::createAxes(float length)
{
    if (mScene->hasManualObject("WorldAxes"))
        mScene->destroyManualObject("WorldAxes");

    // RViz' Axes display: X red, Y green, Z blue, drawn from the origin.
    //
    // GL3Plus has no fixed-function stage, so per-vertex colours are not an
    // option: each axis gets its own single-colour material instead and the
    // three segments are emitted as separate sections. The materials are plain
    // RTSS-shaded ones, which is what the render system can actually draw.
    struct AxisDef
    {
        const char *material;
        Ogre::ColourValue colour;
        Ogre::Vector3 dir;
    };
    const AxisDef axes[3] = {
        {"AxisXUnlit", Ogre::ColourValue(0.90f, 0.15f, 0.15f), Ogre::Vector3::UNIT_X}, // X red
        {"AxisYUnlit", Ogre::ColourValue(0.15f, 0.85f, 0.15f), Ogre::Vector3::UNIT_Y}, // Y green
        {"AxisZUnlit", Ogre::ColourValue(0.20f, 0.45f, 1.00f), Ogre::Vector3::UNIT_Z}, // Z blue
    };
    for (const auto &a : axes)
    {
        ensureUnlitMaterial(a.material, a.colour);
        if (auto *sg = Ogre::RTShader::ShaderGenerator::getSingletonPtr())
            sg->validateMaterial(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME,
                                 a.material);
    }

    auto *mo = mScene->createManualObject("WorldAxes");
    for (const auto &a : axes)
    {
        mo->begin(a.material, Ogre::RenderOperation::OT_LINE_LIST);
        mo->position(Ogre::Vector3::ZERO);
        mo->position(a.dir * length);
        mo->end();
    }

    auto *node = mWorld->createChildSceneNode("WorldAxesNode");

    node->attachObject(mo);
}

// Marker for the camera's look-at point - RViz calls it the "Focal Point".
//
// Drawn the way RViz' focal-point indicator reads: a small sphere with a
// three-axis cross through it. The cross keeps the exact pivot readable even
// when the sphere is only a few pixels wide, and the amber colour stands out
// against both the grey grid and the grey model.
//
// The marker is semi-transparent because it is an overlay, not part of the
// scene: at full opacity it competes with the model it is supposed to help you
// aim at. The alpha leaves the geometry readable while still letting whatever
// sits underneath show through.
void OgreWidget::createFocalMarker(float size)
{
    // Rebuild from scratch: the geometry is tiny and the size changes with the
    // viewing distance, so regenerating is simpler than rescaling in place.
    destroyWorldChild("FocalMarkerNode");
    if (mScene->hasManualObject("FocalMarkerCross"))
        mScene->destroyManualObject("FocalMarkerCross");
    if (mScene->hasManualObject("FocalMarkerBall"))
        mScene->destroyManualObject("FocalMarkerBall");

    // Alpha is carried in the emissive colour, which is the only channel the
    // unlit material path uses.
    ensureTranslucentUnlitMaterial("FocalCrossUnlit", Ogre::ColourValue(1.00f, 0.45f, 0.00f, kFocalMarkerAlpha));
    ensureTranslucentUnlitMaterial("FocalBallUnlit", Ogre::ColourValue(1.00f, 0.80f, 0.10f, kFocalMarkerAlpha));
    if (auto *sg = Ogre::RTShader::ShaderGenerator::getSingletonPtr())
    {
        sg->validateMaterial(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME, "FocalCrossUnlit");
        sg->validateMaterial(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME, "FocalBallUnlit");
    }

    const float arm = std::max(size, 1e-5f);

    // Three-axis cross. Slightly longer than the sphere radius so its tips stick
    // out and read as a cross rather than as a blob.
    auto *cross = mScene->createManualObject("FocalMarkerCross");
    cross->begin("FocalCrossUnlit", Ogre::RenderOperation::OT_LINE_LIST);
    const Ogre::Vector3 dirs[3] = {Ogre::Vector3::UNIT_X,
                                   Ogre::Vector3::UNIT_Y,
                                   Ogre::Vector3::UNIT_Z};
    for (const auto &d : dirs)
    {
        cross->position(-d * arm);
        cross->position(d * arm);
    }
    cross->end();

    // Low-poly sphere; cheap and always faces the viewer.
    auto *ball = mScene->createManualObject("FocalMarkerBall");
    ball->begin("FocalBallUnlit", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    {
        const int rings = 8, segs = 12;
        const float rad = arm * 0.5f;
        for (int r = 0; r <= rings; ++r)
        {
            const float phi = (float(r) / rings) * Ogre::Math::PI;
            for (int s = 0; s <= segs; ++s)
            {
                const float th = (float(s) / segs) * Ogre::Math::TWO_PI;
                const Ogre::Vector3 n(std::sin(phi) * std::cos(th),
                                      std::sin(phi) * std::sin(th),
                                      std::cos(phi));
                ball->position(n * rad);
                ball->normal(n);
            }
        }
        auto idx = [segs](int r, int s) { return r * (segs + 1) + s; };
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < segs; ++s)
            {
                const int i0 = idx(r, s), i1 = idx(r, s + 1);
                const int i2 = idx(r + 1, s), i3 = idx(r + 1, s + 1);
                ball->triangle(i0, i2, i1);
                ball->triangle(i1, i2, i3);
            }
    }
    ball->end();

    mFocalMarkerNode = mWorld->createChildSceneNode("FocalMarkerNode");
    mFocalMarkerNode->attachObject(cross);
    mFocalMarkerNode->attachObject(ball);
    mFocalMarkerSize = arm;
    syncFocalMarker();
}

// Put the marker on the current look-at point. mTarget is already stored in the
// ROS frame, which is the frame mWorld's children live in.
void OgreWidget::syncFocalMarker()
{
    if (mFocalMarkerNode)
        mFocalMarkerNode->setPosition(mTarget.x(), mTarget.y(), mTarget.z());
}

// Keep the marker a roughly constant size on screen by scaling it with the
// viewing distance. Without this it vanishes when zoomed out and swallows the
// model when zoomed in.
void OgreWidget::resizeFocalMarkerToView()
{
    if (!mFocalMarkerNode || !mCamera)
        return;

    float dist = (mCamera->getPosition() - focalPoint()).length();
    if (dist < 1e-3f)
        dist = (mStyle == ViewStyle::Orbit) ? mDistance : 1.0f;

    const float want = dist * 0.02f; // ~2% of the view height
    if (std::abs(want - mFocalMarkerSize) < mFocalMarkerSize * 0.05f)
        return; // close enough; do not rebuild the geometry every frame

    createFocalMarker(want);
}

// -------------------- Mesh loading --------------------
Ogre::MovableObject *OgreWidget::createAnyMeshOrManual(const std::string &name,
                                                       const std::string &absPath,
                                                       const std::array<float, 3> &scale)
{
    // 1) Native .mesh (OGRE's own format) if the file happens to be one.
    if (absPath.size() > 5 && absPath.compare(absPath.size() - 5, 5, ".mesh") == 0)
    {
        try
        {
            return mScene->createEntity(name, absPath);
        }
        catch (const Ogre::Exception &)
        { /* not in a resource location: fall through to Assimp */
        }
    }

    // 2) Assimp for STL / OBJ / DAE / FBX.
    //    aiProcess_GenSmoothNormals matches RViz' STL loader (it smooths the
    //    facets into a solid-looking surface); aiProcess_JoinIdenticalVertices
    //    + OptimizeMeshes keep the vertex count sane for the 21-file URDFs.
    Assimp::Importer importer;
    const aiScene *sc = importer.ReadFile(absPath,
                                          aiProcess_Triangulate |
                                              aiProcess_GenSmoothNormals |
                                              aiProcess_JoinIdenticalVertices |
                                              aiProcess_ImproveCacheLocality |
                                              aiProcess_SortByPType |
                                              aiProcess_OptimizeMeshes |
                                              aiProcess_OptimizeGraph);
    if (!sc || !sc->mRootNode)
    {
        qWarning() << "Assimp failed to read mesh:" << absPath.c_str()
                   << importer.GetErrorString();
        return nullptr;
    }

    auto *mo = mScene->createManualObject(name + "_mo");
    mo->begin("DefaultWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);

    unsigned vbase = 0;
    std::function<void(const aiNode *, const aiMatrix4x4 &)> visit =
        [&](const aiNode *node, const aiMatrix4x4 &parent)
    {
        const aiMatrix4x4 M = parent * node->mTransformation;
        // Normals must be transformed by the inverse-transpose, otherwise a file
        // with non-uniform node scaling lights up incorrectly (this is what made
        // the FBX-based URDF look washed out / inside-out).
        aiMatrix3x3 N = aiMatrix3x3(M);
        N.Inverse().Transpose();

        for (unsigned m = 0; m < node->mNumMeshes; ++m)
        {
            const aiMesh *mesh = sc->mMeshes[node->mMeshes[m]];
            const unsigned nv = mesh->mNumVertices;
            if (nv == 0)
                continue;

            for (unsigned v = 0; v < nv; ++v)
            {
                const aiVector3D P = M * mesh->mVertices[v];
                mo->position(scale[0] * P.x, scale[1] * P.y, scale[2] * P.z);

                Ogre::Vector3 n(0.f, 0.f, 1.f);
                if (mesh->HasNormals())
                {
                    n = Ogre::Vector3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
                    n = Ogre::Vector3(N.a1, N.a2, N.a3) * n.x +
                        Ogre::Vector3(N.b1, N.b2, N.b3) * n.y +
                        Ogre::Vector3(N.c1, N.c2, N.c3) * n.z;
                }
                // Scaling is applied per axis, so the normal has to be scaled by
                // its inverse to stay perpendicular to the transformed surface.
                if (std::abs(scale[0] - 1.f) > 1e-6f ||
                    std::abs(scale[1] - 1.f) > 1e-6f ||
                    std::abs(scale[2] - 1.f) > 1e-6f)
                {
                    n.x /= (scale[0] != 0.f ? scale[0] : 1.f);
                    n.y /= (scale[1] != 0.f ? scale[1] : 1.f);
                    n.z /= (scale[2] != 0.f ? scale[2] : 1.f);
                }
                if (n.squaredLength() > 1e-12f)
                    n.normalise();
                else
                    n = Ogre::Vector3(0.f, 0.f, 1.f);
                mo->normal(n);
            }

            for (unsigned f = 0; f < mesh->mNumFaces; ++f)
            {
                const aiFace &face = mesh->mFaces[f];
                if (face.mNumIndices != 3)
                    continue; // aiProcess_Triangulate guarantees 3, but stay safe
                const unsigned a = face.mIndices[0], b = face.mIndices[1], c = face.mIndices[2];
                if (a >= nv || b >= nv || c >= nv)
                    continue;
                mo->triangle(vbase + a, vbase + b, vbase + c);
            }
            vbase += nv;
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
    if (!mCamera)
        return;

    clampPitch();
    mDistance = std::max(1e-3f, mDistance);
    applySlew();

    Ogre::Vector3 fwd, left, down;
    cameraBasis(fwd, left, down);

    const Ogre::Vector3 focal = focalPoint();
    // Orbit: the eye sits at a fixed distance behind the focal point.
    // FPS:   the focal point is virtual; only the orientation matters.
    const Ogre::Vector3 eye = (mStyle == ViewStyle::Orbit)
                                  ? focal - fwd * mDistance
                                  : focal;

    mCamera->setPosition(eye);

    // Build the orientation from an orthonormal camera basis: X = left (screen
    // right is -left), Y = up, Z = -forward (Ogre cameras look along -Z).
    //
    // Two things must hold here and neither did before:
    //   * "up" is the world up (ROS +Z) projected so it is perpendicular to the
    //     view direction. Feeding the raw world up made the triple
    //     (left, up, -fwd) non-orthogonal, so the quaternion came out wrong.
    //   * the world up expressed in Ogre coordinates is toOgre(ros +Z) =
    //     (0, -1, 0), NOT Ogre::UNIT_Z. Using UNIT_Z treated a horizontal axis
    //     as "up", which rolled the camera 90 deg - the reason the ground grid
    //     rendered as a vertical wall.
    const Ogre::Vector3 worldUp = -left.crossProduct(fwd); // world up, Ogre coords
    Ogre::Vector3 up = worldUp - fwd * worldUp.dotProduct(fwd); // perpendicular part
    if (up.squaredLength() < 1e-8f)
        up = down * -1.f; // looking straight up/down: fall back to the basis
    up.normalise();

    // Re-orthogonalise "left" too, so all three axes are exactly perpendicular
    // even after the projection above.
    Ogre::Vector3 right = fwd.crossProduct(up);
    right.normalise();

    mCamera->setOrientation(Ogre::Quaternion(right, up, -fwd));

    // Keep the look-at marker glued to the point we orbit around, and scale it
    // to the current viewing distance so it stays legible at any zoom.
    resizeFocalMarkerToView();
    syncFocalMarker();
}

// -------------------- RViz-like navigation --------------------
// Button mapping matches RViz' default view controller:
//   left       Orbit (RViz) / Look around (FPS)
//   middle     Move the focal point in the camera plane
//   right      Zoom (dolly)
//   shift+left Move (same as middle; handy on laptops without a middle button)
void OgreWidget::mousePressEvent(QMouseEvent *e)
{
    mLastMouse = e->pos();
    setFocus(Qt::MouseFocusReason);

    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && shift))
        mDragMode = DragMode::Move;
    else if (e->button() == Qt::RightButton)
        mDragMode = DragMode::Zoom;
    else if (e->button() == Qt::LeftButton)
        mDragMode = (mStyle == ViewStyle::Orbit) ? DragMode::Orbit : DragMode::Look;
    else
        mDragMode = DragMode::None;

    // The marker is a navigation aid, so it appears as soon as the user starts
    // interacting with the view.
    if (mDragMode != DragMode::None)
        showFocalMarkerTemporarily();
}
void OgreWidget::mouseReleaseEvent(QMouseEvent * /*e*/)
{
    mDragMode = DragMode::None;
    // Keep it up briefly after the button is released: hiding it the instant the
    // drag ends makes it flicker on every click, and the user usually wants to
    // see where the pivot ended up.
    if (mFocalMarkerEnabled)
        mFocalMarkerHideTimer.start();
}
void OgreWidget::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint d = e->pos() - mLastMouse;
    mLastMouse = e->pos();
    if (!mCamera || mDragMode == DragMode::None || (d.x() == 0 && d.y() == 0))
        return;

    // Keep the marker up for the whole gesture.
    showFocalMarkerTemporarily();

    // Screen-space drag expressed in a Z-up camera frame: +X right, +Y up,
    // +Z towards the viewer. This is the frame RViz uses for its controls.
    const Ogre::Vector3 screen(-float(d.x()), float(d.y()), 0.f);

    switch (mDragMode)
    {
    case DragMode::Orbit:
    {
        // Orbit around the focal point.
        //
        // Only mYaw / mPitch are updated here and updateCamera() does all of the
        // orientation work. That is deliberate: the camera basis has no roll by
        // construction (it is rebuilt from yaw/pitch every frame), so composing
        // the rotation directly onto the camera quaternion - which is what this
        // used to do - produced a rolled orientation that the very next
        // updateCamera() could not reproduce, and the view snapped one frame
        // after every drag. Driving yaw/pitch keeps both paths identical.
        //
        // Angle sign convention, matching RViz' orbit controller:
        //   dragging right -> the model turns right, i.e. the camera swings to
        //                     the LEFT around it (the scene follows the cursor)
        //   dragging down  -> the model turns down, so the camera rises and you
        //                     end up looking at its top
        // screen.x is the drag to the right, screen.y the drag downwards, and
        // mPitch is the look elevation (negative = looking down).
        //
        // Both Y signs have to agree with the image: dragging right must move the
        // camera towards the screen-left basis vector. mYaw grows towards ROS +Y
        // (to the left of a +X view), so following the cursor means ADDING the
        // rightward drag to mYaw. Subtracting it (the previous code) orbited the
        // camera the other way, which made the scene drift against the cursor -
        // most obvious when panning along the horizon.
        mYaw += screen.x * mOrbitSpeed;
        mPitch -= screen.y * mOrbitSpeed;
        clampPitch();

        mFocalDistSet = true;
        break; // fall through to the shared updateCamera() call below
    }
    case DragMode::Look:
    {
        // FPS mode: the drag turns the head in place, so only yaw/pitch change
        // (the eye stays put). Driving the angles directly - instead of composing
        // a quaternion and re-deriving from it - keeps this consistent with
        // updateCamera(), which is the same fix applied to Orbit above.
        //
        // Sign convention matches Orbit so the two modes feel alike: the drag
        // turns the view in the direction the cursor moves (dragging right looks
        // to the right, dragging down looks down).
        mYaw += screen.x * mFpsLookSpeed;
        mPitch -= screen.y * mFpsLookSpeed;
        clampPitch();
        break;
    }
    case DragMode::Move:
    {
        // Shift the focal point in the camera plane, scaled with the distance so
        // the scene tracks the cursor one to one. The offsets are expressed in
        // the live camera basis (right / up) rather than in a raw local frame,
        // so panning stays level no matter how the camera is oriented.
        Ogre::Vector3 fwd, left, down;
        cameraBasis(fwd, left, down);
        const Ogre::Vector3 right = left;
        const Ogre::Vector3 up = down;

        float dist = (mCamera->getPosition() - focalPoint()).length();
        if (dist < 1e-3f)
            dist = mDistance;
        const float k = mPanSpeed * dist;

        const Ogre::Vector3 offset = right * (-k * screen.x) + up * (k * screen.y);
        setFocalPoint(focalPoint() + offset);
        mFocalDistSet = true;
        break;
    }
    case DragMode::Zoom:
    {
        // Dolly along the viewing direction (RViz' "Zoom"), NOT a fov zoom, and
        // crucially leave the focal point alone: the camera only changes how far
        // it is from the look-at point, so the orbit pivot stays exactly where it
        // was. Moving the focal point here would drag the pivot with the mouse
        // and break the "always rotate around the look-at point" behaviour.
        if (mStyle == ViewStyle::Orbit)
        {
            // Multiplicative, so one drag notch feels the same at any zoom level.
            mDistance *= (1.f + mZoomSpeed * 0.1f * screen.y);
            mDistance = std::max(mDistance, 1e-3f);
            mFocalDistSet = true;
        }
        else
        {
            // FPS has no orbit radius: walk the eye along its view direction.
            Ogre::Vector3 fwd, left, down;
            cameraBasis(fwd, left, down);
            mSlew += fwd * (mZoomSpeed * 0.1f * screen.y);
        }
        break;
    }
    default: // DragMode::None
        return;
    }

    updateCamera();
}

void OgreWidget::wheelEvent(QWheelEvent *e)
{
    // One notch (120 units) moves the camera a fraction of its current distance
    // closer to the look-at point, exactly like RViz' wheel zoom. The focal
    // point is deliberately untouched so the orbit pivot stays put.
    const float steps = float(e->angleDelta().y()) / 120.f;
    if (steps == 0.f || !mCamera)
        return;

    // Zooming is navigation too, so it reveals the pivot just like a drag does.
    showFocalMarkerTemporarily();

    if (mStyle == ViewStyle::Orbit)
    {
        // Multiplicative so each notch feels identical at any zoom level.
        mDistance *= std::pow(1.f - mZoomSpeed, steps);
        mDistance = std::max(mDistance, 1e-3f);
        mFocalDistSet = true;
    }
    else
    {
        // FPS has no orbit radius: walk the eye along its view direction.
        Ogre::Vector3 fwd, left, down;
        cameraBasis(fwd, left, down);
        mSlew += fwd * (mZoomSpeed * steps);
    }
    updateCamera();
}

void OgreWidget::keyPressEvent(QKeyEvent *e)
{
    if (e->isAutoRepeat())
    {
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key())
    {
    case Qt::Key_W: mKeyFwd = true; break;
    case Qt::Key_S: mKeyBack = true; break;
    case Qt::Key_A: mKeyLeft = true; break;
    case Qt::Key_D: mKeyRight = true; break;
    case Qt::Key_Q: mKeyUp = true; break;
    case Qt::Key_E: mKeyDown = true; break;
    case Qt::Key_R: resetView(); return;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
}
void OgreWidget::keyReleaseEvent(QKeyEvent *e)
{
    if (e->isAutoRepeat())
    {
        QWidget::keyReleaseEvent(e);
        return;
    }
    switch (e->key())
    {
    case Qt::Key_W: mKeyFwd = false; break;
    case Qt::Key_S: mKeyBack = false; break;
    case Qt::Key_A: mKeyLeft = false; break;
    case Qt::Key_D: mKeyRight = false; break;
    case Qt::Key_Q: mKeyUp = false; break;
    case Qt::Key_E: mKeyDown = false; break;
    default:
        QWidget::keyReleaseEvent(e);
        return;
    }
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

// Flat-coloured material for the grid and the world axes.
//
// Two hard constraints of this render setup shape the implementation:
//
//  1. GL3Plus has no fixed-function pipeline, so EVERY pass must come from a
//     shader. A "setLightingEnabled(false)" material therefore cannot be drawn
//     at all - and RTSS refuses to synthesise a shader for it too
//     (verify: ShaderGenerator::createShaderBasedTechnique() returns false).
//  2. Per-vertex colours (TVC_DIFFUSE) need that same fixed-function stage, so
//     they are not available either.
//
// The only workable route is a normal lit material whose colour does not depend
// on the scene lights. That is achieved with a fully emissive pass: the emissive
// term is added after lighting, so the surface renders as a flat patch of the
// requested colour regardless of how the model is lit.
static void ensureUnlitMaterial(const char *name, const Ogre::ColourValue &colour)
{
    using RGM = Ogre::ResourceGroupManager;
    if (Ogre::MaterialManager::getSingleton().resourceExists(name))
        return;
    auto m = Ogre::MaterialManager::getSingleton().create(name, RGM::DEFAULT_RESOURCE_GROUP_NAME);
    auto *p = m->getTechnique(0)->getPass(0);
    // Lighting must stay enabled for RTSS (see 1. above), but cutting the
    // diffuse response and pushing the colour through emissive makes the result
    // independent of the lights, which is the "unlit" look wanted here.
    p->setLightingEnabled(true);
    p->setAmbient(Ogre::ColourValue::Black); // no ambient response
    p->setDiffuse(Ogre::ColourValue::Black); // no diffuse response
    p->setSpecular(Ogre::ColourValue::Black);
    p->setEmissive(colour);                  // flat self-lit colour
    p->setDepthWriteEnabled(true);
}

// Same flat look as ensureUnlitMaterial(), but blended so the marker reads as an
// overlay instead of a solid object sitting in the scene.
//
// Transparent geometry needs a few extra pass settings on GL3Plus/RTSS:
//   * SBT_TRANSPARENT_ALPHA  - actually blend the alpha
//   * depth write off         - otherwise the marker hides whatever is drawn
//                               behind it even where it is nearly invisible
//   * depth bias              - lift it a hair off coplanar geometry so it does
//                               not z-fight with the grid it sits on
static void ensureTranslucentUnlitMaterial(const char *name, const Ogre::ColourValue &colour)
{
    using RGM = Ogre::ResourceGroupManager;
    if (Ogre::MaterialManager::getSingleton().resourceExists(name))
        return;
    auto m = Ogre::MaterialManager::getSingleton().create(name, RGM::DEFAULT_RESOURCE_GROUP_NAME);
    auto *p = m->getTechnique(0)->getPass(0);
    p->setLightingEnabled(true);
    p->setAmbient(Ogre::ColourValue::Black);
    p->setDiffuse(Ogre::ColourValue::Black);
    p->setSpecular(Ogre::ColourValue::Black);
    p->setEmissive(colour); // alpha carried here as well
    p->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
    p->setDepthWriteEnabled(false);
    p->setDepthBias(2.f, 4.f);
}

void OgreWidget::ensureBaseMaterials()
{
    using RGM = Ogre::ResourceGroupManager;

    // --- Model surfaces -----------------------------------------------------
    // "DefaultWhite" is used for every URDF mesh that does not carry a colour.
    // RViz' RobotModel display shades links in that exact grey (0.7) on its
    // dark viewport, so lit geometry reads the same way here.
    if (!Ogre::MaterialManager::getSingleton().resourceExists("DefaultWhite"))
    {
        auto m = Ogre::MaterialManager::getSingleton().create(
            "DefaultWhite", RGM::DEFAULT_RESOURCE_GROUP_NAME);
        m->setReceiveShadows(true);
        auto *p = m->getTechnique(0)->getPass(0);
        p->setLightingEnabled(true);
        // Use the pass's own ambient instead of the scene's, and keep it high:
        // RTSS evaluates ambient + diffuse*N.L, so a low ambient left every
        // surface facing away from the key light almost black. RViz' RobotModel
        // display behaves the same way - the whole link stays its grey (0.7)
        // from any viewing angle.
        p->setAmbient(Ogre::ColourValue(0.55f, 0.55f, 0.55f));
        p->setDiffuse(Ogre::ColourValue(0.70f, 0.70f, 0.70f));
        p->setSpecular(Ogre::ColourValue(0.08f, 0.08f, 0.08f));
        p->setShininess(8.f);
    }
    // Ghost instance of the dual view: same grey, additive-ish transparency.
    if (!Ogre::MaterialManager::getSingleton().resourceExists("GhostWhite"))
    {
        auto m = Ogre::MaterialManager::getSingleton().create(
            "GhostWhite", RGM::DEFAULT_RESOURCE_GROUP_NAME);
        auto *p = m->getTechnique(0)->getPass(0);
        p->setLightingEnabled(true);
        p->setAmbient(Ogre::ColourValue(0.10f, 0.16f, 0.22f));
        p->setDiffuse(Ogre::ColourValue(0.55f, 0.75f, 0.95f, 0.35f)); // cyan-ish ghost
        p->setSpecular(Ogre::ColourValue(0.10f, 0.10f, 0.10f, 0.35f));
        p->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
        p->setDepthWriteEnabled(false); // nicer overlay without z-fighting artifacts
    }

    // --- Grid / axes --------------------------------------------------------
    // These are drawn as plain coloured line lists. RTSS generates their shader
    // from the same lit template as the model, so they are handled by the
    // normal scheme mechanism (see ensureUnlitMaterial above).
    ensureUnlitMaterial("GridUnlit", Ogre::ColourValue(0.35f, 0.35f, 0.35f));     // dim cells
    ensureUnlitMaterial("GridLineUnlit", Ogre::ColourValue(0.45f, 0.45f, 0.45f)); // bright lines
}

void OgreWidget::clearInstances()
{
    // Destroy all instance roots but keep the grid/light that you created separately.
    // NOTE: SceneNode::removeAndDestroyAllChildren() only deletes the nodes, it does
    // NOT destroy the MovableObjects attached to them. Those stay registered in the
    // SceneManager, so rebuilding an instance under the same name throws
    // ItemIdentityException ("... already exists"). Detach + destroy explicitly.
    for (auto &kv : mRobots)
    {
        if (kv.second.root)
        {
            destroySubtree(mScene, kv.second.root);
        }
    }
    mRobots.clear();
    mBuiltJoints.clear();
    mLinkNodes.clear();
}

bool OgreWidget::loadURDFDual(const std::string &urdf_path,
                              const std::string &cmdName,
                              const std::string &fbName)
{
    if (!ensureOgre())
        return false;

    UrdfModel model;
    if (!UrdfLoader::loadFile(urdf_path, model))
    {
        qWarning() << "URDF load failed:" << QString::fromStdString(urdf_path);
        return false;
    }
    mModel = std::move(model);
    clearInstances();

    const std::string dir = fs::path(urdf_path).parent_path().string();

    buildInstance(fbName, /*transparent*/ false, dir);
    buildInstance(cmdName, /*transparent*/ true, dir);

    fitGridToModel();
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
        auto *node = R.root->createChildSceneNode(instance + "_" + kv.first);
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
        s = V.scale;
        // vnode->setScale(V.scale[0], V.scale[1], V.scale[2]);
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

    // Every material in this scene needs an RTSS generated pass, because
    // GL3Plus has no fixed-function pipeline at all. The generator is driven
    // purely by the pass diffuse/ambient colours, so no material is skipped.
    MaterialPtr mat = MaterialManager::getSingleton().getByName(matName);
    if (mat.isNull())
        return nullptr;

    // If the material already carries a technique for the requested scheme it
    // was validated up front (see seed() in initOgre) and is ready to use.
    for (unsigned i = 0; i < mat->getNumTechniques(); ++i)
    {
        Technique *t = mat->getTechnique(i);
        if (t && t->getSchemeName() == ShaderGenerator::DEFAULT_SCHEME_NAME)
            return t;
    }

    // Otherwise generate it now. This is the path taken by materials created
    // after the initial seeding pass (the per-axis materials, for instance), and
    // it is what keeps them from silently falling back to their fixed-function
    // technique - which on GL3Plus renders nothing at all.
    const bool ok = mSG->createShaderBasedTechnique(
        *mat,
        MaterialManager::DEFAULT_SCHEME_NAME, // source scheme (fixed-function)
        ShaderGenerator::DEFAULT_SCHEME_NAME, // destination (RTSS) scheme
        /*overProgrammable*/ false);

    if (!ok)
        return nullptr;

    // createShaderBasedTechnique() only registers the technique; the shader
    // programs themselves are built by validateMaterial(). Skipping this leaves
    // the technique without GpuPrograms, so the material draws as flat black.
    mSG->validateMaterial(ShaderGenerator::DEFAULT_SCHEME_NAME, matName);

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
