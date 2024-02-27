/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "3DModel.h"

#include "3DModelVAO.h"
#include "Game/GlobalUnsynced.h"
#include "Rendering/GL/myGL.h"
#include "Sim/Misc/CollisionVolume.h"
#include "Sim/Projectiles/ProjectileHandler.h"
#include "Sim/Objects/SolidObject.h"
#include "System/Exceptions.h"
#include "System/SafeUtil.h"

#include "System/Log/ILog.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include <tracy/Tracy.hpp>

CR_BIND(LocalModelPiece, (nullptr))
CR_REG_METADATA(LocalModelPiece, (
	CR_MEMBER(pos),
	CR_MEMBER(rot),
	CR_MEMBER(dir),
	CR_MEMBER(colvol),
	CR_MEMBER(scriptSetVisible),
	CR_MEMBER(blockScriptAnims),
	CR_MEMBER(lmodelPieceIndex),
	CR_MEMBER(scriptPieceIndex),
	CR_MEMBER(parent),
	CR_MEMBER(localModel),
	CR_MEMBER(children),

	CR_MEMBER(pseudoWorldSpacePosition),
	CR_MEMBER(pseudoWorldSpaceRotation),

	// reload
	CR_IGNORED(original),

	CR_IGNORED(dirty),
	CR_IGNORED(customDirty),
	CR_IGNORED(modelSpaceMat),
	CR_IGNORED(pieceSpaceMat),

	CR_IGNORED(lodDispLists) //FIXME GL idx!
))

CR_BIND(LocalModel, )
CR_REG_METADATA(LocalModel, (
	CR_MEMBER(pieces),

	CR_MEMBER(boundingVolume),
	CR_IGNORED(luaMaterialData),
	CR_MEMBER(needsBoundariesRecalc),
	CR_MEMBER(owningObject)
))


void S3DModelHelpers::BindLegacyAttrVBOs()
{
	S3DModelVAO::GetInstance().BindLegacyVertexAttribsAndVBOs();
}
void S3DModelHelpers::UnbindLegacyAttrVBOs()
{
	S3DModelVAO::GetInstance().UnbindLegacyVertexAttribsAndVBOs();
}

/** ****************************************************************************************************
 * S3DModelPiece
 */

void S3DModelPiece::DrawStaticLegacy(bool bind, bool bindPosMat) const
{
	if (!HasGeometryData())
		return;

	if (bind) S3DModelHelpers::BindLegacyAttrVBOs();

	if (bindPosMat) {
		glPushMatrix();
		glMultMatrixf(bposeMatrix);
		DrawElements();
		glPopMatrix();
	}
	else {
		DrawElements();
	}

	if (bind) S3DModelHelpers::UnbindLegacyAttrVBOs();
}

// only used by projectiles with the PF_Recursive flag
void S3DModelPiece::DrawStaticLegacyRec() const
{
	S3DModelHelpers::BindLegacyAttrVBOs();

	DrawStaticLegacy(false, false);

	for (const S3DModelPiece* childPiece : children) {
		childPiece->DrawStaticLegacy(false, false);
	}

	S3DModelHelpers::UnbindLegacyAttrVBOs();
}


float3 S3DModelPiece::GetEmitPos() const
{
	switch (vertices.size()) {
		case 0:
		case 1: { return ZeroVector; } break;
		default: { return GetVertexPos(0); } break;
	}
}

float3 S3DModelPiece::GetEmitDir() const
{
	switch (vertices.size()) {
		case 0: { return FwdVector; } break;
		case 1: { return GetVertexPos(0); } break;
		default: { return (GetVertexPos(1) - GetVertexPos(0)); } break;
	}
}


void S3DModelPiece::CreateShatterPieces()
{
	if (!HasGeometryData())
		return;

	shatterIndices.reserve(S3DModelPiecePart::SHATTER_VARIATIONS * indices.size());

	for (int i = 0; i < S3DModelPiecePart::SHATTER_VARIATIONS; ++i) {
		CreateShatterPiecesVariation(i);
	}
}


void S3DModelPiece::CreateShatterPiecesVariation(int num)
{
	using ShatterPartDataPair = std::pair<S3DModelPiecePart::RenderData, std::vector<uint32_t>>;
	using ShatterPartsBuffer  = std::array<ShatterPartDataPair, S3DModelPiecePart::SHATTER_MAX_PARTS>;

	ShatterPartsBuffer shatterPartsBuf;

	for (auto& [rd, idcs] : shatterPartsBuf) {
		rd.dir = (guRNG.NextVector()).ANormalize();
	}

	// helper
	const auto GetPolygonDir = [&](size_t idx)
	{
		float3 midPos;
		midPos += GetVertexPos(indices[idx + 0]);
		midPos += GetVertexPos(indices[idx + 1]);
		midPos += GetVertexPos(indices[idx + 2]);
		midPos /= 3.0f;
		return midPos.ANormalize();
	};

	// add vertices to splitter parts
	for (size_t i = 0; i < indices.size(); i += 3) {
		const float3& dir = GetPolygonDir(i);

		// find the closest shatter part (the one that points into same dir)
		float md = -2.0f;

		ShatterPartDataPair* mcp = nullptr;
		const S3DModelPiecePart::RenderData* rd = nullptr;

		for (ShatterPartDataPair& cp: shatterPartsBuf) {
			rd = &cp.first;

			if (rd->dir.dot(dir) < md)
				continue;

			md = rd->dir.dot(dir);
			mcp = &cp;
		}

		assert(mcp);

		//  + vertIndex will be added in void S3DModelVAO::ProcessIndicies(S3DModel* model)
		(mcp->second).push_back(indices[i + 0]);
		(mcp->second).push_back(indices[i + 1]);
		(mcp->second).push_back(indices[i + 2]);
	}

	{
		const size_t mapSize = indices.size();

		uint32_t indxPos = 0;

		for (auto& [rd, idcs] : shatterPartsBuf) {
			rd.indexCount = static_cast<uint32_t>(idcs.size());
			rd.indexStart = static_cast<uint32_t>(num * mapSize) + indxPos;

			if (rd.indexCount > 0) {
				shatterIndices.insert(shatterIndices.end(), idcs.begin(), idcs.end());
				indxPos += rd.indexCount;
			}
		}
	}

	{
		// delete empty splitter parts
		size_t backIdx = shatterPartsBuf.size() - 1;

		for (size_t j = 0; j < shatterPartsBuf.size() && j < backIdx; ) {
			const auto& [rd, idcs] = shatterPartsBuf[j];

			if (rd.indexCount == 0) {
				std::swap(shatterPartsBuf[j], shatterPartsBuf[backIdx--]);
				continue;
			}

			j++;
		}

		shatterParts[num].renderData.clear();
		shatterParts[num].renderData.reserve(backIdx + 1);

		// finish: copy buffer to actual memory
		for (size_t n = 0; n <= backIdx; n++) {
			shatterParts[num].renderData.push_back(shatterPartsBuf[n].first);
		}
	}
}


void S3DModelPiece::Shatter(float pieceChance, int modelType, int texType, int team, const float3 pos, const float3 speed, const CMatrix44f& m) const
{
	const float2  pieceParams = {float3::max(float3::fabs(maxs), float3::fabs(mins)).Length(), pieceChance};
	const   int2 renderParams = {texType, team};

	projectileHandler.AddFlyingPiece(modelType, this, m, pos, speed, pieceParams, renderParams);
}


void S3DModelPiece::PostProcessGeometry(uint32_t pieceIndex)
{
	if (!HasGeometryData())
		return;


	for (auto& v : vertices)
		if (v.boneIDs == SVertexData::DEFAULT_BONEIDS)
			v.boneIDs = { static_cast<uint8_t>(pieceIndex), 255, 255, 255 };
}

void S3DModelPiece::DrawElements(GLuint prim) const
{
	if (indxCount == 0)
		return;
	assert(indxCount != ~0u);

	S3DModelVAO::GetInstance().DrawElements(prim, indxStart, indxCount);
}

void S3DModelPiece::DrawShatterElements(uint32_t vboIndxStart, uint32_t vboIndxCount, GLuint prim)
{
	if (vboIndxCount == 0)
		return;

	S3DModelVAO::GetInstance().DrawElements(prim, vboIndxStart, vboIndxCount);
}

void S3DModelPiece::ReleaseShatterIndices()
{
	shatterIndices.clear();
}

/** ****************************************************************************************************
 * LocalModel
 */

void LocalModel::DrawPieces() const
{
	for (const auto& p : pieces) {
		p.Draw();
	}
}

void LocalModel::DrawPiecesLOD(uint32_t lod) const
{
	if (!luaMaterialData.ValidLOD(lod))
		return;

	for (const auto& p: pieces) {
		p.DrawLOD(lod);
	}
}

void LocalModel::SetLODCount(uint32_t lodCount)
{
	assert(Initialized());

	luaMaterialData.SetLODCount(lodCount);
	pieces[0].SetLODCount(lodCount);
}


void LocalModel::SetModel(const S3DModel* model, bool initialize)
{
	// make sure we do not get called for trees, etc
	assert(model != nullptr);
	assert(model->numPieces >= 1);

	if (!initialize) {
		assert(pieces.size() == model->numPieces);

		// PostLoad; only update the pieces
		for (size_t n = 0; n < pieces.size(); n++) {
			S3DModelPiece* omp = model->GetPiece(n);

			pieces[n].original = omp;
		}

		pieces[0].UpdateChildMatricesRec(true);
		UpdateBoundingVolume();
		return;
	}

	assert(pieces.empty());

	pieces.clear();
	pieces.reserve(model->numPieces);

	CreateLocalModelPieces(model->GetRootPiece());

	// must recursively update matrices here too: for features
	// LocalModel::Update is never called, but they might have
	// baked piece rotations (in the case of .dae)
	pieces[0].UpdateChildMatricesRec(false);
	UpdateBoundingVolume();

	assert(pieces.size() == model->numPieces);
}

LocalModelPiece* LocalModel::CreateLocalModelPieces(const S3DModelPiece* mpParent)
{
	LocalModelPiece* lmpChild = nullptr;

	// construct an LMP(mp) in-place
	pieces.emplace_back(mpParent);
	LocalModelPiece* lmpParent = &pieces.back();

	lmpParent->SetLModelPieceIndex(pieces.size() - 1);
	lmpParent->SetScriptPieceIndex(pieces.size() - 1);
	lmpParent->SetLocalModel(this);

	// the mapping is 1:1 for Lua scripts, but not necessarily for COB
	// CobInstance::MapScriptToModelPieces does the remapping (if any)
	assert(lmpParent->GetLModelPieceIndex() == lmpParent->GetScriptPieceIndex());

	for (const S3DModelPiece* mpChild: mpParent->children) {
		lmpChild = CreateLocalModelPieces(mpChild);
		lmpChild->SetParent(lmpParent);
		lmpParent->AddChild(lmpChild);
	}

	return lmpParent;
}


void LocalModel::UpdateBoundingVolume()
{
	ZoneScoped;

	// bounding-box extrema (local space)
	float3 bbMins = DEF_MIN_SIZE;
	float3 bbMaxs = DEF_MAX_SIZE;

	for (const auto& lmPiece: pieces) {
		const CMatrix44f& matrix = lmPiece.GetModelSpaceMatrix();
		const S3DModelPiece* piece = lmPiece.original;

		// skip empty pieces or bounds will not be sensible
		if (!piece->HasGeometryData())
			continue;

		// transform only the corners of the piece's bounding-box
		const float3 pMins = piece->mins;
		const float3 pMaxs = piece->maxs;
		const float3 verts[8] = {
			// bottom
			float3(pMins.x,  pMins.y,  pMins.z),
			float3(pMaxs.x,  pMins.y,  pMins.z),
			float3(pMaxs.x,  pMins.y,  pMaxs.z),
			float3(pMins.x,  pMins.y,  pMaxs.z),
			// top
			float3(pMins.x,  pMaxs.y,  pMins.z),
			float3(pMaxs.x,  pMaxs.y,  pMins.z),
			float3(pMaxs.x,  pMaxs.y,  pMaxs.z),
			float3(pMins.x,  pMaxs.y,  pMaxs.z),
		};

		for (const float3& v: verts) {
			const float3 vertex = matrix * v;

			bbMins = float3::min(bbMins, vertex);
			bbMaxs = float3::max(bbMaxs, vertex);
		}
	}

	// note: offset is relative to object->pos
	boundingVolume.InitBox(bbMaxs - bbMins, (bbMaxs + bbMins) * 0.5f);

	needsBoundariesRecalc = false;
}

/** ****************************************************************************************************
 * LocalModelPiece
 */

LocalModelPiece::LocalModelPiece(const S3DModelPiece* piece)
	: colvol(piece->GetCollisionVolume())
	, dirty(true)
	, customDirty(true)

	, scriptSetVisible(true)
	, blockScriptAnims(false)

	, lmodelPieceIndex(-1)
	, scriptPieceIndex(-1)

	, original(piece)
	, parent(nullptr) // set later
{
	assert(piece != nullptr);

	pos = piece->offset;
	dir = piece->GetEmitDir(); // warning investigated, seems fake

	pieceSpaceMat = CalcPieceSpaceMatrix(pos, rot, original->scales);

	children.reserve(piece->children.size());
}

void LocalModelPiece::SetDirty() {
	dirty = true;
	SetGetCustomDirty(true);

	for (LocalModelPiece* child: children) {
		if (child->dirty)
			continue;
		child->SetDirty();
	}
}

bool LocalModelPiece::SetGetCustomDirty(bool cd) const
{
	std::swap(cd, customDirty);
	return cd;
}

void LocalModelPiece::SetPosOrRot(const float3& src, float3& dst) {
	if (blockScriptAnims)
		return;
	if (!dirty && !dst.same(src)) {
		SetDirty();
		assert(localModel);
		localModel->SetBoundariesNeedsRecalc();
	}

	dst = src;
}


void LocalModelPiece::UpdateChildMatricesRec(bool updateChildMatrices) const
{
	if (dirty) {
		dirty = false;
		updateChildMatrices = true;

		pieceSpaceMat = CalcPieceSpaceMatrix(pos, rot, original->scales);
	}

	if (updateChildMatrices) {
		modelSpaceMat = pieceSpaceMat;
		ApplyParentMatrix(modelSpaceMat);
	}

	for (auto& child : children) {
		child->UpdateChildMatricesRec(updateChildMatrices);
	}
}

void LocalModelPiece::UpdateParentMatricesRec() const
{
	if (parent != nullptr && parent->dirty)
		parent->UpdateParentMatricesRec();

	dirty = false;

	pieceSpaceMat = CalcPieceSpaceMatrix(pos, rot, original->scales);
	modelSpaceMat = pieceSpaceMat;

	ApplyParentMatrix(modelSpaceMat);
}


void LocalModelPiece::Draw() const
{
	if (!scriptSetVisible)
		return;

	if (!original->HasGeometryData())
		return;

	assert(original);

	glPushMatrix();
	glMultMatrixf(GetModelSpaceMatrix());
	S3DModelHelpers::BindLegacyAttrVBOs();
	original->DrawElements();
	S3DModelHelpers::UnbindLegacyAttrVBOs();
	glPopMatrix();
}

void LocalModelPiece::DrawLOD(uint32_t lod) const
{
	if (!scriptSetVisible)
		return;

	if (!original->HasGeometryData())
		return;

	glPushMatrix();
	glMultMatrixf(GetModelSpaceMatrix());
	if (const auto ldl = lodDispLists[lod]; ldl == 0) {
		S3DModelHelpers::BindLegacyAttrVBOs();
		original->DrawElements();
		S3DModelHelpers::UnbindLegacyAttrVBOs();
	} else {
		glCallList(ldl);
	}
	glPopMatrix();
}



void LocalModelPiece::SetLODCount(uint32_t count)
{
	// any new LOD's get null-lists first
	lodDispLists.resize(count, 0);

	for (uint32_t i = 0; i < children.size(); i++) {
		children[i]->SetLODCount(count);
	}
}


bool LocalModelPiece::GetEmitDirPos(float3& emitPos, float3& emitDir) const
{
	if (original == nullptr)
		return false;

	// note: actually OBJECT_TO_WORLD but transform is the same
	emitPos = GetModelSpaceMatrix() *        original->GetEmitPos()        * WORLD_TO_OBJECT_SPACE;
	emitDir = GetModelSpaceMatrix() * float4(original->GetEmitDir(), 0.0f) * WORLD_TO_OBJECT_SPACE;
	return true;
}

void LocalModelPiece::ApplyParentMatrix(CMatrix44f &inOutMat) const {
	if (parent != nullptr) {
		inOutMat >>= parent->modelSpaceMat;
	}

	if (localModel->owningObject != nullptr && (pseudoWorldSpacePosition || pseudoWorldSpaceRotation)) {
		const auto worldMat = localModel->owningObject->GetTransformMatrix(true);
		// the line below instead gets the "unsynced only(?)" interpolated position for drawing
		// which is much, much nicer visually (no need to add radar-jitter here)
//		const auto worldMat = localModel->owningObject->GetTransformMatrix(false, true);
		if (pseudoWorldSpacePosition) {
			auto target = pos - worldMat.GetPos();
			auto len = target.LengthNormalize();
			inOutMat.SetPos(localModel->owningObject->GetObjectSpaceVec(target) * WORLD_TO_OBJECT_SPACE * len);
		}

		if (pseudoWorldSpaceRotation) {
			inOutMat.RotateEulerZXY(-worldMat.GetEulerAnglesLftHand());
		}

		// never be clean
		// world space position and rotation are almost always changing
		dirty = true;
		SetGetCustomDirty(true);

		for (LocalModelPiece* child: children) {
			if (child->dirty)
				continue;
			child->SetDirty();
		}
	}
}

/******************************************************************************/
/******************************************************************************/

S3DModelPiece* S3DModel::FindPiece(const std::string& name)
{
	const auto it = std::find_if(pieceObjects.begin(), pieceObjects.end(), [&name](const S3DModelPiece* piece) {
		return piece->name == name;
	});
	if (it == pieceObjects.end())
		return nullptr;

	return *it;
}

const S3DModelPiece* S3DModel::FindPiece(const std::string& name) const
{
	const auto it = std::find_if(pieceObjects.begin(), pieceObjects.end(), [&name](const S3DModelPiece* piece) {
		return piece->name == name;
		});
	if (it == pieceObjects.end())
		return nullptr;

	return *it;
}

size_t S3DModel::FindPieceOffset(const std::string& name) const
{
	const auto it = std::find_if(pieceObjects.begin(), pieceObjects.end(), [&name](const S3DModelPiece* piece) {
		return piece->name == name;
	});

	if (it == pieceObjects.end())
		return size_t(-1);

	return std::distance(pieceObjects.begin(), it);
}
