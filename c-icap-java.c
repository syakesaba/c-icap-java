#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

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

#if 1
    #define cij_debug_printf(lev,msg, ...) ci_debug_printf(1, "(%d):%s():L%04d: "msg"\n", getpid(), __func__, __LINE__, ## __VA_ARGS__)
#else
    #define cij_debug_printf(lev,msg, ...) ci_debug_printf(lev, "CIJ:"msg"\n", ## __VA_ARGS__)
#endif

#define MAX_JVM_OPTIONS 4

typedef struct jDataStruct {
    JNIEnv * jni;
    JavaVM * jvm;
    JavaVMOption jvmOptions[4];
    jclass jIcapClass;
    char * name;
    int mod_type;
    jmethodID jServiceConstructor;
    jmethodID jInitialize;
    jmethodID jPreview;
    jmethodID jService;
//    jmethodID jInitService;
//    jmethodID jPostInitService;
//    jmethodID jCloseService;
//    jmethodID jInitRequestData;
//    jmethodID jReleaseRequestData;
//    jmethodID jCheckPreviewHandler;
//    jmethodID jEndOfDataHandler;
//    jmethodID jServiceIO;
    u_int32_t times_served;
} jData_t;

typedef struct jServiceDataStruct {
    jData_t * jdata;
    u_int32_t request_id;
    jobject instance;
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

CI_DECLARE_DATA service_handler_module_t module = {
    "java_handler",
    ".class",
    init_java_handler,
    post_init_java_handler,
    release_java_handler,
    load_java_module,
    NULL // TODO: conf-table
};

ci_dyn_array_t * enviroments;
#define MAX_ENVIRONMENTS_SIZE 256
#define CIJ_CLASS_MOD_TYPE "MOD_TYPE"


const char * JAVA_CLASS_PATH;
const char * JAVA_LIBRARY_PATH;

int init_java_handler(struct ci_server_conf * server_conf) {
    enviroments = ci_dyn_array_new(MAX_ENVIRONMENTS_SIZE);//FREEME
    JAVA_CLASS_PATH = server_conf->SERVICES_DIR;
    return CI_OK;
}

int post_init_java_handler(struct ci_server_conf * server_conf) {
    return CI_OK;
}

ci_service_module_t * load_java_module(char * service_file) {
    ci_service_module_t * service = NULL;
    jData_t * jdata = NULL;
    service = malloc(sizeof(ci_service_module_t));//FREEME
    jdata = malloc(sizeof(jData_t));//FREEME
    if (service == NULL || jdata == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL,"Failed to allocate memory for service %s",service_file);
        return NULL;
    }
    jdata->times_served = 0;

    //set service file name to SERVICE NAME
    const char * service_file_name = basename(service_file);
    char * name = strndup(service_file_name, MAX_SERVICE_NAME);//FREEME
    name[strnlen(name, MAX_SERVICE_NAME)-strnlen(".class", MAX_SERVICE_NAME)] = '\0';//strip ".class"
    jdata->name = name;

    {
    //set CLASSPATH
    JavaVMOption * options = jdata->jvmOptions;
    sprintf(options[0].optionString, "-Djava.class.path=%s",JAVA_CLASS_PATH);
    //options[1].optionString = "-verbose:jni";
    //options[2].optionString = sprintf("-Djava.library.path=%s",LIBRARIES_PATH);
    //options[3].optionString = "";

    //create VM
    JavaVMInitArgs jvmInitArgs;
    jvmInitArgs.options = options;
    jvmInitArgs.nOptions = 1; //TODO:CLASSPATH,VERSION,...
    jvmInitArgs.version = JNI_VERSION_1_6;
    jvmInitArgs.ignoreUnrecognized = JNI_FALSE;
    jint ret = JNI_CreateJavaVM(&(jdata->jvm), (void **)&(jdata->jni), &jvmInitArgs);
    if (ret != JNI_OK) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to setup JavaVM(%d).", ret);
        goto FAIL_TO_LOAD_SERVICE;
    }
    }

    JNIEnv * jni = jdata->jni;

    //find class
    {
    const char * className = NULL;
    snprintf(className, "L%s;", name);
    jclass service_class = (*jni)->FindClass(jni, className);
    if (service_class == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find java class '%s'.", className);
        goto FAIL_TO_LOAD_SERVICE;
    }
    jdata->jIcapClass = service_class;
    }
    jclass cls = jdata->jIcapClass;

    //identify REQMOD, RESPMOD 'int class.MOD_TYPE'
    jfieldID mod_type_fid = (*jni)->GetStaticFieldID(jni, cls, "si", CIJ_CLASS_MOD_TYPE);
    if (mod_type_fid == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find field '%s'.", CIJ_CLASS_MOD_TYPE);
        goto FAIL_TO_LOAD_SERVICE;
    }
    jint mod_type = (*jni)->GetStaticIntField(jni, cls, mod_type_fid);
    jdata->mod_type = mod_type;

    //TODO: get <init> for init_request_data
    {
    jmethodID init = (*jni)->GetMethodID(jni, cls, "<init>", "()V");
    if (init == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find constructor method '%s()'.", name);
        goto FAIL_TO_LOAD_SERVICE;
    }
    jdata->jServiceConstructor = init;
    }

    //TODO: get initialize(String,String[]) for init_request_data
    {
    jmethodID initialize = (*jni)->GetMethodID(jni, cls, "initialize", "(Ljava/lang/String;[Ljava/lang/String;)V");
    if (initialize == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find method 'initialize(String, String[])'.");
        goto FAIL_TO_LOAD_SERVICE;
    }
    jdata->jInitialize = initialize;
    }

    //TODO: get   int preview(String preview){} for check_preview_handler
    {
    jmethodID preview = (*jni)->GetMethodID(jni, cls, "preview", "([B[Ljava/lang/String;)I");
    if (preview == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL, "Failed to find constructor method 'preview(byte[], java.lang.String[])'.");
        goto FAIL_TO_LOAD_SERVICE;
    }
    jdata->jPreview = preview;
    }
    /*
     public void initialize(java.lang.String, java.lang.String[]);
      descriptor: (Ljava/lang/String;[Ljava/lang/String;)V

    public int preview(byte[], java.lang.String[]);
      descriptor: ([B[Ljava/lang/String;)I

    public byte[] service(byte[], java.lang.String[]);
      descriptor: ([B[Ljava/lang/String;)[B
*/
    //TODO: get   void ini(byte[] data){} for end_of_data_handler
    //TODO: get   byte[] service(byte[] data){} for end_of_data_handler
    //TODO: get   byte[] preview(byte[] data){} for end_of_data_handler

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
    ci_dyn_array_add(enviroments, name, jdata, sizeof(jData_t *));
    return service;

FAIL_TO_LOAD_SERVICE:
    free(name);
    free(service);
    free(jdata);
    cij_debug_printf(CIJ_ERROR_LEVEL, "Fail at loading service '%s'",service_file);
    ci_dyn_array_destroy(enviroments);
    return NULL;
}

int killVM(void *data, const char *name, const void * value) {
    const jData_t * jdata = (jData_t *)value;
    const char * icapName = jdata->name;
    free((void *)icapName);
    JavaVM * jvm = (jdata->jvm);
    jint ret = (*jvm)->DestroyJavaVM(jvm);
    free((void *)jdata);
    return ret;
}

void release_java_handler() {
    //iterate and kill all JavaVMs
    ci_dyn_array_iterate(enviroments, NULL, killVM);
    return;
}

int java_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf) {
    ci_service_set_preview(srv_xdata, 1024);
    ci_service_enable_204(srv_xdata);
    ci_service_set_transfer_preview(srv_xdata, "*");
    return 0;
}

int java_post_init_service(ci_service_xdata_t * srv_xdata, struct ci_server_conf * server_conf) {
    return 0;
}

void java_close_service() {
    //nop
}

void * java_init_request_data(ci_request_t * req) {
    const int REQ_TYPE = ci_req_type(req);
    const char * METHOD_TYPE = ci_method_string(REQ_TYPE);//Don't free!

    ci_headers_list_t *hdrs = NULL;

    if (REQ_TYPE == ICAP_REQMOD) {
        hdrs = ci_http_request_headers(req);
    } else if (REQ_TYPE == ICAP_RESPMOD) {
        hdrs = ci_http_response_headers(req);
    } else if (REQ_TYPE == ICAP_OPTIONS){
        // something? ;
        return NULL;
    } else {
        // UNKNOWN protocol
        return NULL;
    }
    //identify reqmod or respmod or else
    //create new instance calling constructor;
    //XXX: init(int mod_type, String[] headers)
    return (void *) NULL;
}

void java_release_request_data(void * data) {
    jServiceData_t * jServiceData = (jServiceData_t *)data;
    JNIEnv * jni = (jServiceData->jdata)->jni;
    (*jni)->DeleteLocalRef(jni, jServiceData->instance);
}

int java_check_preview_handler(char * preview_data, int preview_data_len, ci_request_t * req) {
    //check preview data
    //hasbody
    //lock or  unlock
    // send to service_io?
    return CI_MOD_CONTINUE;
}

int java_end_of_data_handler(ci_request_t * req) {
    //replace headers
    return CI_MOD_DONE;
}

int java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req) {
    int ret;

    if (rlen && rbuf) {
        //call
    }

    if (wlen && wbuf) {
        //call
    }
    return ret;
}
