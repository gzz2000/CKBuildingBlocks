/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
//
//		            TT Simple Shadow
//
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
#include "CKAll.h"

#include <array>
#include <memory>
#include <vector>

#include "../SimpleShadowSnapshot.h"

#define TERRATOOLS_GUID CKGUID(0x56495254, 0x4f4f4c53)

#define A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT 50

CKObjectDeclaration *FillBehaviorTTSimpleShadowDecl();
CKERROR CreateTTSimpleShadowProto(CKBehaviorPrototype **pproto);
int TTSimpleShadow(const CKBehaviorContext &behcontext);
CKERROR TTSimpleShadowCallBack(const CKBehaviorContext &behcontext);
void TTSimpleShadowRenderCallback(CKRenderContext *dev, void *arg);

////////////////////////
//   Local Structure
////////////////////////
typedef struct
{
    CK_ID matID;                                     // material to be projected
    CK_ID texID;                                     // texture to be associated with the projected material
    float zoom;                                      // zoom of the soft_shadow
    CK_ID floor[A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT]; // floors under the object ...
    int nb_floors_under;                             // number of floors being under the object...
} SimpleShadowStruct;

namespace {
struct SavedSimpleShadow {
    CK_ID behaviorId = 0;
    CKBehavior *behaviorIdentity = nullptr;
    CK_ID materialId = 0;
    CKMaterial *materialIdentity = nullptr;
    CK_ID textureId = 0;
    float zoom = 0.0f;
    std::array<CK_ID, A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT> floors{};
    std::array<CK3dEntity *, A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT> floorIdentities{};
    int floorCount = 0;
};

struct SimpleShadowSnapshot {
    std::vector<SavedSimpleShadow> shadows;
};
}

static void A_GetFloors(SimpleShadowStruct *tss, CK3dEntity *ent, CKBehavior *beh, VxVector *pos_rel, VxVector *scale, float maxHeight);
static void A_Delete_SoftShadow_From_Floors(CK_ID *floor, CKMaterial *cmat, int nb_floors);

extern "C" int TT_SimpleShadowCaptureSnapshot(
    CKContext *context, TT_SimpleShadowSnapshotHandle *outSnapshot)
{
    if (!context || !outSnapshot)
        return CKERR_INVALIDPARAMETER;
    std::unique_ptr<SimpleShadowSnapshot> snapshot(new SimpleShadowSnapshot());
    const XObjectPointerArray &objects =
        context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
    const CKGUID guid(0x7f0517c2, 0x52cc76bb);
    for (CKObject *object : objects) {
        CKBehavior *behavior = CKBehavior::Cast(object);
        if (!behavior || behavior->GetPrototypeGuid() != guid)
            continue;
        SimpleShadowStruct *runtime = nullptr;
        behavior->GetLocalParameterValue(0, &runtime);
        if (!runtime)
            continue;
        SavedSimpleShadow saved;
        saved.behaviorId = behavior->GetID();
        saved.behaviorIdentity = behavior;
        saved.materialId = runtime->matID;
        saved.materialIdentity = CKMaterial::Cast(context->GetObject(runtime->matID));
        saved.textureId = runtime->texID;
        saved.zoom = runtime->zoom;
        saved.floorCount = runtime->nb_floors_under;
        if (saved.floorCount < 0)
            saved.floorCount = 0;
        if (saved.floorCount > A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT)
            saved.floorCount = A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT;
        for (int i = 0; i < saved.floorCount; ++i) {
            saved.floors[i] = runtime->floor[i];
            saved.floorIdentities[i] =
                CK3dEntity::Cast(context->GetObject(runtime->floor[i]));
        }
        snapshot->shadows.push_back(saved);
    }
    *outSnapshot = snapshot.release();
    return CK_OK;
}

extern "C" int TT_SimpleShadowRestoreSnapshot(
    CKContext *context, TT_SimpleShadowSnapshotHandle opaque)
{
    if (!context || !opaque)
        return CKERR_INVALIDPARAMETER;
    auto *snapshot = static_cast<SimpleShadowSnapshot *>(opaque);
    for (const SavedSimpleShadow &saved : snapshot->shadows) {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(saved.behaviorId));
        if (behavior != saved.behaviorIdentity)
            continue;
        SimpleShadowStruct *runtime = nullptr;
        behavior->GetLocalParameterValue(0, &runtime);
        if (!runtime)
            continue;
        // The material is an implementation-owned render cache. Ball death
        // tears it down and creates a new CKMaterial identity, so restoring a
        // stale ID (or serializing its mesh channel as ordinary topology)
        // leaves an inert shadow channel. Reuse the live cache object and let
        // the next pre-render callback derive floor membership and UVs again.
        CKMaterial *material = CKMaterial::Cast(context->GetObject(runtime->matID));
        if (material)
            A_Delete_SoftShadow_From_Floors(
                runtime->floor, material, runtime->nb_floors_under);
        if (!material) {
            material = static_cast<CKMaterial *>(context->CreateObject(
                CKCID_MATERIAL, "TT_SimpleShadow Material",
                behavior->IsDynamic() ? CK_OBJECTCREATION_DYNAMIC
                                      : CK_OBJECTCREATION_NONAMECHECK));
            if (!material)
                continue;
            material->SetEmissive(VxColor(255, 255, 255));
            material->SetDiffuse(VxColor(255, 255, 255, 255));
            material->SetSpecular(VxColor(0, 0, 0));
            material->SetAmbient(VxColor(0, 0, 0));
            material->SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP);
            material->SetTextureBlendMode(VXTEXTUREBLEND_COPY);
        }
        runtime->matID = material->GetID();
        runtime->texID = saved.textureId;
        runtime->zoom = saved.zoom;
        runtime->nb_floors_under = 0;
        memset(runtime->floor, 0, sizeof(runtime->floor));
        material->SetTexture0(CKTexture::Cast(context->GetObject(saved.textureId)));
        behavior->SetLocalParameterValue(0, &runtime);
    }
    return CK_OK;
}

extern "C" int TT_SimpleShadowIsRuntimeMaterial(
    CKContext *context, CKMaterial *material)
{
    if (!context || !material)
        return FALSE;
    const XObjectPointerArray &objects =
        context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
    const CKGUID guid(0x7f0517c2, 0x52cc76bb);
    for (CKObject *object : objects) {
        CKBehavior *behavior = CKBehavior::Cast(object);
        if (!behavior || behavior->GetPrototypeGuid() != guid)
            continue;
        SimpleShadowStruct *runtime = nullptr;
        behavior->GetLocalParameterValue(0, &runtime);
        if (runtime && runtime->matID == material->GetID())
            return TRUE;
    }
    return FALSE;
}

extern "C" void TT_SimpleShadowDestroySnapshot(
    TT_SimpleShadowSnapshotHandle snapshot)
{
    delete static_cast<SimpleShadowSnapshot *>(snapshot);
}

static void A_ResetSimpleShadowData(CKBehavior *beh, CKContext *context)
{
    if (!beh || !context)
        return;

    SimpleShadowStruct *tss = NULL;
    beh->GetLocalParameterValue(0, &tss);
    if (!tss)
        return;

    CKMaterial *mat = (CKMaterial *)context->GetObject(tss->matID);
    if (mat)
    {
        A_Delete_SoftShadow_From_Floors(tss->floor, mat, tss->nb_floors_under);
        mat->SetTexture0(NULL);
        context->DestroyObject(mat);
    }

    delete tss;
    tss = NULL;
    beh->SetLocalParameterValue(0, &tss);
}

CKObjectDeclaration *FillBehaviorTTSimpleShadowDecl()
{
    CKObjectDeclaration *od = CreateCKObjectDeclaration("TT Simple Shadow");
    od->SetDescription("Display a little soft shadow on floors beneath the object.");
    od->SetCategory("TT Gravity");
    od->SetType(CKDLL_BEHAVIORPROTOTYPE);
    od->SetGuid(CKGUID(0x7f0517c2, 0x52cc76bb));
    od->SetAuthorGuid(TERRATOOLS_GUID);
    od->SetAuthorName("Terratools");
    od->SetVersion(0x00010000);
    od->SetCreationFunction(CreateTTSimpleShadowProto);
    od->SetCompatibleClassId(CKCID_3DOBJECT);
    od->NeedManager(FLOOR_MANAGER_GUID);
    return od;
}

CKERROR CreateTTSimpleShadowProto(CKBehaviorPrototype **pproto)
{
    CKBehaviorPrototype *proto = CreateCKBehaviorPrototype("TT Simple Shadow");
    if (!proto) return CKERR_OUTOFMEMORY;

    proto->DeclareInput("On");
    proto->DeclareInput("Off");

    proto->DeclareOutput("Exit On");
    proto->DeclareOutput("Exit Off");

    proto->DeclareInParameter("Texture", CKPGUID_TEXTURE);
    proto->DeclareInParameter("Size Scale", CKPGUID_FLOAT, "2.0");
    proto->DeclareInParameter("Maximum Height", CKPGUID_FLOAT, "100.0");

    proto->DeclareLocalParameter("Data", CKPGUID_POINTER, "NULL");

    proto->SetFlags(CK_BEHAVIORPROTOTYPE_NORMAL);
    proto->SetFunction(TTSimpleShadow);

    proto->SetBehaviorFlags(CKBEHAVIOR_TARGETABLE);
    proto->SetBehaviorCallbackFct(TTSimpleShadowCallBack);

    *pproto = proto;
    return CK_OK;
}

int TTSimpleShadow(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKRenderContext *dev = behcontext.CurrentRenderContext;
    CKContext *context = behcontext.Context;
    if (!dev || !context)
        return CKBR_PARAMETERERROR;

    if (!beh->GetTarget())
        return CKBR_OWNERERROR;

    if (beh->IsInputActive(1))
    {
        beh->ActivateInput(1, FALSE);
        beh->ActivateOutput(1, TRUE);

        A_ResetSimpleShadowData(beh, context);
        return CKBR_OK;
    }

    if (beh->IsInputActive(0))
    {
        beh->ActivateInput(0, FALSE);
        beh->ActivateOutput(0, TRUE);

        A_ResetSimpleShadowData(beh, context);

        // creation of a CkMaterial
        CKMaterial *mat = (CKMaterial *)context->CreateObject(CKCID_MATERIAL, "TT_SimpleShadow Material",
                                                              beh->IsDynamic() ? CK_OBJECTCREATION_DYNAMIC : CK_OBJECTCREATION_NONAMECHECK);
        if (!mat)
            return CKBR_PARAMETERERROR;
        mat->SetEmissive(VxColor(255, 255, 255));
        mat->SetDiffuse(VxColor(255, 255, 255, 255));
        mat->SetSpecular(VxColor(0, 0, 0));
        mat->SetAmbient(VxColor(0, 0, 0));
        mat->SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP);
        mat->SetTextureBlendMode(VXTEXTUREBLEND_COPY);

        SimpleShadowStruct *tss = new SimpleShadowStruct;
        tss->matID = CKOBJID(mat);
        tss->texID = -1;
        tss->zoom = 2.0f;
        tss->nb_floors_under = 0;
        memset(tss->floor, 0, sizeof(tss->floor));

        beh->SetLocalParameterValue(0, &tss);
    }

    dev->AddPreRenderCallBack(TTSimpleShadowRenderCallback, beh, TRUE);
    return CKBR_ACTIVATENEXTFRAME;
}

CKERROR TTSimpleShadowCallBack(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKRenderContext *dev = behcontext.CurrentRenderContext;
    CKContext *context = behcontext.Context;

    switch (behcontext.CallbackMessage)
    {
    case CKM_BEHAVIORDELETE:
    case CKM_BEHAVIORDETACH:
    case CKM_BEHAVIORRESET:
    case CKM_BEHAVIORNEWSCENE:
    case CKM_BEHAVIORDEACTIVATESCRIPT:
        if (dev)
            dev->RemovePreRenderCallBack(TTSimpleShadowRenderCallback, beh);
        A_ResetSimpleShadowData(beh, context);
        break;
    case CKM_BEHAVIORPAUSE:
        if (dev)
            dev->RemovePreRenderCallBack(TTSimpleShadowRenderCallback, beh);
        break;
    case CKM_BEHAVIORRESUME:
        if (dev)
            dev->AddPreRenderCallBack(TTSimpleShadowRenderCallback, beh, TRUE);
        break;
    }

    return CKBR_OK;
}

void TTSimpleShadowRenderCallback(CKRenderContext *dev, void *arg)
{
    CKBehavior *beh = (CKBehavior *)arg;
    if (!beh)
        return;
    CKContext *context = dev->GetCKContext();

    CK3dEntity *entity = (CK3dEntity *)beh->GetTarget();
    if (!entity)
        return;

    SimpleShadowStruct *tss = NULL;
    beh->GetLocalParameterValue(0, &tss);
    if (!tss)
        return;

    CKMaterial *mat = (CKMaterial *)context->GetObject(tss->matID);
    if (!mat)
        return;

    //////////////////////////////////////////////////
    //  We get the Floors that WERE under the object
    //////////////////////////////////////////////////
    int old_nb_floors_under = tss->nb_floors_under;

#if CKVERSION == 0x13022002
    CKMemoryPool memoryPool1(context, old_nb_floors_under * sizeof(CK_ID));
    CK_ID *old_floor = (CK_ID *)memoryPool1.Mem();
#else
    VxScratch mempool1(old_nb_floors_under * sizeof(CK_ID));
    CK_ID *old_floor = (CK_ID *)mempool1.Mem();
#endif

    memcpy(old_floor, tss->floor, old_nb_floors_under * sizeof(CK_ID));

    // we get the input texture id
    CK_ID newtexID;
    beh->GetInputParameterValue(0, &newtexID);

    // if the texture change ( or at start )
    if (newtexID != tss->texID)
    {
        tss->texID = newtexID;
        CKTexture *tex = (CKTexture *)context->GetObject(tss->texID);
        mat->SetTexture0(tex);
    }

    // we get the ZOOM factor
    float zoom = 2.0f;
    beh->GetInputParameterValue(1, &zoom);
    tss->zoom = zoom;

    float maxHeight = 100.0f;
    beh->GetInputParameterValue(2, &maxHeight);

    // bbox local multiply by world scale
    const VxBbox &Bbox_obj = entity->GetBoundingBox(TRUE);
    float width = (Bbox_obj.Max.x - Bbox_obj.Min.x) * Magnitude(entity->GetWorldMatrix()[0]);
    if (zoom == 0.0f || width == 0.0f)
        return;
    float inv_zoom = 1.0f / (zoom * width);

    //////////////////////////////////////////////////
    //  We get the Floors that ARE under the object
    //////////////////////////////////////////////////
#if CKVERSION == 0x13022002
    CKMemoryPool memoryPool2(context, A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT);
    CKMemoryPool memoryPool3(context, A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT);
    VxVector* pos_rel = (VxVector*)memoryPool2.Mem();
    VxVector* scale = (VxVector*)memoryPool3.Mem();
#else
    VxScratch mempool2(A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT * sizeof(VxVector) * 2);
    VxVector* pos_rel = (VxVector*)mempool2.Mem();
    VxVector* scale = pos_rel + A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT;
#endif

    A_GetFloors(tss, entity, beh, pos_rel, scale, maxHeight);
    CK_ID *floor = tss->floor;

    //////////////////////////////////////////////////
    //  We REMOVE the material from floors
    //  that are no longer under the object
    //////////////////////////////////////////////////
    int a, b;
    CK3dEntity *o;
    CKMesh *dMesh;
    int matChannel;

    for (a = 0; a < old_nb_floors_under; a++)
    {
        // list of OLD floors
        CKBOOL out = TRUE;
        for (b = 0; b < tss->nb_floors_under; b++)
        {
            // list of NEW floors
            if (old_floor[a] == floor[b])
            {
                out = FALSE;
                break;
            }
        }
        if (out)
        {
            o = (CK3dEntity *)context->GetObject(old_floor[a]);
            if (CKIsChildClassOf(o, CKCID_3DENTITY))
            {
                dMesh = (CKMesh *)o->GetCurrentMesh();
                if (dMesh)
                {
                    matChannel = dMesh->GetChannelByMaterial(mat);
                    if (matChannel >= 0)
                        dMesh->RemoveChannel(matChannel);
                }
            }
        }
    }

    //////////////////////////////////////////////////
    //  We ADD the material to the floors
    //  that are under the object
    //////////////////////////////////////////////////

    Vx2DVector *uvArray;
    CKDWORD stride, cStride;
    VxVector *vertexArray;

    int i, vertexCount;

    VxVector vPos;
    VxVector vz(0.0f, 0.0f, 1.0f);

    float tmp_x, tmp_z;

    for (a = 0; a < tss->nb_floors_under; a++)
    {
        o = (CK3dEntity *)context->GetObject(floor[a]);
        if (CKIsChildClassOf(o, CKCID_3DENTITY))
        {
            dMesh = (CKMesh *)o->GetCurrentMesh();
            if (dMesh)
            {
                matChannel = dMesh->GetChannelByMaterial(mat);

                // There were no shadow channel applied to the mesh
                if (matChannel < 0)
                {
                    matChannel = dMesh->AddChannel(mat, FALSE);
                    dMesh->SetChannelSourceBlend(matChannel, VXBLEND_ZERO);
                    dMesh->SetChannelDestBlend(matChannel, VXBLEND_SRCCOLOR);
                }

                ////////////////////////////////////////////////////////
                //  We now change the U,V coords of the shadow channel
                ////////////////////////////////////////////////////////
                uvArray = (Vx2DVector *)dMesh->GetTextureCoordinatesPtr(&cStride, matChannel);
                vertexArray = (VxVector *)dMesh->GetPositionsPtr(&stride);

                vertexCount = dMesh->GetVertexCount();

                tmp_x = inv_zoom * scale[a].x;
                tmp_z = inv_zoom * scale[a].z;

                vPos.x = pos_rel[a].x * tmp_x + 0.5f;
                vPos.z = pos_rel[a].z * tmp_z + 0.5f;

                for (i = 0; i < vertexCount; i++,
                        vertexArray = (VxVector *) ((CKBYTE *) vertexArray + stride),
                        uvArray = (Vx2DVector *) ((CKBYTE *) uvArray + cStride))
                {
                    // Transform Vertex Position from mesh coordinates system to projector coordinate system
                    uvArray->x = vPos.x - vertexArray->x * tmp_x;
                    uvArray->y = vPos.z - vertexArray->z * tmp_z;
                }

                dMesh->UVChanged();
            }
        }
    }

    beh->SetLocalParameterValue(0, &tss);
}

/*****************************************************/
/*                  A_GetFloors                      */
/*****************************************************/
//  This function puts in the local structures, the floors under the object
static void A_GetFloors(SimpleShadowStruct *tss, CK3dEntity *ent, CKBehavior *beh, VxVector *pos_rel, VxVector *scale_array, float maxHeight)
{
    CKContext *context = beh->GetCKContext();

    const VxBbox &Bbox_obj = ent->GetBoundingBox();
    float inv_zoom = 1.0f / (tss->zoom * (Bbox_obj.Max.x - Bbox_obj.Min.x));

    CKFloorManager *FloorManager = (CKFloorManager *)context->GetManagerByGuid(FLOOR_MANAGER_GUID);
    int floorAttribute = FloorManager->GetFloorAttribute();

    CKAttributeManager *attman = beh->m_Context->GetAttributeManager();
    const XObjectPointerArray &floor_objects = attman->GetGlobalAttributeListPtr(floorAttribute);

    float tmp_x, tmp_z;
    VxVector vPos, scale;

    CKBOOL under;
    int nbf_under = 0; // number of floors under

    CK3dEntity *floor;
    int floor_count = floor_objects.Size();

    for (int n = 0; n < floor_count; ++n)
    {
        floor = (CK3dEntity *)floor_objects[n];

        if (floor->IsVisible() && !floor->IsAllOutsideFrustrum())
        {
            // ... and just the VISIBLE floors
            // rem: IsAllOutsideFrustrum() function will result in a 1 frame delayed appearance
            // if the floor was totally outside the viewing frustum during the previous rendering,
            // and becomes very visible now... (if this is the case, the IsAllOutsideFrustrum()
            // function can be removed ).

            under = FALSE;

            const VxBbox &Bbox_floorWorld = floor->GetBoundingBox();

            // test the two world bbox on XZ plane
            if (Bbox_floorWorld.Min.x > Bbox_obj.Max.x ||
                Bbox_floorWorld.Min.z > Bbox_obj.Max.z ||
                Bbox_floorWorld.Max.x < Bbox_obj.Min.x ||
                Bbox_floorWorld.Max.z < Bbox_obj.Min.z)
                continue;

            // test the two bbox on height
            if (Bbox_floorWorld.Min.y > Bbox_obj.Max.y ||           // floor is above the object
                Bbox_obj.Min.y - Bbox_floorWorld.Max.y > maxHeight) // floor is too below
                continue;

            ent->GetPosition(&vPos, floor);
            const VxBbox &Bbox_floor = floor->GetBoundingBox(TRUE);

            floor->GetScale(&scale);

            tmp_x = inv_zoom * scale.x;
            tmp_z = inv_zoom * scale.z;

            if ((Bbox_floor.Max.x - vPos.x) * tmp_x >= -0.5f)
                if ((Bbox_floor.Min.x - vPos.x) * tmp_x <= 0.5f)
                    if ((Bbox_floor.Max.z - vPos.z) * tmp_z >= -0.5f)
                        if ((Bbox_floor.Min.z - vPos.z) * tmp_z <= 0.5f)
                        {
                            // floors is UNDER if the UV of its Bbox
                            // are possibly in the texture (0-1)
                            under = TRUE;
                        }

            if (under)
            {
                // ... and just floors that should be UNDER the object
                if (nbf_under < A_MAX_NUMBER_OF_FLOOR_UNDER_OBJECT)
                {
                    scale_array[nbf_under] = scale;
                    pos_rel[nbf_under] = vPos;
                    tss->floor[nbf_under] = floor->GetID();
                    nbf_under++;
                }
            }
        }
    }

    tss->nb_floors_under = nbf_under;
}

/*****************************************************/
/*        A_Delete_SoftShadow_From_Floors            */
/*****************************************************/
//  This function remove the material channel 'cmat' from all floors stocked in 'floor[]'
static void A_Delete_SoftShadow_From_Floors(CK_ID *floor, CKMaterial *cmat, int nb_floors)
{
    CKContext *context = cmat->GetCKContext();

    CK3dEntity *o;
    CKMesh *dMesh;
    int matChannel;

    for (int a = 0; a < nb_floors; a++)
    {
        o = (CK3dEntity *)context->GetObject(floor[a]);
        if (CKIsChildClassOf(o, CKCID_3DENTITY))
        {
            dMesh = (CKMesh *)o->GetCurrentMesh();
            if (dMesh)
            {
                matChannel = dMesh->GetChannelByMaterial(cmat);
                if (matChannel >= 0)
                    dMesh->RemoveChannel(matChannel);
            }
        }
    }
}
