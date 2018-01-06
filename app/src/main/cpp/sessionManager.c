#include <jni.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ela_carrier.h>
#include <ela_session.h>

#include "log.h"
#include "utils.h"
#include "carrierCookie.h"
#include "sessionUtils.h"

typedef struct CallbackContext {
    JNIEnv* env;
    jclass  clazz;
    jobject carrier;
    jobject handler;
} CallbackContext;

static CallbackContext callbackContext;

static
void onSessionRequestCallback(ElaCarrier* carrier, const char* from, const char* sdp, size_t len,
                              void* context)
{
    assert(carrier);
    assert(from);
    assert(sdp);

    (void)carrier;
    (void)len;

    CallbackContext* cc = (CallbackContext*)context;

    int needDetach = 0;
    JNIEnv* env = attachJvm(&needDetach);
    if (!env) {
        logE("Attach JVM error");
        return;
    }

    jstring jfrom = (*env)->NewStringUTF(env, from);
    if (!jfrom) {
        detachJvm(env, needDetach);
        return;
    }

    jstring jsdp = (*env)->NewStringUTF(env, sdp);
    if (!jsdp) {
        (*env)->DeleteLocalRef(env, jfrom);
        detachJvm(env, needDetach);
        return;
    }

    if (!callVoidMethod(env, cc->clazz, cc->handler, "onSessionRequest",
                        "("_W("Carrier;")_J("String;")_J("String;)V"),
                        cc->carrier, jfrom, jsdp)) {
        logE("Can not call method:\n\tvoid onSessionRequest(Carrier, String, String)");
    }

    (*env)->DeleteLocalRef(env, jsdp);
    (*env)->DeleteLocalRef(env, jfrom);

    detachJvm(env, needDetach);
}

static
bool callbackCtxtSet(CallbackContext* hc, JNIEnv* env, jobject jcarrier, jobject jhandler)
{

    jclass lclazz = (*env)->GetObjectClass(env, jhandler);
    if (!lclazz) {
        setErrorCode(ELA_GENERAL_ERROR(ELAERR_LANGUAGE_BINDING));
        return false;
    }

    jclass  gclazz = NULL;
    jobject gjcarrier = NULL;
    jobject gjhandler = NULL;

    gclazz    = (*env)->NewGlobalRef(env, lclazz);
    gjcarrier = (*env)->NewGlobalRef(env, jcarrier);
    gjhandler = (*env)->NewGlobalRef(env, jhandler);

    if (!gclazz || !gjcarrier || !gjhandler) {
        setErrorCode(ELA_GENERAL_ERROR(ELAERR_OUT_OF_MEMORY));
        goto errorExit;
    }

    hc->env     = NULL;
    hc->clazz   = gclazz;
    hc->carrier = gjcarrier;
    hc->handler = gjhandler;
    return true;

errorExit:
    if (gjhandler) (*env)->DeleteGlobalRef(env, gjhandler);
    if (gjcarrier) (*env)->DeleteGlobalRef(env, gjcarrier);
    if (gclazz)    (*env)->DeleteGlobalRef(env, gclazz);

    return false;
}

static
void callbackCtxtCleanup(CallbackContext* cc, JNIEnv* env)
{
    assert(cc);

    if (cc->clazz)
        (*env)->DeleteGlobalRef(env, cc->clazz);
    if (cc->carrier)
        (*env)->DeleteGlobalRef(env, cc->carrier);
    if (cc->handler)
        (*env)->DeleteGlobalRef(env, cc->handler);
}

static
jboolean sessionMgrInit(JNIEnv* env, jclass clazz, jobject jcarrier, jobject jhandler)
{
    assert(jcarrier);

    (void)clazz;

    memset(&callbackContext, 0, sizeof(callbackContext));

    CallbackContext *hc = NULL;
    if (jhandler) {
        hc = (CallbackContext*)&callbackContext;

        if (!callbackCtxtSet(hc, env, jcarrier, jhandler)) {
            setErrorCode(ELA_GENERAL_ERROR(ELAERR_LANGUAGE_BINDING));
            return JNI_FALSE;
        }
    }

    int result = ela_session_init(getCarrier(env, jcarrier), onSessionRequestCallback, hc);
    if (result < 0) {
        logE("Call ela_session_init API error");
        setErrorCode(ela_get_error());
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

static
void sessionMgrCleanup(JNIEnv* env, jclass clazz, jobject jcarrier)
{
    assert(jcarrier);

    (void)clazz;

    callbackCtxtCleanup(&callbackContext, env);
    ela_session_cleanup(getCarrier(env, jcarrier));
}

static
jobject createSession(JNIEnv* env, jobject thiz, jobject jcarrier, jstring jto, jobject jtype)
{
    assert(jcarrier);
    assert(jto);

    (void)thiz;

    const char* to = (*env)->GetStringUTFChars(env, jto, NULL);
    if (!to) {
        setErrorCode(ELA_GENERAL_ERROR(ELAERR_LANGUAGE_BINDING));
        return NULL;
    }

    ElaSession* session = ela_session_new(getCarrier(env, jcarrier), to);
    (*env)->ReleaseStringUTFChars(env, jto, to);
    if (!session) {
        logE("Call ela_session_new API error");
        setErrorCode(ela_get_error());
        return NULL;
    }

    jobject jsession = NULL;
    if (!newJavaSession(env, session, jto, &jsession)) {
        ela_session_close(session);
        setErrorCode(ELA_GENERAL_ERROR(ELAERR_LANGUAGE_BINDING));
        return NULL;
    }

    return jsession;
}

static
jint getErrorCode(JNIEnv* env, jclass clazz)
{
    (void)env;
    (void)clazz;

    return _getErrorCode();
}

static const char* gClassName = "org/elastos/carrier/session/Manager";
static JNINativeMethod gMethods[] = {
        {"native_init",      "("_W("Carrier;")_S("ManagerHandler;)Z"),  (void*)sessionMgrInit   },
        {"native_cleanup",   "("_W("Carrier;)V"),                       (void*)sessionMgrCleanup},
        {"create_session",   "("_W("Carrier;")_J("String;")_S("Session;"),
                                                                        (void*)createSession    },
        {"get_error_code",   "()I",                                     (void*)getErrorCode     },
};

int registerCarrierSessionManagerMethods(JNIEnv* env)
{
    return registerNativeMethods(env, gClassName,
                                 gMethods,
                                 sizeof(gMethods) / sizeof(gMethods[0]));
}

void unregisterCarrierSessionManagerMethods(JNIEnv* env)
{
    jclass clazz = (*env)->FindClass(env, gClassName);
    if (clazz)
        (*env)->UnregisterNatives(env, clazz);
}