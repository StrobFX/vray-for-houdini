//
// Copyright (c) 2015-2016, Chaos Software Ltd
//
// V-Ray For Houdini
//
// ACCESSIBLE SOURCE CODE WITHOUT DISTRIBUTION OF MODIFICATION LICENSE
//
// Full license text: https://github.com/ChaosGroup/vray-for-houdini/blob/master/LICENSE
//

#include "vfh_export_vrayproxy.h"
#include "vfh_exporter.h"
#include "vfh_export_mesh.h"
#include "vfh_export_hair.h"

#include <ROP/ROP_Error.h>
#include <OBJ/OBJ_Node.h>
#include <FS/FS_Info.h>
#include <FS/FS_FileSystem.h>
#include <UT/UT_Assert.h>

#include <uni.h>
#include <voxelsubdivider.h>


using namespace VRayForHoudini;

/// Dummy VRayExporter exporter instance necessary
/// to create HairPrimitiveExporter or MeshExporter
/// TODO: we should remove this dependency in future
static VRayExporter& getDummyExporter()
{
	static VRayExporter exporter(nullptr);
	return exporter;
}


bool VRayProxyExportOptions::isAppendMode() const
{
	return (   m_exportAsAnimation
			&& m_animFrames > 1
			&& m_context.getTime() > m_animStart);
}


UT_String VRayProxyExportOptions::getFilepath(const SOP_Node &sop) const
{
	UT_ASSERT( m_filepath.isstring() );

	UT_String filepath = m_filepath;

	if (NOT(filepath.matchFileExtension(".vrmesh"))) {
		filepath += ".vrmesh";
	}

	if (NOT(m_exportAsSingle)) {
		UT_String soppathSuffix;
		sop.getFullPath(soppathSuffix);
		soppathSuffix.forceAlphaNumeric();
		soppathSuffix += ".vrmesh";
		filepath.replaceSuffix(".vrmesh", soppathSuffix);
	}

	return filepath;
}


int VRayProxyExporter::GeometryDescription::hasValidData() const
{
	return (   m_description.contains( getVertsAttrName() )
			&& m_description.contains( getPrimAttrName() ));
}


Attrs::PluginAttr& VRayProxyExporter::GeometryDescription::getAttr(const char *attrName)
{
	Attrs::PluginAttr *attr = m_description.get(attrName);
	UT_ASSERT( attr );
	return *attr;
}


void VRayProxyExporter::GeometryDescription::clearData()
{
	m_isHair = false;
	m_description = Attrs::PluginDesc();
	// make identity
	m_transform.matrix.makeIdentity();
	m_transform.offset.makeZero();
	m_bbox.init();
}


VUtils::ErrorCode VRayProxyExporter::doExport(VRayProxyExportOptions &options, const UT_ValArray<SOP_Node *> &sopList)
{
	VUtils::ErrorCode err;

	if (NOT(options.m_filepath.isstring())) {
		err.setError(__FUNCTION__,
					 ROP_MISSING_FILE,
					 "Invalid file specified.");
		return err;
	}

	if (sopList.size() <= 0) {
		err.setError(__FUNCTION__,
					 ROP_BAD_CONTEXT,
					 "No geometry valid found.");
		return err;
	}

	UT_String dirpath;
	UT_String filename;
	options.m_filepath.splitPath(dirpath, filename);

	// create parent dirs if necessary
	FS_Info fsInfo(dirpath);
	if (NOT(fsInfo.exists())) {
		FS_FileSystem fsys;
		if (   NOT(options.m_mkpath)
			|| NOT(fsys.createDir(dirpath)))
		{
			err.setError(__FUNCTION__,
						 ROP_CREATE_DIRECTORY_FAIL,
						 "Failed to create parent directory.");
			return err;
		}
	}

	int nodeCnt = ((options.m_exportAsSingle)? sopList.size() : 1);
	for (int f = 0; f < options.m_animFrames; ++f) {
		if (err.error()) {
			break;
		}

		options.m_context.setTime(options.m_animStart);
		options.m_context.setFrame(options.m_context.getFrame() + f);
		for (int i = 0; i < sopList.size(); i += nodeCnt) {
			VRayProxyExporter exporter(options, sopList.getRawArray() + i, nodeCnt);
			VUtils::ErrorCode ferr = exporter.init();
			// TODO: collect errors, not just last one
			if (ferr.error()) {
				err = ferr;
				continue;
			}

			// Do actual export
			ferr = exporter.doExportFrame();
			if (ferr.error()) {
				err = ferr;
			}
		}
	}

	return err;
}


VRayProxyExporter::VRayProxyExporter(const VRayProxyExportOptions &options, SOP_Node * const *nodes, int nodeCnt):
	m_options(options),
	m_previewVerts(nullptr),
	m_previewFaces(nullptr),
	m_previewStrandVerts(nullptr),
	m_previewStrands(nullptr)
{
	UT_ASSERT( nodeCnt > 0 );
	UT_ASSERT( nodes != nullptr );

	m_geomDescrList.reserve(nodeCnt);
	for (int i = 0; i < nodeCnt; ++i) {
		SOP_Node *node = *(nodes + i);
		if (node) {
			m_geomDescrList.emplace_back(*node);
		}
	}

	UT_ASSERT(m_geomDescrList.size() > 0);

	m_voxels.resize(m_geomDescrList.size() + 1);
}


VRayProxyExporter::~VRayProxyExporter()
{
	cleanup();
}


VUtils::ErrorCode VRayProxyExporter::init()
{
	VUtils::ErrorCode res;

	cleanup();

	for (auto &geomDescr : m_geomDescrList){
		res = cacheDescriptionForContext(m_options.m_context, geomDescr);
	}

	return res;
}


void VRayProxyExporter::cleanup()
{
	for (VUtils::MeshVoxel &voxel : m_voxels) {
		voxel.freeMem();
	}

	for (auto &geomDecr : m_geomDescrList) {
		geomDecr.clearData();
	}

	if (m_previewVerts) {
		FreePtrArr(m_previewVerts);
	}

	if (m_previewFaces) {
		FreePtrArr(m_previewFaces);
	}

	if (m_previewStrandVerts) {
		FreePtrArr(m_previewStrandVerts);
	}

	if (m_previewStrands) {
		FreePtrArr(m_previewStrands);
	}
}


VUtils::ErrorCode VRayProxyExporter::doExportFrame()
{
	VUtils::ErrorCode err;

	UT_String filepath = m_options.getFilepath(m_geomDescrList[0].m_node);
	bool isAppendMode = m_options.isAppendMode();

	FS_Info fsInfo(filepath);
	if (   fsInfo.fileExists()
		&& NOT(isAppendMode)
		&& NOT(m_options.m_overwrite)
		)
	{
		err.setError(__FUNCTION__,
					 ROP_FILE_EXISTS,
					 "File already exists: %s", filepath.buffer());
		return err;
	}

	VUtils::SubdivisionParams subdivParams;
	subdivParams.facesPerVoxel = (m_options.m_maxFacesPerVoxel > 0)? m_options.m_maxFacesPerVoxel : INT_MAX;

	return VUtils::subdivideMeshToFile(this,
									   filepath,
									   subdivParams,
									   NULL,
									   isAppendMode,
									   m_options.m_exportPCLs,
									   m_options.m_pointSize);
}


uint32 VRayProxyExporter::getVoxelFlags(int i)
{
	UT_ASSERT( i >= 0 );
	UT_ASSERT( i < getNumVoxels() );

	if ( i == (getNumVoxels()-1) ) {
		return MVF_PREVIEW_VOXEL;
	}

	const GeometryDescription &geomDescr = m_geomDescrList[i];
	return (geomDescr.m_isHair)? MVF_HAIR_GEOMETRY_VOXEL : MVF_GEOMETRY_VOXEL;
}


VUtils::Box VRayProxyExporter::getVoxelBBox(int i)
{
	UT_ASSERT( i >= 0 );
	UT_ASSERT( i < getNumVoxels() );

	if ( i < (getNumVoxels()-1) ) {
		return m_geomDescrList[i].m_bbox;
	}

	VUtils::Box previewBBox(0);
	for (const auto &geomDecr : m_geomDescrList) {
		previewBBox += geomDecr.m_bbox;
	}

	return previewBBox;
}


VUtils::MeshVoxel* VRayProxyExporter::getVoxel(int i, uint64 *memUsage)
{
	UT_ASSERT( i >= 0 );
	UT_ASSERT( i < getNumVoxels() );

	VUtils::MeshVoxel &voxel = m_voxels[i];
	voxel.init();
	voxel.index = i;

	if ( i == (getNumVoxels()-1) ) {
		buildPreviewVoxel(voxel);
	} else if (m_geomDescrList[i].m_isHair) {
		buildHairVoxel(voxel, m_geomDescrList[i]);
	} else {
		buildMeshVoxel(voxel, m_geomDescrList[i]);
	}

	if (memUsage) {
		*memUsage = voxel.getMemUsage();
	}

	return &voxel;
}


void VRayProxyExporter::releaseVoxel(VUtils::MeshVoxel *voxel, uint64 *memUsage)
{
	if (voxel) {
		if (memUsage) {
			*memUsage = voxel->getMemUsage();
		}

		voxel->freeMem();
		Log::getLog().info("Voxel released");
	}
}


VUtils::ErrorCode VRayProxyExporter::cacheDescriptionForContext(const OP_Context &context, GeometryDescription &geomDescr)
{
	const UT_String& name = geomDescr.m_node.getName();

	VUtils::ErrorCode res;

	OP_Context cntx = context;
	// advance current time with velocityStart and get mesh description for that time
	if (m_options.m_exportVelocity) {
		cntx.setFrame(context.getFloatFrame() + m_options.m_velocityStart);
	}

	OP_Network *parentNet = geomDescr.m_node.getParent();
	if (NOT(parentNet)) {
		res.setError(__FUNCTION__,
					 ROP_BAD_CONTEXT,
					 "No renderable geometry found for context #%0.3f!", context.getFloatFrame());
		return res;
	}

	OBJ_Node *parentOBJ = parentNet->castToOBJNode();
	if (   NOT(parentOBJ)
		|| NOT(parentOBJ->isObjectRenderable(cntx.getFloatFrame())) ) {

		res.setError(__FUNCTION__,
					 ROP_BAD_CONTEXT,
					 "No renderable geometry found for context #%0.3f!", context.getFloatFrame());
		return res;
	}

	res = getDescriptionForContext(cntx, geomDescr);
	if (res.error()) {
		return res;
	}

	// transform normals
	if (m_options.m_applyTransform) {
		if (geomDescr.m_description.contains("normals")) {
			VRay::VUtils::VectorRefList &normals = geomDescr.getAttr("normals").paramValue.valRawListVector;
			// calc nm(normal matrix) = transpose(inverse(matrix))
			VRay::Matrix nm = geomDescr.m_transform.matrix;
			nm.makeInverse();
			nm.makeTranspose();
			for (int i = 0; i < normals.size(); ++i) {
				normals[i] = nm * normals[i];
			}
		}
	}

	VRay::VUtils::VectorRefList &verts = geomDescr.getVertAttr().paramValue.valRawListVector;

	// calc bbox
	geomDescr.m_bbox.init();
	for (int i = 0; i < verts.size(); ++i) {
		geomDescr.m_bbox += VUtils::Vector(verts[i].x, verts[i].y, verts[i].z);
	}

	if (m_options.m_exportVelocity) {
		// add velocities to description
		VRay::VUtils::VectorRefList velocities(verts.size());
		for (int i = 0; i < velocities.size(); ++i) {
			velocities[i].set(0.f,0.f,0.f);
		}

		const float velocityInterval = m_options.m_velocityEnd - m_options.m_velocityStart;
		if (velocityInterval > 1e-6f) {
			cntx.setFrame(context.getFloatFrame() + m_options.m_velocityEnd);

			GeometryDescription nextDescr(geomDescr.m_node);
			VUtils::ErrorCode err_code = getDescriptionForContext(cntx, nextDescr);

			if ( !err_code.error() ) {
				VRay::VUtils::VectorRefList &nextVerts = nextDescr.getVertAttr().paramValue.valRawListVector;
				// if no change in topology calc velocities
				if (verts.size() == nextVerts.size() ) {
					const float dt = 1.f/velocityInterval;
					for (int i = 0; i < velocities.size(); ++i) {
						velocities[i] = (nextVerts[i] - verts[i]) * dt;
					}
				}
			}
		}

		geomDescr.m_description.addAttribute(Attrs::PluginAttr("velocities", velocities));
	}

	return res;
}


void VRayProxyExporter::getTransformForContext(OP_Context &context, GeometryDescription &geomDescr) const
{
	OP_Network *parentNet = geomDescr.m_node.getParent();
	if (NOT(parentNet)) {
		return;
	}

	OBJ_Node *parentOBJ = parentNet->castToOBJNode();
	if (NOT(parentOBJ)) {
		return;
	}

	UT_Matrix4 mat;
	UT_Vector3 offs;
	parentOBJ->getLocalToWorldTransform(context, mat);
	mat.getTranslates(offs);

	geomDescr.m_transform.matrix.setCol(0, VRay::Vector(mat(0,0), mat(0,1), mat(0,2)));
	geomDescr.m_transform.matrix.setCol(1, VRay::Vector(mat(1,0), mat(1,1), mat(1,2)));
	geomDescr.m_transform.matrix.setCol(2, VRay::Vector(mat(2,0), mat(2,1), mat(2,2)));
	geomDescr.m_transform.offset.set(offs(0), offs(1), offs(2));
}


VUtils::ErrorCode VRayProxyExporter::getDescriptionForContext(OP_Context &context, GeometryDescription &geomDescr)
{
	VUtils::ErrorCode res;

	// query geometry for context
	GU_DetailHandleAutoReadLock gdl(geomDescr.m_node.getCookedGeoHandle(context));
	const GU_Detail *gdp = gdl.getGdp();

	// NOTE: Could happen, for example, with file node when file is missing
	if (NOT(gdp)) {
		res.setError(__FUNCTION__,
					 ROP_COOK_ERROR,
					 "Incorrect geometry detail for context #%0.3f!", context.getFloatFrame());
		return res;
	}

	VRayExporter &dummyExp = getDummyExporter();
	HairPrimitiveExporter hairExp(*geomDescr.m_node.getParent()->castToOBJNode(),
								  context,
								  dummyExp);

	if (hairExp.asPluginDesc(*gdp, geomDescr.m_description)) {
		geomDescr.m_isHair = true;
	}
	else {
		MeshExporter meshExporter(*geomDescr.m_node.getParent()->castToOBJNode(),
								  context,
								  dummyExp);
		meshExporter.asPluginDesc(*gdp, geomDescr.m_description);
		geomDescr.m_isHair = false;
	}

	if (NOT(geomDescr.hasValidData())) {
		res.setError(__FUNCTION__,
					 ROP_COOK_ERROR,
					 "No geometry found for context #%0.3f!", context.getFloatFrame());
		return res;
	}

	if (m_options.m_applyTransform) {
		VRay::VUtils::VectorRefList &verts = geomDescr.getVertAttr().paramValue.valRawListVector;
		getTransformForContext(context, geomDescr);

		for (int i = 0; i < verts.size(); ++i) {
			verts[i] = geomDescr.m_transform * verts[i];
		}
	} else {
		geomDescr.m_transform.makeIdentity();
	}

	return res;
}


void VRayProxyExporter::buildMeshVoxel(VUtils::MeshVoxel &voxel, GeometryDescription &meshDescr)
{
	const int hasVerts = meshDescr.m_description.contains("vertices");
	const int hasFaces = meshDescr.m_description.contains("faces");
	const int hasMtlIDs = meshDescr.m_description.contains("face_mtlIDs");
	const int hasNormals = meshDescr.m_description.contains("normals");
	const int hasVelocities = meshDescr.m_description.contains("velocities");
	int nUVChannels = meshDescr.m_description.contains("map_channels");

	if (nUVChannels) {
		VRay::VUtils::ValueRefList &map_channels = meshDescr.getAttr("map_channels").paramValue.valRawListValue;
		nUVChannels = map_channels.size();
	}

	voxel.numChannels = hasVerts + hasVelocities + hasFaces + hasMtlIDs + hasNormals * 2 + nUVChannels * 2;
	voxel.channels = new VUtils::MeshChannel[voxel.numChannels];

	int ch_idx = 0;

	// init vertex channel
	if (hasVerts) {
		Log::getLog().info("buildGeomVoxel populate VERT_GEOM_CHANNEL");

		VRay::VUtils::VectorRefList &vertices = meshDescr.getAttr("vertices").paramValue.valRawListVector;

		VUtils::MeshChannel &verts_ch = voxel.channels[ ch_idx++ ];
		verts_ch.init( sizeof(VUtils::VertGeomData), vertices.size(), VERT_GEOM_CHANNEL, FACE_TOPO_CHANNEL, MF_VERT_CHANNEL, false);
		verts_ch.data = vertices.get();
	}
	// init velocity channel
	if (hasVelocities) {
		Log::getLog().info("buildGeomVoxel populate VERT_VELOCITY_CHANNEL");

		VRay::VUtils::VectorRefList &velocities = meshDescr.getAttr("velocities").paramValue.valRawListVector;

		VUtils::MeshChannel &velocity_ch = voxel.channels[ ch_idx++ ];
		velocity_ch.init( sizeof(VUtils::VertGeomData), velocities.size(), VERT_VELOCITY_CHANNEL, FACE_TOPO_CHANNEL, MF_VERT_CHANNEL, false);
		velocity_ch.data = velocities.get();
	}
	// init face channel
	if (hasFaces) {
		Log::getLog().info("buildGeomVoxel populate FACE_TOPO_CHANNEL");

		VRay::VUtils::IntRefList &faces = meshDescr.getAttr("faces").paramValue.valRawListInt;

		VUtils::MeshChannel &face_ch = voxel.channels[ ch_idx++ ];
		face_ch.init(sizeof(VUtils::FaceTopoData), faces.size() / 3, FACE_TOPO_CHANNEL, 0, MF_TOPO_CHANNEL, false);
		face_ch.data = faces.get();
	}
	// init face info channel
	if (hasMtlIDs) {
		Log::getLog().info("buildGeomVoxel populate FACE_INFO_CHANNEL");

		VRay::VUtils::IntRefList &faceMtlIDs = meshDescr.getAttr("face_mtlIDs").paramValue.valRawListInt;

		VUtils::MeshChannel &faceinfo_ch = voxel.channels[ ch_idx++ ];
		faceinfo_ch.init(sizeof(VUtils::FaceInfoData), faceMtlIDs.size(), FACE_INFO_CHANNEL, 0, MF_FACE_CHANNEL, true);
		for(int i = 0; i < faceMtlIDs.size(); ++i) {
			((VUtils::FaceInfoData*)faceinfo_ch.data)[i].mtlID = faceMtlIDs[i];
		}
	}
	// init normals channels
	if (hasNormals) {
		Log::getLog().info("buildGeomVoxel populate VERT_NORMAL_CHANNEL");

		VRay::VUtils::VectorRefList &normals = meshDescr.getAttr("normals").paramValue.valRawListVector;

		VUtils::MeshChannel &normal_ch = voxel.channels[ ch_idx++ ];
		normal_ch.init(sizeof(VUtils::VertGeomData), normals.size(), VERT_NORMAL_CHANNEL, VERT_NORMAL_TOPO_CHANNEL, MF_VERT_CHANNEL, false);
		normal_ch.data = normals.get();

		VRay::VUtils::IntRefList &facenormals = meshDescr.getAttr("faceNormals").paramValue.valRawListInt;

		VUtils::MeshChannel &facenormal_ch = voxel.channels[ ch_idx++ ];
		facenormal_ch.init(sizeof(VUtils::FaceTopoData), facenormals.size() / 3, VERT_NORMAL_TOPO_CHANNEL, 0, MF_TOPO_CHANNEL, false);
		facenormal_ch.data = facenormals.get();
	}

	// init uv channels
	if (nUVChannels) {
		VRay::VUtils::ValueRefList &mapChannels = meshDescr.getAttr("map_channels").paramValue.valRawListValue;

		for (int i = 0; i < nUVChannels; ++i) {
			VRay::VUtils::ValueRefList mapChannel = mapChannels[i].getList();

			int uvchannel_idx = static_cast< int >(mapChannel[0].getDouble());
			VRay::VUtils::VectorRefList uv_verts = mapChannel[1].getListVector();
			VRay::VUtils::IntRefList uv_faces = mapChannel[2].getListInt();

			Log::getLog().info("buildGeomVoxel populate VERT_TEX_CHANNEL %d", uvchannel_idx);

			VUtils::MeshChannel &uvverts_ch = voxel.channels[ ch_idx++ ];
			uvverts_ch.init(sizeof(VUtils::VertGeomData), uv_verts.size(),  VERT_TEX_CHANNEL0 + uvchannel_idx, VERT_TEX_TOPO_CHANNEL0 + uvchannel_idx, MF_VERT_CHANNEL, false);
			uvverts_ch.data = uv_verts.get();

			VUtils::MeshChannel &uvface_ch = voxel.channels[ ch_idx++ ];
			uvface_ch.init(sizeof(VUtils::FaceTopoData), uv_faces.size() / 3, VERT_TEX_TOPO_CHANNEL0 + uvchannel_idx, 0, MF_TOPO_CHANNEL, false);
			uvface_ch.data = uv_faces.get();
		}
	}

	UT_ASSERT( voxel.numChannels == ch_idx );
}


void VRayProxyExporter::buildHairVoxel(VUtils::MeshVoxel &voxel, GeometryDescription &hairDescr)
{
	const int hasVerts = hairDescr.m_description.contains("hair_vertices");
	const int hasStrands = hairDescr.m_description.contains("num_hair_vertices");
	const int hasWidths = hairDescr.m_description.contains("widths");
	const int hasVelocities = hairDescr.m_description.contains("velocities");

	voxel.numChannels = hasVerts + hasStrands + hasVelocities + hasWidths;
	voxel.channels = new VUtils::MeshChannel[voxel.numChannels];
	int ch_idx = 0;

	if (hasVerts) {
		Log::getLog().info("buildHairVoxel populate HAIR_VERT_CHANNEL");
		VRay::VUtils::VectorRefList &vertices = hairDescr.getAttr("hair_vertices").paramValue.valRawListVector;

		VUtils::MeshChannel &vertices_ch = voxel.channels[ ch_idx++ ];
		vertices_ch.init( sizeof(VUtils::VertGeomData), vertices.size(), HAIR_VERT_CHANNEL, HAIR_NUM_VERT_CHANNEL, MF_VERT_CHANNEL, false);
		vertices_ch.data = vertices.get();
	}

	if (hasStrands) {
		Log::getLog().info("buildHairVoxel populate HAIR_NUM_VERT_CHANNEL");
		VRay::VUtils::IntRefList &strands = hairDescr.getAttr("num_hair_vertices").paramValue.valRawListInt;

		VUtils::MeshChannel &strands_ch = voxel.channels[ ch_idx++ ];
		strands_ch.init( sizeof(int), strands.size(), HAIR_NUM_VERT_CHANNEL, 0, MF_NUM_VERT_CHANNEL, false);
		strands_ch.data = strands.get();
	}

	if (hasWidths) {
		Log::getLog().info("buildHairVoxel populate HAIR_WIDTH_CHANNEL");
		VRay::VUtils::FloatRefList &widths = hairDescr.getAttr("widths").paramValue.valRawListFloat;

		VUtils::MeshChannel &width_ch = voxel.channels[ ch_idx++ ];
		width_ch.init( sizeof(float), widths.size(), HAIR_WIDTH_CHANNEL, HAIR_NUM_VERT_CHANNEL, MF_VERT_CHANNEL, false);
		width_ch.data = widths.get();
	}

	if (hasVelocities) {
		Log::getLog().info("buildHairVoxel populate HAIR_VELOCITY_CHANNEL");
		VRay::VUtils::VectorRefList &velocities = hairDescr.getAttr("velocities").paramValue.valRawListVector;

		VUtils::MeshChannel &velocities_ch = voxel.channels[ ch_idx++ ];
		velocities_ch.init( sizeof(VUtils::VertGeomData), velocities.size(), HAIR_VELOCITY_CHANNEL, HAIR_NUM_VERT_CHANNEL, MF_VERT_CHANNEL, false);
		velocities_ch.data = velocities.get();
	}

	UT_ASSERT( voxel.numChannels == ch_idx );
}


void VRayProxyExporter::buildPreviewVoxel(VUtils::MeshVoxel &voxel)
{
	VUtils::ObjectInfoChannelData meshObjInfo;
	createMeshPreviewGeometry(meshObjInfo);

	VUtils::ObjectInfoChannelData hairObjInfo;
	createHairPreviewGeometry(hairObjInfo);

	// init uv sets data
	VUtils::DefaultMeshSetsData setsInfo;
	int nTotalUVChannels = 0;
	for (auto &meshDescr : m_geomDescrList) {
		if ( meshDescr.m_description.contains("map_channels_names") ) {
			const VRay::VUtils::ValueRefList &mapChannelNames = meshDescr.getAttr("map_channels_names").paramValue.valRawListValue;
			nTotalUVChannels += mapChannelNames.size();
		}
	}

	setsInfo.setNumSets(VUtils::MeshSetsData::meshSetType_uvSet, nTotalUVChannels);

	int offset = 0;
	for (auto &meshDescr : m_geomDescrList) {
		if (meshDescr.m_description.contains("map_channels_names")) {
			VRay::VUtils::ValueRefList &mapChannelNames = meshDescr.getAttr("map_channels_names").paramValue.valRawListValue;

			for (int i = 0; i < mapChannelNames.size(); ++i) {
				VRay::VUtils::CharString channelName = mapChannelNames[i].getString();
				setsInfo.setSetName(VUtils::MeshSetsData::meshSetType_uvSet, offset++, channelName.ptr());
			}
		}
	}

	const int numMeshObj = meshObjInfo.getNumObjectInfos();
	const int numHairObj = hairObjInfo.getNumObjectInfos();
	const int numUVSets = setsInfo.getNumSets(VUtils::MeshSetsData::meshSetType_uvSet);

	voxel.numChannels = ((bool)numMeshObj)*3 + ((bool)numHairObj)*3 + (bool)numUVSets;
	voxel.channels = new VUtils::MeshChannel[voxel.numChannels];

	int ch_idx = 0;
	if ( numMeshObj ) {
		// vertex channel
		Log::getLog().info("buildPreviewVoxel populate VERT_GEOM_CHANNEL");

		VUtils::MeshChannel &verts_ch = voxel.channels[ ch_idx++ ];
		verts_ch.init( sizeof(VUtils::VertGeomData), meshObjInfo.getTotalPreviewVertices(), VERT_GEOM_CHANNEL, FACE_TOPO_CHANNEL, MF_VERT_CHANNEL, false);
		verts_ch.data = m_previewVerts;

		// face channel
		Log::getLog().info("buildPreviewVoxel populate FACE_TOPO_CHANNEL");

		VUtils::MeshChannel &face_ch = voxel.channels[ ch_idx++ ];
		face_ch.init( sizeof(VUtils::FaceTopoData), meshObjInfo.getTotalPreviewElements(), FACE_TOPO_CHANNEL, 0, MF_TOPO_CHANNEL, false);
		face_ch.data = m_previewFaces;

		// obj info channel
		Log::getLog().info("buildPreviewVoxel populate OBJECT_INFO_CHANNEL");

		int bufLen = 0;
		meshObjInfo.writeToBuffer(NULL, bufLen);

		VUtils::MeshChannel &info_ch = voxel.channels[ ch_idx++ ];
		info_ch.init(bufLen, 1, OBJECT_INFO_CHANNEL, 0, MF_INFO_CHANNEL);
		meshObjInfo.writeToBuffer(info_ch.data, bufLen);
	}

	if (numHairObj) {
		// vertex channel
		Log::getLog().info("buildPreviewVoxel populate HAIR_VERT_CHANNEL");

		VUtils::MeshChannel &verts_ch = voxel.channels[ ch_idx++ ];
		verts_ch.init( sizeof(VUtils::VertGeomData), hairObjInfo.getTotalPreviewVertices(), HAIR_VERT_CHANNEL, HAIR_NUM_VERT_CHANNEL, MF_VERT_CHANNEL, false);
		verts_ch.data = m_previewStrandVerts;

		// strand channel
		Log::getLog().info("buildPreviewVoxel populate HAIR_NUM_VERT_CHANNEL");

		VUtils::MeshChannel &strands_ch = voxel.channels[ ch_idx++ ];
		strands_ch.init( sizeof(int), hairObjInfo.getTotalPreviewElements(), HAIR_NUM_VERT_CHANNEL, 0, MF_NUM_VERT_CHANNEL, false);
		strands_ch.data = m_previewStrands;

		// obj info channel
		Log::getLog().info("buildPreviewVoxel populate HAIR_OBJECT_INFO_CHANNEL");

		int bufLen = 0;
		hairObjInfo.writeToBuffer(NULL, bufLen);

		VUtils::MeshChannel &info_ch = voxel.channels[ ch_idx++ ];
		info_ch.init(bufLen, 1, HAIR_OBJECT_INFO_CHANNEL, 0, MF_INFO_CHANNEL);
		hairObjInfo.writeToBuffer(info_ch.data, bufLen);
	}

	if (numUVSets) {
		// uv sets channel
		Log::getLog().info("buildPreviewVoxel populate MAYA_INFO_CHANNEL");
		int bufLen = 0;
		setsInfo.writeToBuffer(NULL, bufLen);

		VUtils::MeshChannel &info_ch = voxel.channels[ ch_idx++ ];
		info_ch.init(bufLen, 1, MAYA_INFO_CHANNEL, 0, MF_MAYA_INFO_CHANNEL);
		setsInfo.writeToBuffer(info_ch.data, bufLen);
	}

	UT_ASSERT( voxel.numChannels == ch_idx );
}


void VRayProxyExporter::createHairPreviewGeometry(VUtils::ObjectInfoChannelData &objInfo)
{
	addObjectInfoForType(MVF_HAIR_GEOMETRY_VOXEL, objInfo);

	int nTotalStrands = 0;
	int maxVertsPerStrand = 0;
	const int previewStartIdx = getPreviewStartIdx();

	// calc total number of strands for hairs that will be preview
	for (int j = previewStartIdx; j < m_geomDescrList.size(); ++j) {
		// skip geometry other than hair
		if (NOT(getVoxelFlags(j) & MVF_HAIR_GEOMETRY_VOXEL)) {
			continue;
		}

		GeometryDescription &meshDescr =  m_geomDescrList[j];
		if (NOT(meshDescr.hasValidData())) {
			continue;
		}

		VRay::VUtils::IntRefList &strands = meshDescr.getAttr("num_hair_vertices").paramValue.valRawListInt;
		for (int i = 0; i < strands.count(); ++i) {
			maxVertsPerStrand = std::max(maxVertsPerStrand, strands[i]);
		}

		nTotalStrands += strands.count();
	}

	// build hair preview geometry by skipping strands
	int numPreviewVerts = 0;
	int numPreviewStrands = 0;
	const int maxPreviewStrands = std::min(m_options.m_maxPreviewStrands, nTotalStrands);
	if (maxPreviewStrands > 0 && nTotalStrands > 0) {
		m_previewStrandVerts = new VUtils::VertGeomData[ maxPreviewStrands * maxVertsPerStrand ];
		m_previewStrands = new int[ maxPreviewStrands ];

		const float nStrands = static_cast<float>(maxPreviewStrands) / nTotalStrands;
		for (int j = previewStartIdx; j < m_geomDescrList.size(); ++j) {
			// skip geometry other than hair
			if (NOT(getVoxelFlags(j) & MVF_HAIR_GEOMETRY_VOXEL)) {
				continue;
			}

			GeometryDescription &meshDescr =  m_geomDescrList[j];
			if (NOT(meshDescr.hasValidData())) {
				continue;
			}

			VRay::VUtils::IntRefList &strands = meshDescr.getAttr("num_hair_vertices").paramValue.valRawListInt;
			VRay::VUtils::VectorRefList &vertices = meshDescr.getAttr("hair_vertices").paramValue.valRawListVector;

			const int numStrandsPrevObj = numPreviewStrands;

			int voffset = 0;
			for(int i = 0; i < strands.count(); ++i) {
				const int p0 =  i * nStrands;
				const int p1 = (i+1) * nStrands;

				if (p0 != p1 && numPreviewStrands < maxPreviewStrands) {
					m_previewStrands[ numPreviewStrands++ ] = strands[i];
					for(int k = 0; k < strands[i]; ++k) {
						VRay::Vector &v = vertices[voffset + k];
						m_previewStrandVerts[ numPreviewVerts++ ].set(v.x, v.y, v.z);
					}

					if (numPreviewStrands >= maxPreviewStrands) {
						break;
					}
				}

				voffset += strands[i];
			}

			objInfo.setElementRange(j, numStrandsPrevObj, numPreviewStrands);
			objInfo.addVoxelData(j, vertices.count(), strands.count());
		}
	}

	objInfo.setTotalPreviewVertices(numPreviewVerts);
	objInfo.setTotalPreviewElements(numPreviewStrands);

	UT_ASSERT( numPreviewVerts <= maxPreviewStrands * maxVertsPerStrand );
	UT_ASSERT( numPreviewStrands <= maxPreviewStrands );
}


void VRayProxyExporter::createMeshPreviewGeometry(VUtils::ObjectInfoChannelData &objInfo)
{
	addObjectInfoForType(MVF_GEOMETRY_VOXEL, objInfo);

	int numPreviewVerts = 0;
	int numPreviewFaces = 0;

	if (m_options.m_simplificationType == VUtils::SIMPLIFY_FACE_SAMPLING) {
		simplifyFaceSampling(numPreviewVerts, numPreviewFaces, objInfo);
	} else {
		simplifyMesh(numPreviewVerts, numPreviewFaces, objInfo);
	}

	objInfo.setTotalPreviewVertices(numPreviewVerts);
	objInfo.setTotalPreviewElements(numPreviewFaces);
}


void VRayProxyExporter::simplifyFaceSampling(int &numPreviewVerts, int &numPreviewFaces,
										   VUtils::ObjectInfoChannelData &objInfo)
{
	numPreviewVerts = 0;
	numPreviewFaces = 0;
	int nTotalFaces = 0;
	const int previewStartIdx = getPreviewStartIdx();
	for (int j = previewStartIdx; j < m_geomDescrList.size(); ++j) {
		// skip other geometry than mesh
		if (NOT(getVoxelFlags(j) & MVF_GEOMETRY_VOXEL)) {
			continue;
		}

		GeometryDescription &meshDescr =  m_geomDescrList[j];
		if (NOT(meshDescr.hasValidData())) {
			continue;
		}

		VRay::VUtils::IntRefList &faces = meshDescr.getAttr("faces").paramValue.valRawListInt;
		nTotalFaces += faces.count() / 3;
	}

	const int maxPreviewFaces = std::min(m_options.m_maxPreviewFaces, nTotalFaces);
	if (maxPreviewFaces > 0 && nTotalFaces > 0) {
		m_previewVerts = new VUtils::VertGeomData[ maxPreviewFaces * 3 ];
		m_previewFaces = new VUtils::FaceTopoData[ maxPreviewFaces ];

		const float nFaces = static_cast<float>(maxPreviewFaces) / nTotalFaces;
		for (int j = previewStartIdx; j < m_geomDescrList.size(); ++j) {
			// skip other geometry than mesh
			if (NOT(getVoxelFlags(j) & MVF_GEOMETRY_VOXEL)) {
				continue;
			}

			GeometryDescription &meshDescr =  m_geomDescrList[j];
			if (NOT(meshDescr.hasValidData())) {
				continue;
			}

			VRay::VUtils::VectorRefList &vertices = meshDescr.getAttr("vertices").paramValue.valRawListVector;
			VRay::VUtils::IntRefList &faces = meshDescr.getAttr("faces").paramValue.valRawListInt;

			int numFacesPrevObj = numPreviewFaces;

			for(int i = 0; i < faces.count() / 3; ++i) {
				const int p0 =  i * nFaces;
				const int p1 = (i+1) * nFaces;
				if (p0 != p1 && numPreviewFaces < maxPreviewFaces) {
					for(int k = 0; k < 3; ++k) {
						VRay::Vector &v = vertices[faces[i*3+k]];
						m_previewFaces[numPreviewFaces].v[k] = numPreviewVerts;
						m_previewVerts[numPreviewVerts++].set(v.x, v.y, v.z);
					}

					numPreviewFaces++;
					if (numPreviewFaces >= maxPreviewFaces)
						break;
				}
			}

			objInfo.setElementRange(j, numFacesPrevObj, numPreviewFaces);
			objInfo.addVoxelData(j, vertices.count(), faces.count());
		}
	}

	UT_ASSERT( numPreviewFaces <= maxPreviewFaces );
	UT_ASSERT( numPreviewVerts <= (maxPreviewFaces * 3) );
}


void VRayProxyExporter::simplifyMesh(int &numPreviewVerts, int &numPreviewFaces,
								   VUtils::ObjectInfoChannelData &objInfo)
{
	numPreviewVerts = 0;
	numPreviewFaces = 0;
	int nTotalFaces = 0;
	const int previewStartIdx = getPreviewStartIdx();
	VUtils::SimplifierMesh mesh;
	for (int j = previewStartIdx; j < m_geomDescrList.size(); ++j) {
		// skip other than mesh geometry
		if (NOT(getVoxelFlags(j) & MVF_GEOMETRY_VOXEL)) {
			continue;
		}

		GeometryDescription &meshDescr =  m_geomDescrList[j];
		if (NOT(meshDescr.hasValidData())) {
			continue;
		}

		VRay::VUtils::VectorRefList &vertices = meshDescr.getAttr("vertices").paramValue.valRawListVector;
		VRay::VUtils::IntRefList &faces = meshDescr.getAttr("faces").paramValue.valRawListInt;

		objInfo.addVoxelData(j, vertices.count(), faces.count());

		const int vertOffset = mesh.countVertices();
		for (int i = 0; i != vertices.size(); ++i) {
			mesh.addVertex(VUtils::Vector(vertices[i].x, vertices[i].y, vertices[i].z));
		}

		for (int i = 0; i != faces.size();) {
			int nullValues[3] = {-1,-1,-1};
			int verts[3];
			for(int p = 0 ; p < 3 ; ++p)
				verts[p] = faces[i++];

			mesh.addFace(verts, nullValues, nullValues, vertOffset);
			++nTotalFaces;
		}
	}

	const int maxPreviewFaces = VUtils::Min(m_options.m_maxPreviewFaces, nTotalFaces);
	if (maxPreviewFaces > 0) {
		VUtils::simplifyMesh(mesh, m_options.m_simplificationType, maxPreviewFaces);
		mesh.exportMesh(m_previewVerts, m_previewFaces, numPreviewVerts, numPreviewFaces);
	}
}


void VRayProxyExporter::addObjectInfoForType(uint32 type, VUtils::ObjectInfoChannelData &objInfo)
{
	objInfo.setTotalVoxelCount(m_voxels.size());
	for (int j = 0; j < m_geomDescrList.size(); ++j) {
		// skip other geometry
		if (NOT(getVoxelFlags(j) & type)) {
			continue;
		}

		GeometryDescription &meshDescr =  m_geomDescrList[j];

		UT_String fullName;
		meshDescr.m_node.getFullPath(fullName);

		VUtils::ObjectInfo info(meshDescr.m_node.getUniqueId(), fullName);
		info.setVoxelRange(j, j+1);
		objInfo.addObjectInfo(info);
	}
}


int VRayProxyExporter::getPreviewStartIdx() const
{
	if (m_options.m_lastAsPreview) {
		return std::max(static_cast<int>(m_geomDescrList.size() - 1), 0);
	}
	return 0;
}
