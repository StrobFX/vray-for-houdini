//
// Copyright (c) 2015, Chaos Software Ltd
//
// V-Ray For Houdini
//
// ACCESSIBLE SOURCE CODE WITHOUT DISTRIBUTION OF MODIFICATION LICENSE
//
// Full license text: https://github.com/ChaosGroup/vray-for-houdini/blob/master/LICENSE
//

#ifndef VRAY_FOR_HOUDINI_EXPORTER_H
#define VRAY_FOR_HOUDINI_EXPORTER_H

#include "vfh_defines.h"
#include "vfh_typedefs.h"
#include "vfh_vray.h"
#include "vfh_plugin_exporter.h"
#include "vfh_plugin_info.h"
#include "vfh_export_view.h"
#include "vfh_vfb.h"
#include "vfh_log.h"

#include <OP/OP_Node.h>
#include <OBJ/OBJ_Node.h>
#include <ROP/ROP_Node.h>

namespace VRayForHoudini {

typedef VUtils::HashMap<int> SHOPToID;

enum VRayLightType {
	VRayLightOmni      = 0,
	VRayLightRectangle = 2,
	VRayLightSphere    = 4,
	VRayLightDome      = 6,
#if UT_MAJOR_VERSION_INT < 15
	VRayLightSun       = 7,
#else
	VRayLightSun       = 8,
#endif
};


struct GeomExportParams
{
	GeomExportParams():
		uvWeldThreshold(1e-6f),
		exportMtlIds(true)
	{}

	float uvWeldThreshold;
	int exportMtlIds;
	SHOPToID shopToID;
};

struct OpInterestItem {
	OpInterestItem():
		op_node(nullptr),
		cb(nullptr),
		cb_data(nullptr)
	{}

	OpInterestItem(OP_Node *op_node, OP_EventMethod cb, void *cb_data):
		op_node(op_node),
		cb(cb),
		cb_data(cb_data)
	{}

	OP_Node        *op_node;
	OP_EventMethod  cb;
	void           *cb_data;
};
typedef std::vector<OpInterestItem> CbItems;


struct MotionBlurParams {
	MotionBlurParams()
		: mb_geom_samples(1)
		, mb_duration(0.0f)
		, mb_interval_center(0.0f)
	{}

	void   calcParams(float frameCurrent);

	int    mb_geom_samples;
	float  mb_duration;
	float  mb_interval_center;

	float  mb_start;
	float  mb_end;
	float  mb_frame_inc;
};


struct MtlContext {
	MtlContext()
		: m_object(nullptr)
	{}
	MtlContext(OBJ_Node *object)
		: m_object(object)
	{}

	void      setObject(OBJ_Node *object) { m_object = object; }
	OBJ_Node *getObject() { return m_object; }

	int       hasMaterialOverrides();
	int       hasMaterialPromotes();

private:
	// NOTE: Not const because of getRenderSopPtr()
	OBJ_Node *m_object;
};


class VRayExporter
{
public:
	// NOTE: Keep in sync with "render_export_mode"
	enum ExpWorkMode {
		ExpRender = 0,
		ExpExportRender,
		ExpExport,
	};

	typedef std::set<VRayExporter*> ExporterInstances;
	static ExporterInstances        Instances;

	VRayExporter(OP_Node *rop);
	~VRayExporter();

public:
	int                            initRenderer(int hasUI, int reInit);
	void                           initExporter(int hasUI, int nframes, fpreal tstart, fpreal tend);

	void                           fillCameraData(const OBJ_Node &camera, const OP_Node &rop, ViewParams &viewParams);
	void                           fillSettingsMotionBlur(ViewParams &viewParams);
	void                           fillPhysicalCamera(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);
	void                           fillSettingsCameraDof(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);
	void                           fillCameraDefault(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);
	void                           fillSettingsCamera(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);
	void                           fillRenderView(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);
	void                           fillStereoSettings(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);
	void                           fillMotionBlurParams(MotionBlurParams &mbParams);

	int                            exportView();
	void                           exportScene();
	void                           exportSettings();
	void                           exportRenderChannels(OP_Node *op_node);
	void                           exportEnvironment(OP_Node *op_node);
	void                           exportEffects(OP_Node *op_net);
	void                           exportFrame(fpreal time);
	void                           exportEnd();

	void                           exportGeomStaticMeshDesc(const GU_Detail &gdp, GeomExportParams &expParams, Attrs::PluginDesc &geomPluginDesc);
	VRay::Plugin                   exportGeomStaticMesh(SOP_Node &sop_node, const GU_Detail &gdp, GeomExportParams &expParams);
	void                           exportGeomMayaHairGeom(SOP_Node *sop_node, const GU_Detail *gdp, Attrs::PluginDesc &pluginDesc);
	VRay::Plugin                   exportGeomMayaHair(SOP_Node *sop_node, const GU_Detail *gdp);

	void                           exportDisplacementDesc(OBJ_Node *obj_node, Attrs::PluginDesc &pluginDesc);
	VRay::Plugin                   exportDisplacement(OBJ_Node *obj_node, VRay::Plugin &geomPlugin);
	VRay::Plugin                   exportNodeData(SOP_Node *sop_node, GeomExportParams &expParams);
	VRay::Plugin                   exportNode(OBJ_Node *obj_node, VRay::Plugin material, VRay::Plugin geometry);
	VRay::Plugin                   exportObject(OBJ_Node *obj_node);
	VRay::Plugin                   exportParticles(OBJ_Node *dop_network);
	VRay::Plugin                   exportLight(OBJ_Node *obj_node);
	VRay::Plugin                   exportVop(OP_Node *op_node);
	VRay::Plugin                   exportMaterial(SHOP_Node *shop_node, MtlContext mtlCtx=MtlContext());
	VRay::Plugin                   exportDefaultMaterial();

#ifdef CGR_HAS_VRAYSCENE
	VRay::Plugin                   exportVRayScene(OBJ_Node *obj_node, SOP_Node *geom_node);
#endif

	VRay::Plugin                   exportPlugin(const Attrs::PluginDesc &pluginDesc);
	void                           exportPluginProperties(VRay::Plugin &plugin, const Attrs::PluginDesc &pluginDesc);
	int                            exportVrscene(const std::string &filepath);

	void                           removePlugin(OBJ_Node *node);
	void                           removePlugin(const std::string &pluginName);
	void                           removePlugin(const Attrs::PluginDesc &pluginDesc);

	int                            renderFrame(int locked=false);
	int                            renderSequence(int start, int end, int step, int locked=false);

	void                           clearKeyFrames(float toTime);

	void                           setAnimation(bool on);
	void                           setFrame(float frame);
	void                           setIPR(int isIPR);
	void                           setRendererMode(int mode);
	void                           setWorkMode(ExpWorkMode mode);
	void                           setContext(const OP_Context &ctx);
	void                           setAbort();
	void                           setExportFilepath(const std::string &path);
	void                           setRenderSize(int w, int h);
	void                           setSettingsRtEngine();

	OP_Context                    &getContext()  { return m_context;   }
	VRayPluginRenderer            &getRenderer() { return m_renderer; }
	OP_Node                       &getRop() { return *m_rop; }
	ROP_RENDER_CODE                getError() const { return m_error; }

	int                            isGPU() const { return m_isGPU; }
	int                            isIPR() const { return m_isIPR; }
	int                            isAborted() const { return m_isAborted; }
	int                            isAnimation() const { return m_isAnimation; }
	int                            isStereoView() const;
	int                            isPhysicalView(const OBJ_Node &camera) const;
	int                            isNodeAnimated(OP_Node *op_node);
	int                            hasMotionBlur(OP_Node &rop, OBJ_Node &camera);

	static OP_Input               *getConnectedInput(OP_Node *op_node, const std::string &inputName);
	static OP_Node                *getConnectedNode(OP_Node *op_node, const std::string &inputName);
	static const Parm::SocketDesc *getConnectedOutputType(OP_Node *op_node, const std::string &inputName);

	static std::string             getPluginName(OP_Node *op_node, const std::string &prefix="", const std::string &suffix="");
	static std::string             getPluginName(OBJ_Node *obj_node);

	static OBJ_Node               *getCamera(const OP_Node *rop);
	SHOP_Node                     *getObjMaterial(OBJ_Node *obj, fpreal t=0.0);
	static VRay::Transform         getObjTransform(OBJ_Node *obj_node, OP_Context &context, bool flip=false);
	static void                    getObjTransform(OBJ_Node *obj_node, OP_Context &context, float tm[4][4]);

	static void                    TransformToMatrix4(const VUtils::TraceTransform &tm, UT_Matrix4 &m);
	static VRay::Transform         Matrix4ToTransform(const UT_Matrix4D &m4, bool flip=false);
	static OP_Node                *FindChildNodeByType(OP_Node *op_node, const std::string &op_type);

	void                           setAttrValueFromOpNode(Attrs::PluginDesc &plugin, const Parm::AttrDesc &parmDesc, OP_Node *opNode, const std::string &prefix="");
	void                           setAttrsFromOpNode(Attrs::PluginDesc &plugin, OP_Node *opNode, const std::string &prefix="");

	VRay::Plugin                   exportConnectedVop(OP_Node *op_node, const UT_String &inputName);
	void                           phxAddSimumation(VRay::Plugin sim);

private:
	OP_Node                       *m_rop;
	UI::VFB                        m_vfb;
	VRayPluginRenderer             m_renderer;
	OP_Context                     m_context;
	int                            m_renderMode;
	int                            m_isAborted;
	ViewParams                     m_viewParams;
	int                            m_frames;
	ROP_RENDER_CODE                m_error;
	std::string                    m_exportFilepath;
	ExpWorkMode                    m_workMode;
	CbItems                        m_opRegCallbacks;
	VRay::ValueList                m_phxSimulations;
	int                            m_isIPR;
	int                            m_isGPU;
	int                            m_isAnimation;
	int                            m_isMotionBlur;
	fpreal                         m_timeStart;
	fpreal                         m_timeEnd;
	FloatSet                       m_exportedFrames;

public:
	static bool                    TraverseOBJs(OP_Node &op_node, void *data);
	static void                    TraverseOBJ(OBJ_Node *obj_node, void *data);

	void                           addOpCallback(OP_Node *op_node, OP_EventMethod cb);
	void                           delOpCallback(OP_Node *op_node, OP_EventMethod cb);
	void                           delOpCallbacks(OP_Node *op_node);
	void                           resetOpCallbacks();

	void                           onDumpMessage(VRay::VRayRenderer &renderer, const char *msg, int level);
	void                           onProgress(VRay::VRayRenderer &renderer, const char *msg, int elementNumber, int elementsCount);
	void                           onAbort(VRay::VRayRenderer &renderer);

	static void                    RtCallbackObjManager(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackLight(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackNode(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackNodeData(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackView(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackVop(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackSurfaceShop(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackDisplacementShop(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackDisplacementVop(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void                    RtCallbackDisplacementObj(OP_Node *caller, void *callee, OP_EventType type, void *data);

};

} // namespace VRayForHoudini

#endif // VRAY_FOR_HOUDINI_EXPORTER_H
