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
#define CIJ_DEBUG_LEVEL 9

#if 1
    #define cij_debug_printf(lev,msg, ...) ci_debug_printf(1, "(%d):%s():L%04d: "msg"\n", getpid(), __func__, __LINE__, ## __VA_ARGS__)
#else
    #define cij_debug_printf(lev,msg, ...) ci_debug_printf(lev, "CIJ:"msg"\n", ## __VA_ARGS__)
#endif

#define MAX_JVM_OPTIONS 4

typedef struct jData {
    JNIEnv jni;
    JavaVM * jvm;
    JavaVMOption * jvmOptions;
    jclass jIcapClass;
    const char * name;
    const int mod_type;
    jobject jIcapInstance;
    jmethodID jInitService;
    jmethodID jPostInitService;
    jmethodID jCloseService;
    jmethodID jInitRequestData;
    jmethodID jReleaseRequestData;
    jmethodID jCheckPreviewHandler;
    jmethodID jEndOfDataHandler;
    jmethodID jServiceIO;
} jData_t;

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

const char * LIBRARIES_PATH;

int init_java_handler(struct ci_server_conf * server_conf) {
    enviroments = ci_dyn_array_new(MAX_ENVIRONMENTS_SIZE);
    cij_debug_printf(CIJ_INFO_LEVEL, "SERVICES_DIR: %s", server_conf->SERVICES_DIR);
    cij_debug_printf(CIJ_INFO_LEVEL, "MODULES_DIR: %s", server_conf->MODULES_DIR);
    LIBRARIES_PATH = server_conf->SERVICES_DIR;//:server_conf->MODULES_DIR;
    return CI_OK;
}

int post_init_java_handler(struct ci_server_conf * server_conf) {
    return CI_OK;
}

ci_service_module_t * load_java_module(char * service_file) {
    ci_service_module_t * service = NULL;
    jData_t * jdata = NULL;
    service = malloc(sizeof(ci_service_module_t));
    jdata = malloc(sizeof(jData_t));
    if (service == NULL || jdata == NULL) {
        cij_debug_printf(CIJ_ERROR_LEVEL,"Failed to allocate memory for service %s",service_file);
        return NULL;
    }
    //set service file name to SERVICE NAME
    const char * service_file_name = basename(service_file);
    const char * name = strdup(service_file_name);
    jdata->name = name;

    //set CLASSPATH
    JavaVMOption * options = jdata->jvmOptions;
    options[0].optionString = LIBRARIES_PATH;

    JavaVMInitArgs jvmInitArgs;
    jvmInitArgs.options = options;
    jvmInitArgs.nOptions = 2; //TODO:CLASSPATH,VERSION,...
    jvmInitArgs.version = JNI_VERSION_1_8;
    jvmInitArgs.ignoreUnrecognized = JNI_VERSION_1_8;

    JNI_CreateJavaVM(&(jdata->jvm), (void **)&(jdata->jni), &jvmInitArgs);
    //initialize jvm;
    //load class
    //identify REQMOD, RESPMOD

    service->mod_data = NULL;
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
    ci_debug_printf(1, "OK service %s loaded\n", service_file);
    return service;
}

void release_java_handler() {
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
}

void * java_init_request_data(ci_request_t * req) {
    ci_headers_list_t * hdrs = ci_http_request_headers(req);
    //identify reqmod or respmod or else
    //create new instance;
    return (void *) NULL;
}

void java_release_request_data(void * data) {
    jData_t * jdata = (jData_t *) data;
    free(jdata);
}

int java_check_preview_handler(char * preview_data, int preview_data_len, ci_request_t * req) {
    jData_t * jdata = (jData_t *)ci_service_data(req);
    //check preview data
    //hasbody
    //lock or  unlock
    // send to service_io?
    return CI_MOD_CONTINUE;
}

int java_end_of_data_handler(ci_request_t * req) {
    jData_t * jdata = (jData_t *)ci_service_data(req);
    //replace headers
    return CI_MOD_DONE;
}

int java_service_io(char * wbuf, int * wlen, char * rbuf, int * rlen, int iseof, ci_request_t * req) {
    jData_t * jdata = (jData_t *)ci_service_data(req);
    int ret;

    if (rlen && rbuf) {
        //call
    }

    if (wlen && wbuf) {
        //call
    }
    return ret;
}
