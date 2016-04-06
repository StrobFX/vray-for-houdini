//
// Copyright (c) 2015-2016, Chaos Software Ltd
//
// V-Ray For Houdini
//
// ACCESSIBLE SOURCE CODE WITHOUT DISTRIBUTION OF MODIFICATION LICENSE
//
// Full license text: https://github.com/ChaosGroup/vray-for-houdini/blob/master/LICENSE
//

#ifndef VRAY_FOR_HOUDINI_VOP_NODE_GEOMSTATICSMOOTHEDMESH_H
#define VRAY_FOR_HOUDINI_VOP_NODE_GEOMSTATICSMOOTHEDMESH_H

#include "vop_node_base.h"


namespace VRayForHoudini {
namespace VOP {

class GeomStaticSmoothedMesh:
		public VOP::NodeBase
{
public:
	GeomStaticSmoothedMesh(OP_Network *parent, const char *name, OP_Operator *entry):NodeBase(parent, name, entry) {}
	virtual                    ~GeomStaticSmoothedMesh() {}

//	From VOP_Node
	virtual void          getCode(UT_String &codestr, const VOP_CodeGenContext &context) VRAY_OVERRIDE;

	virtual PluginResult        asPluginDesc(Attrs::PluginDesc &pluginDesc, VRayExporter &exporter, ExportContext *parentContext=nullptr) VRAY_OVERRIDE;

protected:
	virtual void                setPluginType() VRAY_OVERRIDE;

};

} // namespace VOP
} // namespace VRayForHoudini

#endif // VRAY_FOR_HOUDINI_VOP_NODE_GEOMSTATICSMOOTHEDMESH_H
