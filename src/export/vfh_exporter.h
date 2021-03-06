//
// Copyright (c) 2015-2016, Chaos Software Ltd
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


#include "vfh_export_context.h"

#include <OP/OP_Node.h>
#include <OBJ/OBJ_Node.h>
#include <ROP/ROP_Node.h>

#include <unordered_map>


namespace VRayForHoudini {

class VRayRendererNode;

enum VRayLightType {
	VRayLightOmni      = 0,
	VRayLightRectangle = 2,
	VRayLightSphere    = 4,
	VRayLightDome      = 6,
	VRayLightSun       = 8,
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
		, mb_duration(0.)
		, mb_interval_center(0.)
	{}

	void   calcParams(fpreal currFrame);

	int    mb_geom_samples;
	/// (fpreal) motion blur duration in frames
	fpreal mb_duration;
	/// (fpreal) motion blur interval center in frames
	fpreal mb_interval_center;

	fpreal mb_start;
	fpreal mb_end;
	fpreal mb_frame_inc;
};

/// Vfh main exporter. This is the main class responsible for translating
/// Houdini geometry and nodes to V-Ray plugins, intilizing and starting
/// the rendering process
class VRayExporter
{
public:
	/// Available work modes for the exporter
	// NOTE: Keep in sync with "render_export_mode"
	enum ExpWorkMode {
		ExpRender = 0, ///< only render
		ExpExportRender, ///< export a vrscene and render
		ExpExport, ///< export a vrscene
	};

	VRayExporter(VRayRendererNode *rop);
	~VRayExporter();

public:
	/// Create and initilize or reset the V-Ray renderer instance.
	/// This will stop currently running rendering (if any).
	/// @param hasUI[in] - true to enable the VFB, false if VFB is not needed
	///        for example when running Houdini in non-GUI mode
	/// @param reInit[in] - true to re-create the V-Ray renderer instance
	///        otherwise it only resets the existing instance
	/// @retval true on success
	int initRenderer(int hasUI, int reInit);

	/// Prepare for scene export and clear cached data from previous invocations
	/// @param hasUI[in] - if the VFB is enabled
	/// @param nframes[in] - how many frames are we going to render
	/// @param tstart[in] - start export time
	/// @param tend[in] - end export time
	void initExporter(int hasUI, int nframes, fpreal tstart, fpreal tend);

	/// Gather data for camera settings
	/// @param camera[in] - the active camera
	/// @param rop[in] - the rop node invoking the rendering
	/// @param viewParams[out] - collects camera settings
	void fillCameraData(const OBJ_Node &camera, const OP_Node &rop, ViewParams &viewParams);

	/// Gather data for motion blur
	/// @param viewParams[out] - collects motion blur settings
	void fillSettingsMotionBlur(ViewParams &viewParams);

	/// Fill in physical camera settings
	/// @param viewParams[in] - holds data for camera settings
	/// @param pluginDesc[out] - physical camera plugin description
	void fillPhysicalCamera(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);

	/// Fill in depth of field settings
	/// @param viewParams[in] - holds data for camera settings
	/// @param pluginDesc[out] - depth of field plugin description
	void fillSettingsCameraDof(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);

	/// Fill in default camera settings
	/// @param viewParams[in] - holds data for camera settings
	/// @param pluginDesc[out] - default camera plugin description
	void fillCameraDefault(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);

	/// Fill in camera settings
	/// @param viewParams[in] - holds data for camera settings
	/// @param pluginDesc[out] - camera settings plugin description
	void fillSettingsCamera(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);

	/// Fill in render view settings
	/// @param viewParams[in] - holds data for camera settings
	/// @param pluginDesc[out] - render view settings plugin description
	void fillRenderView(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);

	/// Fill in stereoscopic settings
	/// @param viewParams[in] - holds data for camera settings
	/// @param pluginDesc[out] - stereoscopic settings plugin description
	void fillStereoSettings(const ViewParams &viewParams, Attrs::PluginDesc &pluginDesc);

	/// Fill in motion blur settings
	/// @param pluginDesc[out] - motion blur settings plugin description
	void fillMotionBlurParams(MotionBlurParams &mbParams);

	/// Fill in output settings
	/// @param pluginDesc[out] - output settings plugin description
	void fillSettingsOutput(Attrs::PluginDesc &pluginDesc);

	/// Export camera related settings - camera, dof, motion blur, etc.
	/// This is called once for each frame we want to render
	/// @retval 0 on success
	int exportView();

	/// Export the actual scene - geometry, materials, lights, environment,
	/// volumes and render channels. This is called once for each frame we
	/// want to render
	void exportScene();

	/// Export global renderer settings - color mapping, gi, irradiance cache, etc.
	/// This is called once when a render session is initililzed.
	void exportSettings();

	/// Export active lights in the scene
	/// This is called once for each frame we want to render
	void exportLights();

	/// Export render channels
	/// This is called once for each frame we want to render
	/// @param op_node[in] - render channels collector VOP node
	void exportRenderChannels(OP_Node *op_node);

	/// Export environment
	/// This is called once for each frame we want to render
	/// @param op_node[in] - environment VOP node
	void exportEnvironment(OP_Node *op_node);

	/// Export volumetric effects
	/// This is called once for each frame we want to render
	/// @param op_node[in] - environment VOP node
	void exportEffects(OP_Node *op_net);

	/// Export scene at a given time
	/// This is called once for each frame we want to render
	void exportFrame(fpreal time);

	/// Export cleanup callback. Called when the rendering has finished.
	void exportEnd();

	/// Export OBJ_geometry node
	/// @retval V-Ray plugin created for that node
	VRay::Plugin exportObject(OBJ_Node *obj_node);

	/// Export VRayClipper node
	/// @retval V-Ray plugin created for that node
	VRay::Plugin exportVRayClipper(OBJ_Node &clipperNode);

	VRay::Plugin exportParticles(OBJ_Node *dop_network);

	/// Fill in displacement/subdivision render properties for the given node
	/// @param obj_node[in] - the OBJ_Geometry node
	/// @param pluginDesc[out] - diplacement/subdivision plugin description
	void exportDisplacementDesc(OBJ_Node *obj_node, Attrs::PluginDesc &pluginDesc);

	/// Export the given geomtry with displacement/subdivision at render time
	/// @param obj_node[in] - the OBJ_Geometry node owner of displacement/subdivision
	///        render properties
	/// @param geomPlugin[in] - geometry to displace/subdivide
	/// @retval V-Ray displacement/subdivision plugin
	VRay::Plugin exportDisplacement(OBJ_Node *obj_node, VRay::Plugin &geomPlugin);

	/// Export OBJ_Light node
	/// @retval V-Ray plugin created for that node
	VRay::Plugin exportLight(OBJ_Node *obj_node);

	/// Export VOP node
	/// @retval V-Ray plugin created for that node
	VRay::Plugin exportVop(OP_Node *op_node, ExportContext *parentContext = nullptr);

	/// Export Make transform VOP node
	/// @retval V-Ray transform for that node
	VRay::Transform exportTransformVop(VOP_Node &vop_node, ExportContext *parentContext = nullptr);

	/// Export V-Ray material SHOP network
	/// @retval V-Ray surface material plugin for that node
	VRay::Plugin exportMaterial(SHOP_Node &shop_node);

	/// Export the default light created when there are no lights in the scene
	/// @param update[in] - flags whether this is called from IPR callback
	/// @retval V-Ray plugin for default light
	VRay::Plugin exportDefaultHeadlight(bool update = false);

	/// Export defaull V-Ray material. This is used when no valid material is found
	/// for the object
	/// @retval V-Ray plugin for default material
	VRay::Plugin exportDefaultMaterial();


#ifdef CGR_HAS_VRAYSCENE
	VRay::Plugin exportVRayScene(OBJ_Node *obj_node, SOP_Node *geom_node);
#endif

	/// Create or update a plugin from a plugin description
	/// @param pluginDesc - plugin description with relevant properties set
	/// @retval invalid Plugin object if not successul
	VRay::Plugin exportPlugin(const Attrs::PluginDesc &pluginDesc);

	/// Update a plugin properties from a plugin description
	/// @param plugin - the plugin to update
	/// @param pluginDesc - plugin description with relevant properties set
	void exportPluginProperties(VRay::Plugin &plugin, const Attrs::PluginDesc &pluginDesc);

	/// Exports the current scene contents to the specified file. AppSDK
	/// serializes all plugins in text format.
	/// @param filepath - The path to the file where the scene will be exported. The
	/// file path must contain the name and extension of the destination file.
	/// @param settings - Additional options such as compression and file splitting
	/// @retval 0 - no error
	int exportVrscene(const std::string &filepath, VRay::VRayExportSettings &settings);

	/// Delete plugins created for the given OBJ node.
	void removePlugin(OBJ_Node *node);

	/// Delete plugin with the given name
	void removePlugin(const std::string &pluginName);

	/// Delete plugin for the plugin description
	void removePlugin(const Attrs::PluginDesc &pluginDesc);

	/// Start rendering at the current time. This will do different this depending on
	/// the work mode of the exporter- export vrscene, render or both
	/// @param locked[in] - when true this will force the current thread to block
	///        until rendering is done. By default this is a non-blocking call
	/// @retval 0 - no error
	int renderFrame(int locked=false);

	/// Start rendering an animation sequence.
	/// @param start[in] - animation start time
	/// @param end[in] - animation end time
	/// @param step[in] - animation time step
	/// @param locked[in] - when true this will force the current thread to block
	///        until rendering is done. By default this is a non-blocking call
	/// @retval 0 - no error
	int renderSequence(int start, int end, int step, int locked=false);

	void clearKeyFrames(float toTime);

	/// Set if we are exporting animation
	/// @note also used for motion blur
	void setAnimation(bool on);

	/// Set current export time
	void setCurrentTime(fpreal time);

	/// Set if we are exporting for IPR
	void setIPR(int isIPR);

	/// Adjust DR options and hosts based on what is set in the ROP parameters
	void setDRSettings();

	/// Set the render mode: Production/RT CPU/RT GPU
	void setRendererMode(int mode);

	/// Set the work mode: export vrscene/render/both
	void setWorkMode(ExpWorkMode mode);

	/// Set current export context
	void setContext(const OP_Context &ctx);

	/// Abort rendering at first possible time
	void setAbort();

	/// Set image width and height
	void setRenderSize(int w, int h);

	/// Export RT engine settings
	void setSettingsRtEngine();

	/// Get current export context
	OP_Context& getContext() { return m_context; }

	/// Get vfh plugin renderer
	VRayPluginRenderer& getRenderer() { return m_renderer; }

	/// Get the V-Ray ROP bound to this exporter
	VRayRendererNode& getRop() { return *m_rop; }

	/// Get ROP error code. This is called from the V-Ray ROP on every frame
	/// to check if rendering should be aborted
	ROP_RENDER_CODE getError() const { return m_error; }

	/// Test if we are using the GPU engine
	int isGPU() const { return m_isGPU; }

	/// Test if we are rendering in IPR mode
	int isIPR() const { return m_isIPR; }

	/// Test if we need to abort the rendering
	int isAborted() const { return m_isAborted; }

	/// Test if we are going to export data for more that single frame
	/// i.e. animation, motion blur or output velocity channel
	int isAnimation() const { return m_isAnimation; }

	/// Test if we are using stereo camera. This is setup on the V-Ray ROP
	int isStereoView() const;

	/// Test if we are using physical camera
	/// @param camera[in] - camera object to read parameters from
	int isPhysicalView(const OBJ_Node &camera) const;

	/// Test if a node is animated
	int isNodeAnimated(OP_Node *op_node);

	/// Test if a ligth is enabled i.e. its enabled flag is on,
	/// intensity is > 0 or its a forced light on the V-Ray ROP
	int isLightEnabled(OP_Node *op_node);

	/// Test if velocity channels is enabled
	/// @param rop[in] - the V-Ray ROP to test for velocity channel enabled
	int hasVelocityOn(OP_Node &rop) const;

	/// Test if motion blur is enabled
	/// @param rop[in] - the V-Ray ROP
	/// @param camera[in] - active render camera
	int hasMotionBlur(OP_Node &rop, OBJ_Node &camera) const;

	/// Helper functions to retrieve the input node given an input connection name
	/// @param op_node[in] - VOP node
	/// @param inputName[in] -  the input connection name
	/// @retval the VOP input
	static OP_Input* getConnectedInput(OP_Node *op_node, const std::string &inputName);
	static OP_Node* getConnectedNode(OP_Node *op_node, const std::string &inputName);

	/// Helper function to retrieve the connection type given an input connection name
	/// @param op_node[in] - VOP node
	/// @param inputName[in] -  the input connection name
	/// @retval the connection type
	static const Parm::SocketDesc* getConnectedOutputType(OP_Node *op_node, const std::string &inputName);

	/// Helper functions to generate a plugin name for a given node
	/// @param op_node[in] - the node
	/// @param prefix[in] - name prefix
	/// @param suffix[in] - name suffix
	/// @retval plugin name
	static std::string getPluginName(OP_Node *op_node, const std::string &prefix="", const std::string &suffix="");
	static std::string getPluginName(OBJ_Node *obj_node);

	/// Helper function to get the active camera from a given ROP node
	/// @param rop[in] - the ROP node
	/// @retval the active camera
	static OBJ_Node* getCamera(const OP_Node *rop);

	/// Helper function to get material for an OBJ_Geometry node
	/// @param obj[in] - the OBJ node
	/// @param t[in] - evaluation time for the paremeter
	/// @retval the SHOP node
	static SHOP_Node* getObjMaterial(OBJ_Node *obj, fpreal t=0.0);

	/// Helper function to get OBJ node transform as VRay::Transform
	/// @param obj[in] - the OBJ node
	/// @param context[in] - evaluation time for the paremeters
	/// @param flip[in] - whether to flip y and z axis
	static VRay::Transform getObjTransform(OBJ_Node *obj_node, OP_Context &context, bool flip=false);

	/// Helper function to get OBJ node transform as 2d array of floats
	/// @param obj[in] - the OBJ node
	/// @param context[in] - evaluation time for the paremeters
	/// @param tm[out] - output trasform
	static void getObjTransform(OBJ_Node *obj_node, OP_Context &context, float tm[4][4]);

	/// Convert from VUtils transform to Houdini one
	static void TransformToMatrix4(const VUtils::TraceTransform &tm, UT_Matrix4 &m);

	/// Convert from Houdini transform to VRay::Transform
	/// @param m4[in] - Houdini trasform
	/// @param flip[in] - whether to flip y and z axis
	static VRay::Transform Matrix4ToTransform(const UT_Matrix4D &m4, bool flip=false);

	/// Obtain the first child node with given operator type
	/// @param op_node[in] - parent node
	/// @param op_type[in] - operator name
	static OP_Node* FindChildNodeByType(OP_Node *op_node, const std::string &op_type);

	/// Helper function to fill in plugin description attributes from UT_options
	/// @param pluginDesc[out] - the plugin description
	/// @param options[in] - UT_Options map that holds attribute values
	/// @retval true on success
	bool setAttrsFromUTOptions(Attrs::PluginDesc &pluginDesc, const UT_Options &options) const;

	/// Helper function to fill in single plugin description attribute from a node parameter
	/// @param pluginDesc[out] - the plugin description
	/// @param parmDesc[in] - plugin attribute description
	/// @param opNode[in] - the node
	/// @param parmName[in] - parameter name
	void setAttrValueFromOpNodePrm(Attrs::PluginDesc &plugin, const Parm::AttrDesc &parmDesc, OP_Node &opNode, const std::string &parmName) const;

	/// Helper function to fill in plugin description attributes from matching node parameters
	/// @param pluginDesc[out] - the plugin description
	/// @param opNode[in] - the node
	/// @param prefix[in] - common prefix for the name of related parameters
	/// @param remapInterp[in] - whether to remap ramp interpotaion type (used for ramp parameters)
	void setAttrsFromOpNodePrms(Attrs::PluginDesc &plugin, OP_Node *opNode, const std::string &prefix="", bool remapInterp=false);

	/// Helper function to fill in plugin description attributes from VOP node connected inputs
	/// @param pluginDesc[out] - the plugin description
	/// @param vopNode[in] - the VOP node
	/// @param parentContext - not used
	void setAttrsFromOpNodeConnectedInputs(Attrs::PluginDesc &pluginDesc, VOP_Node *vopNode, ExportContext *parentContext=nullptr);

	/// Export connected input for a VOP node
	/// @param pluginDesc[out] - the plugin description
	/// @param vopNode[in] - the VOP node
	/// @param inpidx[in] - input index
	/// @param inputName[in] - input connection name
	/// @param parentContext - not used
	/// @retval VRay::Plugin for the input VOP or invalid plugin on error
	VRay::Plugin exportConnectedVop(VOP_Node *vop_node, int inpidx, ExportContext *parentContext = nullptr);
	VRay::Plugin exportConnectedVop(VOP_Node *vop_node, const UT_String &inputName, ExportContext *parentContext = nullptr);

	/// Export input parameter VOPs for a given VOP node as V-Ray user textures.
	/// Default values for the textures will be queried from the corresponding
	/// SHOP parameter. Exported user textures are added as attibutes to the plugin
	/// description.
	/// @param pluginDesc[out] - VOP plugin description
	/// @param vopNode[in] - the VOP node
	void setAttrsFromSHOPOverrides(Attrs::PluginDesc &pluginDesc, VOP_Node &vopNode);

	/// All volumetic primitives need to be gathered and passed to a single global PhxShaderSimVol
	/// plugin instance so different interescting volumes can be blended correctly.
	/// This is called from the volume primitive exporters when exporting volume primitives
	/// @param sim[in] - the volumetric data plugin
	void phxAddSimumation(VRay::Plugin sim);

private:
	VRayRendererNode              *m_rop; ///< the ROP node bound to this exporter
	UI::VFB                        m_vfb; ///< a lightweigth frame buffer window showing the rendered image, when we don't want V-Ray VFB
	VRayPluginRenderer             m_renderer; ///< the plugin renderer
	OP_Context                     m_context; ///< current export context
	int                            m_renderMode; ///< rend
	int                            m_isAborted; ///< flag whether rendering should be aborted when possible
	ViewParams                     m_viewParams; ///< used to gather view data from the ROP and camera
	int                            m_frames; ///< how many frames are we going to export
	ROP_RENDER_CODE                m_error; ///< ROP error to singnal the ROP rendering should be aborted
	ExpWorkMode                    m_workMode; ///< what should the exporter do- export vrscene, render or both
	CbItems                        m_opRegCallbacks; ///< holds registered node callbacks for live IPR updates
	VRay::ValueList                m_phxSimulations; ///< accumulates volumetric data to pass to PhxShaderSimVol
	int                            m_isIPR; ///< if we are rendering in IPR mode, i.e. we are tracking live node updates
	int                            m_isGPU; ///< if we are using RT GPU rendering engine
	int                            m_isAnimation; ///< if we should export the scene at more than one time
	int                            m_isMotionBlur; ///< if motion blur is turned on
	int                            m_isVelocityOn; ///< if we have velocity channel enabled
	fpreal                         m_timeStart; ///< start time for the export
	fpreal                         m_timeEnd; ///< end time for the export
	FloatSet                       m_exportedFrames; ///< set of time points at which the scene has already been exported

public:
	/// Register event callback for a given node. This callback will be invoked when
	/// change on the node occurs. The event type will be passed as parameter to the callback
	/// @param op_node - the node to track
	/// @param cb - the event callback function
	void addOpCallback(OP_Node *op_node, OP_EventMethod cb);

	/// Unregister a callback for the given node. This callback will no longer be invoked when
	/// change on the node occurs.
	/// @param op_node - the node to track
	/// @param cb - the event callback function
	void delOpCallback(OP_Node *op_node, OP_EventMethod cb);

	/// Unregister all callbacks for the given node.
	/// @param op_node - the node to track
	void delOpCallbacks(OP_Node *op_node);

	/// Unregister all callbacks for all nodes.
	/// @note this will only remove callbacks that have been registered
	///       through the callback interface
	void resetOpCallbacks();

	/// Callback function for the event when V-Ray logs a text message.
	void onDumpMessage(VRay::VRayRenderer &renderer, const char *msg, int level);

	/// Callback function for the event when V-Ray updates its current computation task and the number of workunits done.
	void onProgress(VRay::VRayRenderer &renderer, const char *msg, int elementNumber, int elementsCount);

	/// Callback function for the event when rendering has finished, successfully or not.
	void onAbort(VRay::VRayRenderer &renderer);

	/// Callbacks for tracking changes on different types of nodes
	static void RtCallbackObjManager(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackLight(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackOBJGeometry(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackSOPChanged(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackView(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackVop(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackSurfaceShop(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackDisplacementShop(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackDisplacementVop(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackDisplacementObj(OP_Node *caller, void *callee, OP_EventType type, void *data);
	static void RtCallbackVRayClipper(OP_Node *caller, void *callee, OP_EventType type, void *data);

};

} // namespace VRayForHoudini

#endif // VRAY_FOR_HOUDINI_EXPORTER_H
