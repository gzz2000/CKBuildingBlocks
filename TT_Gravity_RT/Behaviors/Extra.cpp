/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
//
//		              TT Extra
//
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
#include "CKAll.h"
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
#include "VxLegacyMatrix.h"
#endif

#include <cstring>
#include <memory>
#include <vector>

#include "../ExtraSnapshot.h"

#define TERRATOOLS_GUID CKGUID(0x56495254, 0x4f4f4c53)

CKObjectDeclaration *FillBehaviorExtraDecl();
CKERROR CreateExtraProto(CKBehaviorPrototype **pproto);
int Extra(const CKBehaviorContext &behcontext);
CKERROR ExtraCallBack(const CKBehaviorContext &behcontext);

CKObjectDeclaration *FillBehaviorExtraDecl()
{
    CKObjectDeclaration *od = CreateCKObjectDeclaration("TT Extra");
    od->SetDescription("Manages the Powerball extras.");
    od->SetCategory("TT Gravity");
    od->SetType(CKDLL_BEHAVIORPROTOTYPE);
    od->SetGuid(CKGUID(0x36106bd9, 0x51813906));
    od->SetAuthorGuid(TERRATOOLS_GUID);
    od->SetAuthorName("Terratools");
    od->SetVersion(0x00010000);
    od->SetCreationFunction(CreateExtraProto);
    od->SetCompatibleClassId(CKCID_3DENTITY);
    return od;
}

CKERROR CreateExtraProto(CKBehaviorPrototype **pproto)
{
    CKBehaviorPrototype *proto = CreateCKBehaviorPrototype("TT Extra");
    if (!proto) return CKERR_OUTOFMEMORY;

    proto->DeclareInput("On");
    proto->DeclareInput("Off");
    proto->DeclareInput("Initialize");

    proto->DeclareOutput("Activated");
    proto->DeclareOutput("Hit");
    proto->DeclareOutput("Ready");

    proto->DeclareInParameter("Ball", CKPGUID_3DENTITY);
    proto->DeclareInParameter("Number of Smallballs", CKPGUID_INT, "6");
    proto->DeclareInParameter("Activationdistance", CKPGUID_FLOAT, "2");
    proto->DeclareInParameter("Extra_Points CollDistance", CKPGUID_FLOAT, "2");
    proto->DeclareInParameter("Rotationspeed", CKPGUID_FLOAT, "5");
    proto->DeclareInParameter("Awayforce", CKPGUID_FLOAT, "50");
    proto->DeclareInParameter("Awaydamping", CKPGUID_FLOAT, "0.5");
    proto->DeclareInParameter("Force", CKPGUID_FLOAT, "0.06");
    proto->DeclareInParameter("Damping", CKPGUID_FLOAT, "0.9");
    proto->DeclareInParameter("Forcewidth", CKPGUID_FLOAT, "0.06");
    proto->DeclareInParameter("Flyawaytime", CKPGUID_FLOAT, "1000.0");
    proto->DeclareInParameter("Hitframegroup", CKPGUID_GROUP);
    proto->DeclareInParameter("Flying Extra?", CKPGUID_BOOL, "FALSE");
    proto->DeclareInParameter("Exactness Framedelay", CKPGUID_INT, "2");

    proto->DeclareOutParameter("Current Hits", CKPGUID_INT, "0");

    proto->DeclareLocalParameter("Activationstatus", CKPGUID_BOOL, "FALSE");
    proto->DeclareLocalParameter("Smallballlist", CKPGUID_POINTER);
    proto->DeclareLocalParameter("Shadowobject", CKPGUID_3DENTITY);
    proto->DeclareLocalParameter("Centerobject", CKPGUID_3DENTITY);
    proto->DeclareLocalParameter("Status", CKPGUID_INT, "-1");
    proto->DeclareLocalParameter("Initialized", CKPGUID_BOOL, "FALSE");
    proto->DeclareLocalParameter("Timecounter", CKPGUID_FLOAT, "0");
    proto->DeclareLocalParameter("Ballcounter", CKPGUID_INT, "0");
    proto->DeclareLocalParameter("Timevalue", CKPGUID_FLOAT, "0");
    proto->DeclareLocalParameter("Next Check", CKPGUID_INT, "0");
    proto->DeclareLocalParameter("Away Positionsave", CKPGUID_VECTOR);

    proto->DeclareSetting("Exactness?", CKPGUID_BOOL, "TRUE");

    proto->SetFlags(CK_BEHAVIORPROTOTYPE_NORMAL);
    proto->SetFunction(Extra);

    proto->SetBehaviorFlags(CKBEHAVIOR_TARGETABLE);
    proto->SetBehaviorCallbackFct(ExtraCallBack);

    *pproto = proto;
    return CK_OK;
}

#define ON 0
#define OFF 1
#define INITIALIZE 2

#define ACTIVATED 0
#define HIT 1
#define READY 2

#define BALL 0
#define NUM_SMALLBALLS 1
#define ACTIVATION_DISTANCE 2
#define EXTRA_POINTS_COLLDISTANCE 3
#define ROTATION_SPEED 4
#define AWAY_FORCE 5
#define AWAY_DAMPING 6
#define FORCE 7
#define DAMPING 8
#define FORCE_WIDTH 9
#define FLYAWAY_TIME 10
#define HIT_FRAME_GROUP 11
#define FLYING_EXTRA 12
#define EXACTNESS_FRAME_DELAY 13
#define CURRENT_HITS 14

#define ACTIVATION_STATUS 0
#define SMALLBALL_LIST 1
#define SHADOW_OBJECT 2
#define CENTER_OBJECT 3
#define STATUS 4
#define INITIALIZED 5
#define TIME_COUNTER 6
#define BALL_COUNTER 7
#define TIME_VALUE 8
#define NEXT_CHECK 9
#define AWAY_POSITION_SAVE 10
#define EXACTNESS 11

typedef enum ExtraState
{
    EXTRA_IDLE = 0,
    EXTRA_ACTIVATED = 1,
    EXTRA_HIT = 2,
    EXTRA_END = 3,
    EXTRA_NONE = -1
} ExtraState;

struct SmallBall
{
    SmallBall *next;
    CK3dEntity *ball;
    VxVector position;
    float force;
    bool flag;

    SmallBall(SmallBall *n, CK3dEntity *b, const VxVector &pos, float f, bool init)
        : next(n), ball(b), position(pos), force(f), flag(init) {}
};

namespace {
struct SavedSmallBall
{
    CK_ID ballId;
    CK3dEntity *identity;
    VxVector position;
    float force;
    bool flag;
};

struct SavedExtra
{
    CK_ID behaviorId;
    CKBehavior *identity;
    std::vector<SavedSmallBall> balls;
};

struct ExtraSnapshot
{
    std::vector<SavedExtra> extras;
};

void DeleteSmallBallList(SmallBall *list)
{
    while (list)
    {
        SmallBall *next = list->next;
        delete list;
        list = next;
    }
}
}

const char *indices[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
static const int kMaxSmallBallCount = sizeof(indices) / sizeof(indices[0]);

CK3dEntity *GetChildOfTarget(int prefixLength, const char *name, CK3dEntity *target)
{
    if (!target || !name || name[0] == '\0')
        return NULL;

    CK3dEntity *result = NULL;

    const CKDWORD length = (int)strlen(name);
    const int count = target->GetChildrenCount();
    for (int i = 0; i < count; ++i)
    {
        CK3dEntity *child = target->GetChild(i);
        CKSTRING childName = child->GetName();
        if (!childName)
            continue;
        if ((int)strlen(childName) <= prefixLength)
            continue;
        childName += prefixLength;
        if (strncmp(name, childName, length) == 0)
        {
            result = child;
            break;
        }
    }

    return result;
}

int DestroyExtra(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    SmallBall *smallBalls = NULL;
    beh->GetLocalParameterValue(SMALLBALL_LIST, &smallBalls);
    if (smallBalls)
    {
        CKScene *scene = context->GetCurrentScene();
        if (!scene)
            return CKBR_PARAMETERERROR;

        SmallBall *cur = smallBalls;
        SmallBall *next = NULL;
        do
        {
            CK3dEntity *child = cur->ball->GetChild(0);
            if (child)
            {
                CKBehavior *script = child->GetScript(0);
                if (script)
                    scene->DeActivate(script);
                else
                    context->OutputToConsoleEx("4Script not found %s", child->GetName());
            }

            child = cur->ball->GetChild(0);
            if (child)
            {
                CKBehavior *script = child->GetScript(0);
                if (script)
                    scene->DeActivate(script);
                else
                    context->OutputToConsoleEx("5Script not found %s", child->GetName());
            }

            cur->ball->Show();

            next = smallBalls->next;
            delete smallBalls;
            cur = next;
            smallBalls = next;
        } while (next);

        beh->SetLocalParameterValue(SMALLBALL_LIST, &smallBalls);
    }

    return CKBR_OK;
}

int ResetExtra(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;

    if (DestroyExtra(behcontext) != CKBR_OK)
        return CKBR_PARAMETERERROR;

    CKBOOL initialized = FALSE;
    beh->SetLocalParameterValue(INITIALIZED, &initialized);

    ExtraState status = EXTRA_NONE;
    beh->SetLocalParameterValue(STATUS, &status);

    return CKBR_OK;
}

int InitializeExtra(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    CKScene *scene = context->GetCurrentScene();
    if (!scene)
        return CKBR_PARAMETERERROR;

    ExtraState status;
    beh->GetLocalParameterValue(STATUS, &status);

    SmallBall *smallBalls = NULL;
    beh->GetLocalParameterValue(SMALLBALL_LIST, &smallBalls);

    if (status == EXTRA_NONE)
    {
        if (smallBalls)
        {
            smallBalls = NULL;
            beh->SetLocalParameterValue(SMALLBALL_LIST, &smallBalls, sizeof(SmallBall *));
        }
    }
    else if (status == EXTRA_ACTIVATED)
    {
        if (ResetExtra(behcontext) != CKBR_OK)
            return CKBR_PARAMETERERROR;
    }
    else if (smallBalls)
    {
        return CKBR_OK;
    }

    int smallBallCount;
    beh->GetInputParameterValue(NUM_SMALLBALLS, &smallBallCount);
    if (smallBallCount <= 0)
        smallBallCount = 1;
    if (smallBallCount > kMaxSmallBallCount)
        smallBallCount = kMaxSmallBallCount;

    beh->SetLocalParameterValue(BALL_COUNTER, &smallBallCount);

    CK3dEntity *target = (CK3dEntity *)beh->GetTarget();
    if (!target)
        return CKBR_OWNERERROR;

    CKBOOL flyingExtra;
    beh->GetInputParameterValue(FLYING_EXTRA, &flyingExtra);

    CK3dEntity *floor = GetChildOfTarget(14, "Floor", target);
    CK3dEntity *ball0 = GetChildOfTarget(14, "Ball0", target);

    if (!flyingExtra && !floor)
    {
        context->OutputToConsole("There is no Floorglowobject in hirachy!", FALSE);
        if (!ball0)
            context->OutputToConsole("There is no Centerbillboard in hirachy!", FALSE);
        return CKBR_PARAMETERERROR;
    }

    if (!ball0)
    {
        if (!floor)
            context->OutputToConsole("There is no Floorglowobject in hirachy!", FALSE);
        context->OutputToConsole("There is no Centerbillboard in hirachy!", FALSE);
        return CKBR_PARAMETERERROR;
    }

    beh->SetLocalParameterObject(SHADOW_OBJECT, floor);
    beh->SetLocalParameterObject(CENTER_OBJECT, ball0);

    if (floor)
    {
        scene->DeActivate(floor);
        floor->Show(CKHIDE);
    }

    CK3dEntity *child = ball0->GetChild(0);
    if (!child)
    {
        context->OutputToConsole("There is no Frame at Centerbillboard.", FALSE);
        return CKBR_PARAMETERERROR;
    }
    CKBehavior *script = child->GetScript(0);
    if (!script)
    {
        context->OutputToConsole("There is no Script at Centerbillboard frame.", FALSE);
        return CKBR_PARAMETERERROR;
    }
    scene->DeActivate(script);
    ball0->Show(CKHIDE);

    float force;
    float forceWidth;
    beh->GetInputParameterValue(FORCE, &force);
    beh->GetInputParameterValue(FORCE_WIDTH, &forceWidth);

    forceWidth /= (float)smallBallCount;
    force -= forceWidth;

    SmallBall *prev = NULL;
    for (int i = 0; i < smallBallCount; ++i)
    {
        CK3dEntity *ball = GetChildOfTarget(18, indices[i], target);
        if (!ball)
        {
            context->OutputToConsole("Missing one or more Billboards.", FALSE);
            return CKBR_PARAMETERERROR;
        }

        child = ball->GetChild(0);
        if (!child)
        {
            context->OutputToConsole("There is no Frame at one of the Billboards.", FALSE);
            return CKBR_PARAMETERERROR;
        }

        script = child->GetScript(0);
        if (!script)
        {
            context->OutputToConsole("There is no Script at one of the Billboardframes.", FALSE);
            return CKBR_PARAMETERERROR;
        }

        scene->DeActivate(script);
        ball->Show(CKHIDE);

        VxVector pos;
        ball->GetPosition(&pos);

        force += forceWidth;
        SmallBall *current = new SmallBall(NULL, ball, pos, force, true);
        if (i == 0)
            smallBalls = current;

        script->Activate(FALSE, FALSE);

        if (prev)
            prev->next = current;
        prev = current;
    }

    beh->SetLocalParameterValue(SMALLBALL_LIST, &smallBalls, sizeof(SmallBall *));

    float flyawayTime;
    beh->GetInputParameterValue(FLYAWAY_TIME, &flyawayTime);
    beh->SetLocalParameterValue(TIME_COUNTER, &flyawayTime);

    CKBOOL initialized = TRUE;
    beh->SetLocalParameterValue(INITIALIZED, &initialized);

    status = EXTRA_IDLE;
    beh->SetLocalParameterValue(STATUS, &status);

    return CKBR_OK;
}

int ActivateExtra(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    SmallBall *smallBalls = NULL;
    beh->GetLocalParameterValue(SMALLBALL_LIST, &smallBalls);
    if (!smallBalls)
        return CKBR_PARAMETERERROR;

    CKScene *scene = context->GetCurrentScene();
    if (!scene)
        return CKBR_PARAMETERERROR;

    CKBOOL flyingExtra;
    beh->GetInputParameterValue(FLYING_EXTRA, &flyingExtra);
    CK3dEntity *shadowObject = (CK3dEntity *)beh->GetLocalParameterObject(SHADOW_OBJECT);
    CK3dEntity *centerObject = (CK3dEntity *)beh->GetLocalParameterObject(CENTER_OBJECT);

    if (centerObject)
        centerObject->Show();

    if (shadowObject && !flyingExtra)
        shadowObject->Show();

    for (SmallBall *node = smallBalls; node != NULL; node = node->next)
    {
        CK3dEntity *child = node->ball->GetChild(0);
        if (child)
        {
            CKBehavior *script = child->GetScript(0);
            if (script)
                scene->Activate(script, TRUE);
            else
                context->OutputToConsoleEx("8Script not found %s", child->GetName());
        }

        node->ball->Show();

        child = node->ball->GetChild(0);
        if (child)
        {
            CKBehavior *script = child->GetScript(0);
            if (script)
                scene->Activate(script, TRUE);
            else
                context->OutputToConsoleEx("8Script not found %s", child->GetName());
        }
    }

    return CKBR_OK;
}

float CalcDistanceSquare(CK3dEntity *target, VxVector pos)
{
    if (!target)
        return -1.0f;

    VxVector targetPos;
    target->GetPosition(&targetPos);
    return SquareMagnitude(pos - targetPos);
}

int ExecuteExtraOnIdle(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    CK3dEntity *target = (CK3dEntity *)beh->GetTarget();
    if (!target)
        return CKBR_OWNERERROR;

    CK3dEntity *ball = (CK3dEntity *)beh->GetInputParameterObject(BALL);
    if (!ball)
        return CKBR_PARAMETERERROR;

    VxVector ballPos;
    ball->GetPosition(&ballPos);

    float activationDistance;
    beh->GetInputParameterValue(ACTIVATION_DISTANCE, &activationDistance);
    activationDistance = activationDistance * activationDistance;

    bool withinActivation;
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
    VxVector targetPos;
    target->GetPosition(&targetPos);
    if (!VxTryLegacyExtraWithinDistance(&ballPos.x,&targetPos.x,activationDistance,&withinActivation))
#endif
        withinActivation = !(CalcDistanceSquare(target, ballPos) > activationDistance);
    if (!withinActivation)
    {
        float rotationSpeed;
        beh->GetInputParameterValue(ROTATION_SPEED, &rotationSpeed);

        float timeValue;
        beh->GetLocalParameterValue(TIME_VALUE, &timeValue);
        rotationSpeed *= timeValue;

        SmallBall *smallBalls = NULL;
        beh->GetLocalParameterValue(SMALLBALL_LIST, &smallBalls);
        if (!smallBalls)
            return CKBR_PARAMETERERROR;

        VxVector pos;
        VxVector vec;
        VxVector newPos;
        VxMatrix mat;

        int i = 0;
        for (SmallBall *node = smallBalls; node != NULL; node = node->next)
        {
            vec.Set(0.0f, 1.0f, 0.0f);
            switch (i++)
            {
            case 0:
                vec.Set(0.0f, 1.0f, 0.0f);
                break;
            case 1:
                vec.Set(0.0f, 0.0f, 1.0f);
                break;
            case 2:
                vec.Set(1.0f, 0.0f, 0.0f);
                break;
            case 3:
                vec.Set(0.5f, 0.5f, 0.707f);
                break;
            case 4:
                vec.Set(0.707f, -0.707f, 0.0f);
                break;
            case 5:
                vec.Set(-0.5f, -0.5f, 0.707f);
                break;
            default:
                break;
            }

            node->ball->GetPosition(&pos, target);
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
            if (!VxTryLegacyExtraRotation(&mat[0][0],&vec.x,rotationSpeed))
#endif
                Vx3DMatrixFromRotation(mat, vec, rotationSpeed);
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
            if (!VxTryLegacyExtraRotateVector(&newPos.x,&mat[0][0],&pos.x))
#endif
                Vx3DRotateVector(&newPos, mat, &pos);
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
            if (!VxTryLegacyExtraNormalize(&newPos.x))
#endif
            {
                float m = newPos.Magnitude();
                if (m > 0.0f && m != 2.0f)
                    newPos *= (1.0f / m) * 2;
            }
            node->ball->SetPosition(&newPos, target);
        }
    }
    else
    {
        ExtraState status = EXTRA_ACTIVATED;
        beh->SetLocalParameterValue(STATUS, &status);

        CKBOOL flyingExtra;
        beh->GetInputParameterValue(FLYING_EXTRA, &flyingExtra);

        CK3dEntity *shadowObject = (CK3dEntity *)beh->GetLocalParameterObject(SHADOW_OBJECT);
        CK3dEntity *centerObject = (CK3dEntity *)beh->GetLocalParameterObject(CENTER_OBJECT);

        CKScene *scene = context->GetCurrentScene();
        if (!scene)
            return CKBR_PARAMETERERROR;

        if (shadowObject && !flyingExtra)
            shadowObject->Show(CKHIDE);

        if (centerObject)
        {
            CK3dEntity *child = centerObject->GetChild(0);
            CKBehavior *script = NULL;
            if (child)
            {
                script = child->GetScript(0);
                if (script)
                    scene->DeActivate(script);
                else
                    context->OutputToConsoleEx("14Script not found %s", child->GetName());
            }
            centerObject->Show(CKHIDE);

            child = centerObject->GetChild(0);
            if (child)
            {
                script = child->GetScript(0);
                child->Show();
                if (script)
                    scene->Activate(script, TRUE);
                else
                    context->OutputToConsoleEx("15Script not found %s", child->GetName());
            }
        }

        SmallBall *smallBalls = NULL;
        beh->GetLocalParameterValue(SMALLBALL_LIST, &smallBalls);
        if (!smallBalls)
            return CKBR_PARAMETERERROR;

        for (SmallBall *node = smallBalls; node != NULL; node = node->next)
            node->ball->GetPosition(&node->position);

        CKBOOL activated = TRUE;
        beh->SetOutputParameterValue(0, &activated);
        beh->ActivateOutput(ACTIVATED);
        ballPos.y -= 2.1f;
        beh->SetLocalParameterValue(AWAY_POSITION_SAVE, &ballPos);
    }

    return CKBR_OK;
}

int ExecuteExtraOnActivated(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;

    VxVector awayPos;
    beh->GetLocalParameterValue(AWAY_POSITION_SAVE, &awayPos);
    awayPos.y -= 3.0f;

    float timeCount;
    beh->GetLocalParameterValue(TIME_COUNTER, &timeCount);
    timeCount -= behcontext.DeltaTime;

    if (timeCount <= 0.0f)
    {
        ExtraState status = EXTRA_HIT;
        beh->SetLocalParameterValue(STATUS, &status);
        return CKBR_OK;
    }

    beh->SetLocalParameterValue(TIME_COUNTER, &timeCount);

    float awayForce;
    beh->GetInputParameterValue(AWAY_FORCE, &awayForce);

    float awayDamping;
    beh->GetInputParameterValue(AWAY_DAMPING, &awayDamping);

    float timeValue;
    beh->GetLocalParameterValue(TIME_VALUE, &timeValue);
    awayForce *= timeValue;

    SmallBall *smallBalls = NULL;
    beh->GetLocalParameterValue(SMALLBALL_LIST, &smallBalls);
    if (!smallBalls)
        return CKBR_PARAMETERERROR;

    VxVector pos;
    for (SmallBall *node = smallBalls; node != NULL; node = node->next)
    {
        VxVector oldPos = node->position;

        node->ball->GetPosition(&pos);
        node->position = pos;

        VxVector newPos;
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
        // awayForce has already passed through the original binary32 store.
        if (!VxTryLegacyExtraMove(&newPos.x,&pos.x,&oldPos.x,&awayPos.x,
                                 awayDamping,awayForce,timeValue,1))
#endif
            newPos = pos + (pos - oldPos) * awayDamping + (pos - awayPos) * awayForce;
        node->ball->SetPosition(&newPos);
    }

    return CKBR_OK;
}

int ExecuteExtraOnHit(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    int ballCount;
    beh->GetLocalParameterValue(BALL_COUNTER, &ballCount);
    if (ballCount <= 0)
    {
        ExtraState status = EXTRA_END;
        beh->SetLocalParameterValue(STATUS, &status);
        beh->ActivateOutput(READY);
        return CKBR_OK;
    }

    CK3dEntity *ball = (CK3dEntity *)beh->GetInputParameterObject(BALL);
    if (!ball)
        return CKBR_PARAMETERERROR;

    VxVector ballPos;
    ball->GetPosition(&ballPos);

    float damping;
    beh->GetInputParameterValue(DAMPING, &damping);

    SmallBall *smallBalls = NULL;
    beh->GetLocalParameterValue(SMALLBALL_LIST, &smallBalls);
    if (!smallBalls)
        return CKBR_PARAMETERERROR;

    CKScene *scene = context->GetCurrentScene();
    if (!scene)
        return CKBR_PARAMETERERROR;

    float timeValue;
    beh->GetLocalParameterValue(TIME_VALUE, &timeValue);

    VxVector pos;
    int activatedBallCount = 0;
    int i = 0;
    for (SmallBall *node = smallBalls; node != NULL; node = node->next, ++i)
    {
        if (!node->flag)
            continue;

        node->ball->GetPosition(&pos);

        float collDistance = 2.0f;
        beh->GetInputParameterValue(EXTRA_POINTS_COLLDISTANCE, &collDistance);
        bool withinCollision;
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
        if (!VxTryLegacyExtraWithinDistance(&ballPos.x,&pos.x,collDistance,&withinCollision))
#endif
            withinCollision = SquareMagnitude(ballPos - pos) <= collDistance;
        if (withinCollision)
        {
            ++activatedBallCount;
            node->flag = false;
            --ballCount;
            beh->SetLocalParameterValue(BALL_COUNTER, &ballCount);
            CK3dEntity *child = node->ball->GetChild(0);
            if (child)
            {
                CKBehavior *script = child->GetScript(0);
                if (script)
                    scene->DeActivate(script);
                else
                    context->OutputToConsoleEx("17Script not found %s", child->GetName());
            }

            child = node->ball->GetChild(0);
            if (child)
            {
                CKBehavior *script = child->GetScript(0);
                if (script)
                    scene->DeActivate(script);
                else
                    context->OutputToConsoleEx("18Script not found %s", child->GetName());
            }

            node->ball->Show(CKHIDE);

            CKGroup *hitFrameGroup = (CKGroup *)beh->GetInputParameterObject(HIT_FRAME_GROUP);
            if (hitFrameGroup)
            {
                CK3dEntity *hitFrame = (CK3dEntity *)hitFrameGroup->GetObject(i);
                if (hitFrame)
                {
                    VxVector position;
                    node->ball->GetPosition(&position);
                    hitFrame->SetPosition(&position);

                    CKBehavior *script = hitFrame->GetScript(0);
                    if (script)
                        scene->Activate(script, TRUE);
                    else
                        context->OutputToConsoleEx("19Script not found %s", hitFrame->GetName());
                }
            }
        }
        else
        {
            VxVector oldPos = node->position;
            node->position = pos;

            VxVector newPos;
#ifdef CKBB_HAVE_MATRIX_ARITHMETIC_BACKEND
            if (!VxTryLegacyExtraMove(&newPos.x,&pos.x,&oldPos.x,&ballPos.x,
                                     damping,node->force,timeValue,0))
#endif
                newPos = pos + (pos - oldPos) * damping + (ballPos - pos) * node->force * timeValue;
            node->ball->SetPosition(&newPos);
        }
    }

    if (activatedBallCount > 0)
    {
        beh->SetOutputParameterValue(0, &activatedBallCount);
        beh->ActivateOutput(HIT);
    }

    return CKBR_OK;
}

int ExecuteExtra(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;

    ExtraState status;
    beh->GetLocalParameterValue(STATUS, &status);

    int ret = CKBR_OK;
    switch (status)
    {
    case EXTRA_IDLE:
        ret = ExecuteExtraOnIdle(behcontext);
        break;
    case EXTRA_ACTIVATED:
        ret = ExecuteExtraOnActivated(behcontext);
        break;
    case EXTRA_HIT:
        ret = ExecuteExtraOnHit(behcontext);
        break;
    default:
        break;
    }

    if (ret != CKBR_OK)
        return CKBR_PARAMETERERROR;
    return ret;
}

int DeactivateExtra(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    SmallBall *smallBalls = NULL;
    beh->GetLocalParameterValue(SMALLBALL_LIST, &smallBalls);
    if (!smallBalls)
        return CKBR_PARAMETERERROR;

    CKScene *scene = context->GetCurrentScene();
    if (!scene)
        return CKBR_PARAMETERERROR;

    CKBOOL flyingExtra;
    beh->GetInputParameterValue(FLYING_EXTRA, &flyingExtra);
    CK3dEntity *shadowObject = (CK3dEntity *)beh->GetLocalParameterObject(SHADOW_OBJECT);
    CK3dEntity *centerObject = (CK3dEntity *)beh->GetLocalParameterObject(CENTER_OBJECT);

    if (centerObject)
    {
        CK3dEntity *child = centerObject->GetChild(0);
        if (child)
        {
            CKBehavior *script = child->GetScript(0);
            if (script)
                scene->DeActivate(script);
            else
                context->OutputToConsoleEx("9Script not found %s", child->GetName());
        }
        centerObject->Show(CKHIDE);
    }

    if (shadowObject && !flyingExtra)
        shadowObject->Show(CKHIDE);

    for (SmallBall *node = smallBalls; node != NULL; node = node->next)
    {
        CK3dEntity *child = node->ball->GetChild(0);
        if (child)
        {
            CKBehavior *script = child->GetScript(0);
            if (script)
                scene->DeActivate(script);
            else
                context->OutputToConsoleEx("11Script not found %s", child->GetName());
        }

        node->ball->Show(CKHIDE);

        child = node->ball->GetChild(0);
        if (child)
        {
            CKBehavior *script = child->GetScript(0);
            if (script)
                scene->DeActivate(script);
            else
                context->OutputToConsoleEx("12Script not found %s", child->GetName());
        }

        if (child)
        {
            CKBehavior *script = child->GetScript(0);
            if (script)
                scene->DeActivate(script);
            else
                context->OutputToConsoleEx("13Script not found %s", child->GetName());
        }
    }

    return CKBR_OK;
}

int Extra(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;
    CKContext *context = behcontext.Context;

    CKBOOL activationStatus = FALSE;
    if (beh->IsInputActive(OFF))
    {
        beh->ActivateInput(OFF, FALSE);
        beh->SetLocalParameterValue(ACTIVATION_STATUS, &activationStatus);
        DeactivateExtra(behcontext);
        return CKBR_OK;
    }

    CKBOOL initialized;
    beh->GetLocalParameterValue(INITIALIZED, &initialized);

    CKBOOL exactness;
    int exactnessFrameDelay;
    float timeValue;

    if (beh->IsInputActive(ON))
    {
        beh->ActivateInput(ON, FALSE);
        if (!initialized)
        {
            context->OutputToConsole("Extra isn't initialized yet, this could slow all down.", FALSE);
            if (InitializeExtra(behcontext) != CKBR_OK)
            {
                context->OutputToConsole("Couldn't initialize the Extra.");
                return CKBR_PARAMETERERROR;
            }
            initialized = TRUE;
        }

        activationStatus = TRUE;
        beh->SetLocalParameterValue(ACTIVATION_STATUS, &activationStatus);

        timeValue = behcontext.DeltaTime * 0.001f;
        beh->SetLocalParameterValue(TIME_VALUE, &timeValue);

        beh->GetLocalParameterValue(EXACTNESS, &exactness);
        if (exactness)
        {
            beh->GetInputParameterValue(EXACTNESS_FRAME_DELAY, &exactnessFrameDelay);
            beh->SetLocalParameterValue(NEXT_CHECK, &exactnessFrameDelay);
        }

        if (ActivateExtra(behcontext) != CKBR_OK)
        {
            context->OutputToConsole("Couldn't activate the Extra.");
            return CKBR_PARAMETERERROR;
        }
    }
    else
    {
        if (beh->IsInputActive(INITIALIZE))
        {
            beh->ActivateInput(INITIALIZE, FALSE);
            if (!initialized)
            {
                if (InitializeExtra(behcontext) != CKBR_OK)
                {
                    context->OutputToConsole("Couldn't initialize the Extra.");
                    return CKBR_PARAMETERERROR;
                }
                return CKBR_OK;
            }
        }
        else
        {
            beh->GetLocalParameterValue(ACTIVATION_STATUS, &activationStatus);
        }

        if (!activationStatus || !initialized)
            return CKBR_ACTIVATENEXTFRAME;
    }

    beh->GetLocalParameterValue(EXACTNESS, &exactness);
    if (exactness)
    {
        int nextCheck = 0;
        beh->GetLocalParameterValue(NEXT_CHECK, &nextCheck);
        --nextCheck;
        if (nextCheck == 0)
        {
            timeValue = behcontext.DeltaTime * 0.001f;
            beh->SetLocalParameterValue(TIME_VALUE, &timeValue);
            beh->GetInputParameterValue(EXACTNESS_FRAME_DELAY, &nextCheck);
        }
        beh->SetLocalParameterValue(NEXT_CHECK, &nextCheck);
    }

    if (ExecuteExtra(behcontext) != CKBR_OK)
    {
        context->OutputToConsole("Couldn't execute the Extra.");
        return CKBR_PARAMETERERROR;
    }

    return CKBR_ACTIVATENEXTFRAME;
}

CKERROR ExtraCallBack(const CKBehaviorContext &behcontext)
{
    CKBehavior *beh = behcontext.Behavior;

    switch (behcontext.CallbackMessage)
    {
    case CKM_BEHAVIORDELETE:
    case CKM_BEHAVIORDETACH:
    case CKM_BEHAVIORRESET:
    case CKM_BEHAVIORDEACTIVATESCRIPT:
    {
        CKBOOL initialized;
        beh->GetLocalParameterValue(INITIALIZED, &initialized);
        if (initialized)
            ResetExtra(behcontext);
    }
    break;
    case CKM_BEHAVIORATTACH:
    case CKM_BEHAVIORLOAD:
    {
        SmallBall *smallBalls = NULL;
        beh->SetLocalParameterValue(SMALLBALL_LIST, &smallBalls, sizeof(SmallBall *));

        CKBOOL initialized = FALSE;
        beh->SetLocalParameterValue(INITIALIZED, &initialized);

        ExtraState status = EXTRA_NONE;
        beh->SetLocalParameterValue(STATUS, &status);

        CKBOOL activationStatus = FALSE;
        beh->SetLocalParameterValue(ACTIVATION_STATUS, &activationStatus);
    }
    break;
    case CKM_BEHAVIORSETTINGSEDITED:
    {
        CKBOOL exactness;
        beh->GetLocalParameterValue(EXACTNESS, &exactness);
        int inParamCount = beh->GetInputParameterCount();
        if (exactness)
        {
            if (inParamCount < 14)
                beh->CreateInputParameter("Exactness Framedelay", CKPGUID_INT);
        }
        else if (inParamCount == 14)
        {
            CKParameterIn *param = beh->RemoveInputParameter(13);
            CKDestroyObject(param);
        }
    }
    break;
    default:
        break;
    }

    return CKBR_OK;
}

int TT_ExtraCaptureSnapshot(CKContext *context, TT_ExtraSnapshotHandle *snapshot)
{
    if (!context || !snapshot)
        return CKERR_INVALIDPARAMETER;
    *snapshot = NULL;
    try
    {
        std::unique_ptr<ExtraSnapshot> result(new ExtraSnapshot());
        const XObjectPointerArray &behaviors =
            context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
        for (XObjectPointerArray::ConstIterator it = behaviors.Begin();
             it != behaviors.End(); ++it)
        {
            CKBehavior *behavior = static_cast<CKBehavior *>(*it);
            if (!behavior || behavior->GetPrototypeGuid() !=
                                 CKGUID(0x36106bd9, 0x51813906))
                continue;
            SavedExtra state;
            state.behaviorId = behavior->GetID();
            state.identity = behavior;
            SmallBall *list = NULL;
            behavior->GetLocalParameterValue(SMALLBALL_LIST, &list);
            for (SmallBall *ball = list; ball; ball = ball->next)
            {
                SavedSmallBall saved;
                saved.ballId = ball->ball ? ball->ball->GetID() : 0;
                saved.identity = ball->ball;
                saved.position = ball->position;
                saved.force = ball->force;
                saved.flag = ball->flag;
                state.balls.push_back(saved);
            }
            result->extras.push_back(state);
        }
        *snapshot = result.release();
        return CK_OK;
    }
    catch (...)
    {
        return CKERR_OUTOFMEMORY;
    }
}

namespace {
using SmallBallList = std::unique_ptr<SmallBall, decltype(&DeleteSmallBallList)>;

struct PreparedExtra {
    SavedExtra saved;
    CKParameterLocal *parameter = nullptr;
    SmallBallList head{nullptr, &DeleteSmallBallList};
};

struct PreparedExtraRestore {
    CKContext *context = nullptr;
    bool committed = false;
    std::vector<std::unique_ptr<PreparedExtra>> extras;
};

CKParameterLocal *ValidateExtraOwner(CKContext *context, const SavedExtra &saved)
{
    CKBehavior *behavior = CKBehavior::Cast(context->GetObject(saved.behaviorId));
    if (!behavior || behavior != saved.identity || behavior->IsToBeDeleted() ||
        behavior->GetPrototypeGuid() != CKGUID(0x36106bd9, 0x51813906))
        return nullptr;
    CKParameterLocal *parameter = behavior->GetLocalParameter(SMALLBALL_LIST);
    if (!parameter || parameter->IsToBeDeleted() ||
        parameter->GetGUID() != CKPGUID_POINTER ||
        parameter->GetDataSize() != sizeof(SmallBall *) ||
        !parameter->GetWriteDataPtr())
        return nullptr;
    for (const SavedSmallBall &ball : saved.balls) {
        CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(ball.ballId));
        if (!entity || entity != ball.identity || entity->IsToBeDeleted())
            return nullptr;
    }
    return parameter;
}
}

int TT_ExtraPrepareRestore(CKContext *context, TT_ExtraSnapshotHandle snapshot,
                          TT_ExtraRestoreHandle *outPrepared)
{
    if (!outPrepared)
        return CKERR_INVALIDPARAMETER;
    *outPrepared = nullptr;
    ExtraSnapshot *saved = static_cast<ExtraSnapshot *>(snapshot);
    if (!context || !saved)
        return CKERR_INVALIDPARAMETER;
    try {
        std::unique_ptr<PreparedExtraRestore> result(new PreparedExtraRestore());
        result->context = context;
        result->extras.reserve(saved->extras.size());
        for (const SavedExtra &state : saved->extras) {
            CKParameterLocal *parameter = ValidateExtraOwner(context, state);
            if (!parameter)
                return CKERR_INVALIDPARAMETER;
            std::unique_ptr<PreparedExtra> prepared(new PreparedExtra());
            prepared->saved = state;
            prepared->parameter = parameter;
            SmallBall *tail = nullptr;
            for (const SavedSmallBall &ball : state.balls) {
                SmallBall *node = new SmallBall(
                    nullptr, ball.identity, ball.position, ball.force, ball.flag);
                if (tail)
                    tail->next = node;
                else
                    prepared->head.reset(node);
                tail = node;
            }
            result->extras.push_back(std::move(prepared));
        }
        *outPrepared = result.release();
        return CK_OK;
    } catch (...) {
        return CKERR_OUTOFMEMORY;
    }
}

int TT_ExtraCommitRestore(CKContext *context, TT_ExtraRestoreHandle opaque)
{
    auto *prepared = static_cast<PreparedExtraRestore *>(opaque);
    if (!context || !prepared || context != prepared->context || prepared->committed)
        return CKERR_INVALIDPARAMETER;
    // Check the entire set before publishing any list. CK changes between
    // prepare and commit must not turn a stale owner into a partial success.
    for (const auto &extra : prepared->extras) {
        if (ValidateExtraOwner(context, extra->saved) != extra->parameter)
            return CKERR_INVALIDPARAMETER;
    }
    for (const auto &extra : prepared->extras) {
        // Validation established a pointer-sized existing buffer. Publishing
        // through it cannot allocate or invoke behavior callbacks. Only after
        // publication may the old nodes be freed; CK entities remain owned by CK.
        void *slot = extra->parameter->GetWriteDataPtr();
        SmallBall *old = nullptr;
        std::memcpy(&old, slot, sizeof(old));
        SmallBall *replacement = extra->head.release();
        std::memcpy(slot, &replacement, sizeof(replacement));
        DeleteSmallBallList(old);
    }
    prepared->committed = true;
    return CK_OK;
}

void TT_ExtraDestroyRestore(TT_ExtraRestoreHandle prepared)
{
    delete static_cast<PreparedExtraRestore *>(prepared);
}

int TT_ExtraRestoreSnapshot(CKContext *context, TT_ExtraSnapshotHandle snapshot)
{
    TT_ExtraRestoreHandle prepared = nullptr;
    const int result = TT_ExtraPrepareRestore(context, snapshot, &prepared);
    if (result != CK_OK)
        return result;
    std::unique_ptr<void, decltype(&TT_ExtraDestroyRestore)> owner(
        prepared, &TT_ExtraDestroyRestore);
    return TT_ExtraCommitRestore(context, prepared);
}

void TT_ExtraDestroySnapshot(TT_ExtraSnapshotHandle snapshot)
{
    delete static_cast<ExtraSnapshot *>(snapshot);
}
