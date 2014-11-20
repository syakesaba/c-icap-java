/**
 * @file c-icap-java.c
 * @brief c-icap module for java handler
 *
 * @author SYA-KE
 * @date 2014-10-01
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

//---beware JNI Version
#include "jni.h"

#include "c_icap/c-icap.h"
#include "c_icap/service.h"
#include "c_icap/module.h"
#include "c_icap/header.h"
#include "c_icap/body.h"
#include "c_icap/simple_api.h"
#include "c_icap/debug.h"

#define CIJ_ERROR_LEVEL 1
#define CIJ_WARN_LEVEL 3
#define CIJ_MESSAGE_LEVEL 5
#define CIJ_INFO_LEVEL 7
#define CIJ_DEBUG_LEVEL CI_DEBUG_LEVEL

//---DEBUG util
#if 1
    #define cij_debug_printf(lev,msg, ...) ci_debug_printf(1, "(%d):%s():L%04d: "msg"\n", getpid(), __func__, __LINE__, ## __VA_ARGS__)
#else
    #define cij_debug_printf(lev,msg, ...) ci_debug_printf(lev, "CIJ:"msg"\n", ## __VA_ARGS__)
#endif

#define MAX_JVM_OPTIONS 4
#define MAX_CLASS_NAME 1024

/**
 * one ICAP service handles one JVIEnv (Java Process).
 * S(mod_type,headers)
 * S.preview(byte[])
 * S.service(byte[])
 */
typedef struct jDataStruct {
    JNIEnv * jni;
    JavaVM * jvm;
    jclass jIcapClass;
    char * name;
    jmethodID jServiceConstructor;//Constructor
    jmethodID jPreview;//preview()
    jmethodID jService;//service()
} jData_t;

typedef struct jServiceDataStruct {
    jData_t * jdata;//includes JVM
    jobject instance;
    ci_membuf_t * buffer;
} jServiceData_t;

int init_java_handler(struct ci_server_conf * server_conf);
int post_init_java_handler(struct ci_server_conf * server_conf);
ci_service_module_t * load_java_module(char * service_file);
void release_java_handler();

int java_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf);
int java_post_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf);
void java_close_service();
void * java_init_request_data(ci_request_t * req);
void java_release_request_data(void * data);
int java_check_preview_handler(char * preview_data, int preview_data_len, ci_request_t * req);
int java_end_of_data_handler(ci_request_t * req);
int java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req);

/**
 * Declare c-icap module handler.<br>
 * <br>
 * TO LOAD: <b>Module service_handler srv_java.so</b>
 */
CI_DECLARE_DATA service_handler_module_t module = {
    "java_handler",
    ".class",//the target is a compiled java class file.
    init_java_handler,
    post_init_java_handler,
    release_java_handler,
    load_java_module,
    NULL // TODO: conf-table
};

ci_dyn_array_t * enviroments; //JVM environments
#define MAX_ENVIRONMENTS_SIZE 256
#define CIJ_CLASS_MOD_TYPE "MOD_TYPE"

const char * JAVA_CLASS_PATH;
const char * JAVA_LIBRARY_PATH;

/**
 * Called When c-icap proccess start.<br>
 * prev = none<br>
 * next = load_java_module(char * service_file)<br>
 *
 * @see load_java_module(char * service_file)
 * @param server_conf a pointer to the server configurations.
 * @return CI_OK
 */
int init_java_handler(struct ci_server_conf * server_conf) {
    enviroments = ci_dyn_array_new(MAX_ENVIRONMENTS_SIZE);//FREEME
    JAVA_CLASS_PATH = server_conf->SERVICES_DIR;
    return CI_OK;
}

/**
 * Called When all c-icap-java services's "java_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf)" has called.<br>
 * prev = java_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf)<br>
 * next = java_post_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf)<br>
 *
 * @see java_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf)
 * @see java_post_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf)
 * @param server_conf a pointer to the server configurations.
 * @return CI_OK
 */
int post_init_java_handler(struct ci_server_conf * server_conf) {
    return CI_OK;
}

/**
 * Called by each service scripts. initialize JVM.<br>
 * prev = init_java_handler(struct ci_server_conf * server_conf)<br>
 * next = java_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf)<br>
 *
 * @see init_java_handler(struct ci_server_conf * server_conf)
 * @see java_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf)
 * @param service_file
 * @return
 */
ci_service_module_t * load_java_module(char * service_file) {
    ci_service_module_t * service = NULL;
    jData_t * jdata = NULL;
    service = malloc(sizeof(ci_service_module_t));//FREEME
    jdata = malloc(sizeof(jData_t));//FREEME
    if (service == NULL || jdata == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL,"Failed to allocate memory for service %s",service_file);
        return NULL;
    }

    //set service file name to SERVICE NAME
    const char * service_file_name = basename(service_file);
    char * name = strndup(service_file_name, MAX_SERVICE_NAME);//FREEME
    name[strnlen(name, MAX_SERVICE_NAME)-strnlen(".class", MAX_SERVICE_NAME)] = '\0';//strip ".class"
    jdata->name = name;

    {
    //set CLASSPATH
    JavaVMOption options[1];
    int ret = asprintf(options[0].optionString, "-Djava.class.path=%s",JAVA_CLASS_PATH);//FREEME
    //"-verbose:jni";
    //"-Djava.library.path=%s.jar"

    if (ret < 0) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to allocate memory for service '%s'.", name);
        free(options[0].optionString);
        goto FAIL_TO_LOAD_SERVICE;
    }

    //create VM
    JavaVMInitArgs jvmInitArgs;
    jvmInitArgs.options = options;
    jvmInitArgs.nOptions = 1; //TODO:CLASSPATH,VERSION,...
    jvmInitArgs.version = JNI_VERSION_1_6;
    jvmInitArgs.ignoreUnrecognized = JNI_FALSE;
    jint ret = JNI_CreateJavaVM(&(jdata->jvm), (void **)&(jdata->jni), &jvmInitArgs);
    free(options[0].optionString);
    if (ret != JNI_OK) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to setup JavaVM(%d).", ret);
        goto FAIL_TO_LOAD_SERVICE;
    }
    }

    JNIEnv * jni = jdata->jni;

    //find class
    {
    const char className[MAX_CLASS_NAME];
    if (snprintf(className, "L%s;", name) < 0) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to setup className string for java class '%s'.", className);
        goto FAIL_TO_LOAD_SERVICE;
    }
    jclass service_class = (*jni)->FindClass(jni, className);
    if (service_class == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find java class '%s'.", className);
        goto FAIL_TO_LOAD_SERVICE;
    }
    jdata->jIcapClass = service_class;
    }
    jclass cls = jdata->jIcapClass;
/*
    //identify REQMOD, RESPMOD 'int class.MOD_TYPE'
    jfieldID mod_type_fid = (*jni)->GetStaticFieldID(jni, cls, "si", CIJ_CLASS_MOD_TYPE);
    if (mod_type_fid == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find field '%s'.", CIJ_CLASS_MOD_TYPE);
        goto FAIL_TO_LOAD_SERVICE;
    }
    jint mod_type = (*jni)->GetStaticIntField(jni, cls, mod_type_fid);
    jdata->mod_type = mod_type;
*/
    {
    jmethodID init = (*jni)->GetMethodID(jni, cls, "<init>", "(Ljava/lang/String;[Ljava/lang/String;)V");
    if (init == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find constructor method '%s()'.", name);
        goto FAIL_TO_LOAD_SERVICE;
    }
    jdata->jServiceConstructor = init;
    }

    {
    jmethodID preview = (*jni)->GetMethodID(jni, cls, "preview", "([B)I");
    if (preview == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find method 'preview(byte[])'.");
        goto FAIL_TO_LOAD_SERVICE;
    }
    jdata->jPreview = preview;
    }

    {
    jmethodID service = (*jni)->GetMethodID(jni, cls, "service", "([B)[B");
    if (service == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find method 'service(byte[], java.lang.String[])'.");
        goto FAIL_TO_LOAD_SERVICE;
    }
    jdata->jService = service;
    }
    /*
    Compiled from "iService.java"
    class iService {
      public iService(java.lang.String, java.lang.String[]);
        descriptor: (Ljava/lang/String;[Ljava/lang/String;)V
      public int preview(byte[]);
        descriptor: ([B)I
      public byte[] service(byte[]);
        descriptor: ([B)[B
    }
    */

    //TODO: stdout,stdin => c-icap's std

    service->mod_data = (void *)jdata;
    service->mod_conf_table = NULL;
    service->mod_init_service = java_init_service;
    service->mod_post_init_service = java_post_init_service;
    service->mod_close_service = java_close_service;
    service->mod_init_request_data = java_init_request_data;
    service->mod_release_request_data = java_release_request_data;
    service->mod_check_preview_handler = java_check_preview_handler;
    service->mod_end_of_data_handler = java_end_of_data_handler;
    service->mod_service_io = java_service_io;
    service->mod_name = name;
    service->mod_type = ICAP_REQMOD | ICAP_RESPMOD;
    cij_debug_printf(CIJ_MESSAGE_LEVEL, "OK service %s loaded\n", service_file);
    if (ci_dyn_array_add(enviroments, name, jdata, sizeof(jData_t *)) == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed adding service '%s' to dyn-array.", name);
        goto FAIL_TO_LOAD_SERVICE;
    }
    return service;

FAIL_TO_LOAD_SERVICE:
    free(name);
    free(service);
    free(jdata);
    cij_debug_printf(CIJ_ERROR_LEVEL, "Fail at loading service '%s'",service_file);
    ci_dyn_array_destroy(enviroments);
    return NULL;
}

/**
 * kills JVM. used at release_java_handler() .<br>
 *
 * @see release_java_handler()
 * @param data
 * @param name
 * @param value
 * @return JNI_OK = 0 if success
 */
int killVM(void *data, const char *name, const void * value) {
    const jData_t * jdata = (jData_t *)value;
    JNIEnv * jni = jdata->jni;
    (*jni)->DeleteLocalRef(jdata->jIcapClass);
    free(jdata->name);
    JavaVM * jvm = (jdata->jvm);
    jint ret = (*jvm)->DestroyJavaVM(jvm);
    free(jdata);
    return ret;
}

/**
 * Call killVM(void *data, const char *name, const void * value) to each JVMs.<br>
 * prev = java_close_service()<br>
 * next = none.<br>
 *
 * @see java_close_service()
 */
void release_java_handler() {
    //iterate and kill all JavaVMs
    ci_dyn_array_iterate(enviroments, NULL, killVM);
    return;
}

/**
 * Called after load_java_module(char * service_file) .<br>
 * initialize service OPTIONS parameter.<br>
 * prev = load_java_module(char * service_file)<br>
 * next =post_init_java_handler(struct ci_server_conf * server_conf)<br>
 *
 * @param srv_xdata a pointer holds service parameter.
 * @param server_conf a pointer of server config.
 * @return CI_SERVICE_OK
 */
int java_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf) {
    ci_service_set_preview(srv_xdata, 1024);
    ci_service_enable_204(srv_xdata);
    ci_service_set_transfer_preview(srv_xdata, "*");
    return CI_SERVICE_OK;
}

/**
 * initialize service OPTIONS parameter after post_init_java_handler(struct ci_server_conf * server_conf) called.<br>
 * prev = post_init_java_handler(struct ci_server_conf * server_conf)<br>
 * next = SERVICE IN<br>
 *
 * @see post_init_java_handler(struct ci_server_conf * server_conf)
 * @param srv_xdata a pointer holds service parameter.
 * @param server_conf a pointer of server config.
 * @return CI_SERVICE_OK
 */
int java_post_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf) {
    return 0;
}

/**
 * initialize ICAP Request.<br>
 * prev = recv Request<br>
 * next = java_check_preview_handler(char * preview_data, int preview_data_len, ci_request_t * req)<br>
 *
 * @see java_check_preview_handler(char * preview_data, int preview_data_len, ci_request_t * req)
 * @param req a pointer of request data.
 * @return service_data instance if OK else returns NULL(pass)
 */
void * java_init_request_data(ci_request_t * req) {
    const int REQ_TYPE = ci_req_type(req);
    const char * METHOD_TYPE = ci_method_string(REQ_TYPE);//Don't free!

    ci_headers_list_t *hdrs = NULL;

    //identify reqmod or respmod or else
    if (REQ_TYPE == ICAP_REQMOD) {
        hdrs = ci_http_request_headers(req);
    } else if (REQ_TYPE == ICAP_RESPMOD) {
        hdrs = ci_http_response_headers(req);
    } else if (REQ_TYPE == ICAP_OPTIONS){
        cij_debug_printf(CIJ_INFO_LEVEL, "ICAP OPTIONS comes. ignoring...");
        return NULL;//pass
    } else {
        //UNKNOWN ICAP METHOD
        cij_debug_printf(CIJ_INFO_LEVEL, "INVALID ICAP METHOD (NO. %d) has come. ignoring...", REQ_TYPE);
        return NULL;//pass
    }

    //create service_data
    jServiceData_t * jServiceData = NULL;
    jServiceData = (jServiceData_t *)malloc(sizeof(jServiceData_t));
    if (jServiceData == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Unable to allocate memory for jServiceData_t !");
        cij_debug_printf(CIJ_ERROR_LEVEL, "Dropping request...");
        return NULL;
    }

    //retribute Java environment from mod_name
    const char * mod_name = (req->current_service_mod)->mod_name;
    jData_t * jdata = (jData_t *)ci_dyn_array_search(enviroments, mod_name);
    if (jdata == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Invalid mod_name %s is not in environments array.", mod_name);
        free(jServiceData);
        return NULL;
    }

    //attach service instance to JVM
    jServiceData->jdata = jdata;
    JNIEnv * jni = jdata->jni;

    //create new HTTP headers array for java
    jclass jClass_String = (*jni)->FindClass("java/lang/String");
    jobjectArray jHeaders = (*jni)->NewObjectArray(jni,hdrs->used,jClass_String);
    if (jHeaders == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Could not allocate memory for http headers. ignoring...");
        return NULL;
    }
    (*jni)->DeleteLocalRef(jni, jClass_String);
    int i;
    for(i=0;i<hdrs->used;i++) {
        jstring buf = (*jni)->NewStringUTF(hdrs->headers[i]);
        if (buf == NULL) {
            cij_debug_printf(CIJ_ERROR_LEVEL, "Could not allocate memory for http headers string. ignoring...");
            return NULL;
        }
        (*jni)->SetObjectArrayElement(jni,i,jHeaders,buf);
        (*jni)->DeleteLocalRef(jni, buf);
    }

    //create instance.
    jobject jInstance = (*jni)->NewObject(jni, jdata->jIcapClass, jdata->jServiceConstructor,METHOD_TYPE,jHeaders);
    (*jni)->DeleteLocalRef(jHeaders);
    if (jInstance == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Could not create instance of the class '%s'. Method='%s'. ignoring...", mod_name, METHOD_TYPE);
        return NULL;
    }
    jServiceData->instance = jInstance;//+REF
    ci_membuf_t * buffer = ci_membuf_new();
    if (buffer == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Could not allocate memory for http body ignoring...");
        return NULL;
    }
    return (void *)jServiceData;
}

/**
 * Preview HTTP Body and determine hook or unlock the request.<br>
 * prev = java_init_request_data(ci_request_t * req)<br>
 * MOD_CONTINUE = java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req)<br>
 * ALLOW204 = java_release_request_data(void * data)<br>
 *
 * @see java_init_request_data(ci_request_t * req)
 * @see java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req)
 * @see java_release_request_data(void * data)
 * @param preview_data preview body.
 * @param preview_data_len preview body byte length.
 * @param req a pointer of request data.
 * @return CI_MOD_ALLOW204 if unhook the request. CI_MOD_CONTINUE if hook the request. CI_ERROR if an error ocur.
 */
int java_check_preview_handler(char * preview_data, int preview_data_len, ci_request_t * req) {
    jServiceData_t * jServiceData = (jServiceData_t *)ci_service_data(req);
    JNIEnv * jni = jServiceData->jdata->jni;
    jData_t * jdata = jServiceData.jdata;
    jobject jInstance = jServiceData->instance;

    //convert C-char* to Java-byte[]
    jbyteArray jba = (*jni)->NewByteArray(jni, preview_data_len);
    if (jba == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Could not allocate memory for preview_data byte array object. ignoring...");
        return CI_ERROR;
    }
    int i;
    for (i=0; i<preview_data_len; i++) {
        (*jni)->SetByteArrayRegion
    }

    //int preview(byte[])
    jint status = (*jni)->CallIntMethod(jni, jInstance, jdata->jPreview, (*jni)->New);
    if (status) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "%s.initialize(...) returned not %s.", CI_OK);
        return NULL;
    }
    //check preview data
    //hasbody
    //lock or  unlock
    // send to service_io?
    return CI_MOD_CONTINUE;
}

/**
 * send-recv ICAP Request body buffer.<br>
 * prev = java_check_preview_handler(char * preview_data, int preview_data_len, ci_request_t * req) or java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req)<br>
 * next = java_end_of_data_handler(ci_request_t * req) or java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req)<br>
 * jumps = java_release_request_data(void * data) when return CI_MOD_ALLOW204<br>
 *<br>
 * Calls java_end_of_data_handler(ci_request_t * req) when iseof is true.<br>
 * Calls java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req) recursively while iseof is not true.<br>
 *
 * @see java_check_preview_handler(char * preview_data, int preview_data_len, ci_request_t * req)
 * @see java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req)
 * @see java_end_of_data_handler(ci_request_t * req)
 * @param wbuf buffer to write body to send
 * @param wlen a pointer of wbuf byte length
 * @param rbuf buffer to write body to recv
 * @param rlen a pointer of rbug byte length
 * @param iseof identify end of body
 * @param req a pointer of request data.
 * @return CI_OK if modification is ok. (if *wlen equals CI_EOF then modification is OK and no write anymore)
 */
int java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req) {
    int ret = CI_OK;

    if (rlen && rbuf) {
        //call
    }

    if (wlen && wbuf) {
        //call
    }
    return ret;
}
/**
 * Called when if wlen == CI_EOF in java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req)<br>
 * prev = java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req)<br>
 * next = java_release_request_data(void * data)<br>
 *
 * @see java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req)
 * @see java_release_request_data(void * data)
 * @param req a pointer of request data.
 * @return CI_OK if continue modification, CI_MOD_DONE if modification has done
 */
int java_end_of_data_handler(ci_request_t * req) {
    //replace headers
    return CI_MOD_DONE;
}

/**
 * finalize ICAP request.<br>
 * prev = java_end_of_data_handler(ci_request_t * req)<br>
 * next = send Request<br>
 *
 * @see java_end_of_data_handler(ci_request_t * req)
 * @param data service_data
 */
void java_release_request_data(void * data) {
    jServiceData_t * jServiceData = (jServiceData_t *)data;
    JNIEnv * jni = (jServiceData->jdata)->jni;
    (*jni)->DeleteLocalRef(jni, jServiceData->instance);
    free(jServiceData);
}

/**
 * close service.<br>
 *<br>
 * prev = STOP SIGNAL FROM PIPE or SIGTERM?<br>
 * next = release_java_handler()<br>
 *
 * @see release_java_handler()
 */
void java_close_service() {
    //nop
}
