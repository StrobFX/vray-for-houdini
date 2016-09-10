//
// Copyright (c) 2015-2016, Chaos Software Ltd
//
// V-Ray For Houdini
//
// ACCESSIBLE SOURCE CODE WITHOUT DISTRIBUTION OF MODIFICATION LICENSE
//
// Full license text: https://github.com/ChaosGroup/vray-for-houdini/blob/master/LICENSE
//
#ifdef CGR_HAS_AUR
#include <vop_PhoenixSim.h>
#include <vfh_prm_templates.h>

#include <utility>

using namespace AurRamps;

using namespace VRayForHoudini;
using namespace VOP;

static PRM_Template * AttrItems = nullptr;

struct RampHandler: ChangeHandler {
	RampHandler(PhxShaderSim::RampData & data): m_Data(data) {}

	virtual void OnEditCurveDiagram(RampUi & curve, OnEditType editReason) {
		if (editReason == OnEdit_ChangeBegin || editReason == OnEdit_ChangeInProgress) {
			return;
		}

		const auto size = curve.pointCount(RampType_Curve);

		m_Data.xS.resize(size);
		m_Data.yS.reserve(size);
		m_Data.interps.resize(size);

		const auto newSize = curve.getCurvePoints(m_Data.xS.data(), m_Data.yS.data(), m_Data.interps.data(), size);

		// if we got less points resize down
		if (newSize != size) {
			m_Data.xS.resize(newSize);
			m_Data.yS.reserve(newSize);
			m_Data.interps.resize(newSize);
		}
	}

	virtual void OnEditColorGradient(RampUi & curve, OnEditType editReason) {
		// NOTE: m_Data.yS is of color type so it's 3 floats per point!

		if (editReason == OnEdit_ChangeBegin || editReason == OnEdit_ChangeInProgress) {
			return;
		}

		const auto size = curve.pointCount(RampType_Color);

		m_Data.xS.resize(size);
		m_Data.yS.reserve(size * 3);

		const auto newSize = curve.getColorPoints(m_Data.xS.data(), m_Data.yS.data(), size);

		// if we got less points resize down
		if (newSize != size) {
			m_Data.xS.resize(newSize);
			m_Data.yS.reserve(newSize * 3);
		}
	}

	PhxShaderSim::RampData & m_Data;
};


int rampButtonClickCB(void *data, int index, fpreal64 time, const PRM_Template *tplate)
{
	using namespace std;
	using namespace AurRamps;

	unordered_map<string, RampType> rampType;
	rampType["elum_curve"] = RampType_Curve;
	rampType["ecolor_ramp"] = RampType_Color;
	rampType["dcolor_ramp"] = RampType_Color;
	rampType["transp_curve"] = RampType_Curve;

	auto simNode = reinterpret_cast<PhxShaderSim*>(data);
	auto & rampData = simNode->m_Ramps[tplate->getToken()];

	const auto type = rampType[tplate->getToken()];
	auto ui = RampUi::createRamp(tplate->getLabel(), type, 200, 200, 300, 500, getWxWidgetsGUI(GetCurrentProcess()));

	if (!rampData.xS.empty()) {
		if (type == RampType_Curve) {
			ui->setCurvePoints(rampData.xS.data(), rampData.yS.data(), rampData.interps.data(), rampData.xS.size());
		} else {
			// NOTE: here rampData.yS is color type which is 3 floats per point so actual count is rampData.xS.size() !!
			ui->setColorPoints(rampData.xS.data(), rampData.yS.data(), rampData.xS.size());
		}
	}

	auto handler = new RampHandler(rampData);
	ui->setChangeHandler(handler);

	ui->show();

	return 1;
}

PRM_Template* PhxShaderSim::GetPrmTemplate()
{
	if (!AttrItems) {
		static Parm::PRMList paramList;
		paramList.addFromFile(Parm::PRMList::expandUiPath("CustomPhxShaderSim.ds"));
		AttrItems = paramList.getPRMTemplate();

		const int rampCount = 4;
		const char *rampNames[rampCount] = {"elum_curve", "ecolor_ramp", "dcolor_ramp", "transp_curve"};

		for (int c = 0; c < paramList.size(); ++c) {
			auto & param = AttrItems[c];

			bool isRamp = false;
			for (int r = 0; r < rampCount; ++r) {
				if (!strcmp(rampNames[r], param.getToken())) {
					isRamp = true;
					break;
				}
			}

			if (!isRamp) {
				continue;
			}

			param.setCallback(rampButtonClickCB);
		}
	}

	return AttrItems;
}

PhxShaderSim::PhxShaderSim(OP_Network *parent, const char *name, OP_Operator *entry)
    : NodeBase(parent, name, entry)
{
	m_Ramps["elum_curve"] = RampData();
	m_Ramps["ecolor_ramp"] = RampData();
	m_Ramps["dcolor_ramp"] = RampData();
	m_Ramps["transp_curve"] = RampData();
}


void PhxShaderSim::setPluginType()
{
	pluginType = "MATERIAL";
	pluginID   = "PhxShaderSim";
}

OP::VRayNode::PluginResult PhxShaderSim::asPluginDesc(Attrs::PluginDesc &pluginDesc, VRayExporter &exporter, ExportContext *parentContext)
{
	const auto t = exporter.getContext().getTime();

	RenderMode rendMode;

	// renderMode
	rendMode = static_cast<RenderMode>(evalInt("renderMode", 0, t));
	pluginDesc.addAttribute(Attrs::PluginAttr("geommode", rendMode == Volumetric_Geometry || rendMode == Volumetric_Heat_Haze || rendMode == Isosurface));
	pluginDesc.addAttribute(Attrs::PluginAttr("mesher", rendMode == Mesh));
	pluginDesc.addAttribute(Attrs::PluginAttr("rendsolid", rendMode == Isosurface));
	pluginDesc.addAttribute(Attrs::PluginAttr("heathaze", rendMode == Volumetric_Heat_Haze));

	// TODO: find a better way to pass these
	// add these so we know later in what to wrap this sim
	Attrs::PluginAttr attrRendMode("_vray_render_mode", Attrs::PluginAttr::AttrTypeIgnore);
	attrRendMode.paramValue.valInt = static_cast<int>(rendMode);
	pluginDesc.add(attrRendMode);

	const bool dynamic_geometry = evalInt("dynamic_geometry", 0, t) == 1;
	Attrs::PluginAttr attrDynGeom("_vray_dynamic_geometry", Attrs::PluginAttr::AttrTypeIgnore);
	attrRendMode.paramValue.valInt = dynamic_geometry;
	pluginDesc.add(attrDynGeom);


	const auto primVal = evalInt("pmprimary", 0, t);
	const bool enableProb = (exporter.isIPR() && primVal) || primVal == 2;
	pluginDesc.addAttribute(Attrs::PluginAttr("pmprimary", enableProb));

	exporter.setAttrsFromOpNodePrms(pluginDesc, this, "", true);

	return OP::VRayNode::PluginResultContinue;
}

#endif // CGR_HAS_AUR
