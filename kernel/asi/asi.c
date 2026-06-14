/*
 * TriniOS - AI Service Interface (ASI) Client Implementation
 *
 * Client library for communicating with the LLM Inference Server.
 * Provides the API for any kernel or user-space component to
 * make AI inference requests.
 */

#include "asi.h"
#include "../ipc/ipc_port.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================================
 * Internal State
 * ============================================================================ */

static bool g_asi_initialized = false;
static mach_port_t g_asi_reply_port = MACH_PORT_NULL;
static uint32_t g_next_request_id = 1;

/* ============================================================================
 * asi_client_init - Initialize the ASI client
 * ============================================================================ */

kern_return_t asi_client_init(void)
{
    if (g_asi_initialized) {
        return KERN_SUCCESS;
    }

    /* Allocate reply port for receiving responses */
    kern_return_t kr = mach_port_allocate(mach_task_self(),
                                          &g_asi_reply_port,
                                          MACH_PORT_RIGHT_RECEIVE);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    g_next_request_id = 1;
    g_asi_initialized = true;

    return KERN_SUCCESS;
}

/* ============================================================================
 * asi_client_shutdown - Shutdown the ASI client
 * ============================================================================ */

void asi_client_shutdown(void)
{
    if (!g_asi_initialized) {
        return;
    }

    if (g_asi_reply_port != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), g_asi_reply_port);
        g_asi_reply_port = MACH_PORT_NULL;
    }

    g_asi_initialized = false;
}

/* ============================================================================
 * asi_server_available - Check if AI server is available
 * ============================================================================ */

bool asi_server_available(void)
{
    if (!g_asi_initialized) {
        return false;
    }

    return ipc_server_active(IPC_SERVER_AI);
}

/* ============================================================================
 * asi_infer - Send an inference request (blocking, simplified API)
 * ============================================================================ */

kern_return_t asi_infer(const char *prompt,
                        const char *system_prompt,
                        const char *model_name,
                        float temperature,
                        int32_t max_tokens,
                        char **response_out,
                        uint32_t *response_len_out)
{
    if (!g_asi_initialized || !prompt || !response_out) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Build full request */
    asi_infer_request_t request;
    memset(&request, 0, sizeof(request));

    request.asi_type = ASI_MSG_INFER;
    request.request_id = g_next_request_id++;
    request.sender_task = mach_task_self();
    request.flags = ASI_FLAG_CACHE_ENABLED;
    request.temperature = temperature;
    request.top_p = 0.9f;
    request.max_tokens = max_tokens;
    request.stop_count = 0;
    request.seed = 0;
    request.repeat_penalty = false;
    request.reply_port = g_asi_reply_port;

    /* Copy prompt */
    strncpy(request.prompt, prompt, ASI_MAX_PROMPT_LEN - 1);
    request.prompt_len = strlen(request.prompt);

    /* Copy system prompt if provided */
    if (system_prompt) {
        strncpy(request.system_prompt, system_prompt, ASI_MAX_SYSTEM_PROMPT - 1);
        request.system_prompt_len = strlen(request.system_prompt);
    }

    /* Copy model name if provided */
    if (model_name) {
        strncpy(request.model_name, model_name, ASI_MODEL_NAME_LEN - 1);
    }

    /* Send request and receive reply */
    asi_infer_reply_t reply;
    kern_return_t kr = asi_infer_full(&request, &reply);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    /* Allocate response buffer */
    uint32_t len = reply.response_len;
    char *response = (char *)malloc(len + 1);
    if (!response) {
        return KERN_RESOURCE_SHORTAGE;
    }

    strncpy(response, reply.response, len);
    response[len] = '\0';

    *response_out = response;
    if (response_len_out) {
        *response_len_out = len;
    }

    return KERN_SUCCESS;
}

/* ============================================================================
 * asi_infer_full - Send inference request with full control
 * ============================================================================ */

kern_return_t asi_infer_full(asi_infer_request_t *request,
                             asi_infer_reply_t *reply)
{
    if (!g_asi_initialized || !request || !reply) {
        return KERN_INVALID_ARGUMENT;
    }

    /* Check server availability */
    ipc_server_entry_t *server = ipc_lookup_server(IPC_SERVER_AI);
    if (!server || !server->active) {
        return KERN_INVALID_RIGHT;
    }

    /* Set reply port */
    if (request->reply_port == MACH_PORT_NULL) {
        request->reply_port = g_asi_reply_port;
    }

    /* Set destination */
    request->header.msgh_remote_port = server->send_port;
    request->header.msgh_local_port = request->reply_port;
    request->header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                                                MACH_MSG_TYPE_MAKE_SEND);
    request->header.msgh_size = sizeof(asi_infer_request_t);
    request->header.msgh_id = request->asi_type;

    /* Send request */
    kern_return_t kr = mach_msg_send(&request->header);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    /* Receive reply */
    memset(reply, 0, sizeof(asi_infer_reply_t));
    reply->header.msgh_size = 0;

    kr = mach_msg(&reply->header,
                  MACH_RCV_MSG | MACH_RCV_LARGE,
                  0,
                  sizeof(asi_infer_reply_t),
                  request->reply_port,
                  MACH_MSG_TIMEOUT_NONE,
                  MACH_PORT_NULL);

    if (kr != MACH_MSG_SUCCESS) {
        return kr;
    }

    return reply->result;
}

/* ============================================================================
 * asi_load_model - Load a model
 * ============================================================================ */

kern_return_t asi_load_model(const char *model_path,
                             const char *model_name,
                             bool set_default,
                             bool gpu_load)
{
    if (!g_asi_initialized || !model_path) {
        return KERN_INVALID_ARGUMENT;
    }

    ipc_server_entry_t *server = ipc_lookup_server(IPC_SERVER_AI);
    if (!server || !server->active) {
        return KERN_INVALID_RIGHT;
    }

    asi_load_model_request_t request;
    memset(&request, 0, sizeof(request));

    request.asi_type = ASI_MSG_LOAD_MODEL;
    request.request_id = g_next_request_id++;
    request.flags = 0;
    strncpy(request.model_path, model_path, 511);
    request.set_default = set_default;
    request.gpu_load = gpu_load;
    request.reply_port = g_asi_reply_port;

    if (model_name) {
        strncpy(request.model_name, model_name, ASI_MODEL_NAME_LEN - 1);
    }

    /* Send */
    request.header.msgh_remote_port = server->send_port;
    request.header.msgh_local_port = request.reply_port;
    request.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                                               MACH_MSG_TYPE_MAKE_SEND);
    request.header.msgh_size = sizeof(asi_load_model_request_t);
    request.header.msgh_id = request.asi_type;

    kern_return_t kr = mach_msg_send(&request.header);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    /* Receive reply */
    asi_load_model_reply_t reply;
    memset(&reply, 0, sizeof(reply));

    kr = mach_msg(&reply.header,
                  MACH_RCV_MSG | MACH_RCV_LARGE,
                  0,
                  sizeof(asi_load_model_reply_t),
                  request.reply_port,
                  MACH_MSG_TIMEOUT_NONE,
                  MACH_PORT_NULL);

    if (kr != MACH_MSG_SUCCESS) {
        return kr;
    }

    return reply.result;
}

/* ============================================================================
 * asi_list_models - List loaded models
 * ============================================================================ */

kern_return_t asi_list_models(asi_model_info_t *models,
                              uint32_t *model_count)
{
    /* TODO: Implement list models request/reply
     * This would require a variable-length reply message.
     * For now, return KERN_NOT_IMPLEMENTED. */
    (void)models;
    (void)model_count;
    return KERN_NOT_IMPLEMENTED;
}

/* ============================================================================
 * asi_get_stats - Get ASI statistics
 * ============================================================================ */

kern_return_t asi_get_stats(asi_stats_t *stats)
{
    /* TODO: Implement stats request */
    (void)stats;
    return KERN_NOT_IMPLEMENTED;
}

/* ============================================================================
 * asi_get_config - Get configuration
 * ============================================================================ */

kern_return_t asi_get_config(asi_config_t *config)
{
    /* TODO: Implement config request */
    (void)config;
    return KERN_NOT_IMPLEMENTED;
}

/* ============================================================================
 * asi_set_config - Set configuration
 * ============================================================================ */

kern_return_t asi_set_config(asi_config_t *config)
{
    /* TODO: Implement config request */
    (void)config;
    return KERN_NOT_IMPLEMENTED;
}

/* ============================================================================
 * asi_verify_output - Verify AI output signature
 * ============================================================================ */

kern_return_t asi_verify_output(const char *output,
                                const uint8_t *signature,
                                bool *valid_out)
{
    /* TODO: Implement cryptographic verification
     * This would:
     * 1. Hash the output with SHA-512
     * 2. Compare against the signature
     * 3. Optionally verify against the server's public key */
    (void)output;
    (void)signature;
    (void)valid_out;
    return KERN_NOT_IMPLEMENTED;
}