/*
 * TriniOS - AI Service Interface (ASI)
 *
 * Defines the IPC message format for AI inference requests.
 * This is the system-level interface between any component and the
 * LLM Inference Server, making AI a first-class system primitive.
 */

#ifndef ASI_H
#define ASI_H

#include <mach/mach.h>
#include <mach/message.h>
#include <mach/port.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * ASI Configuration
 * ============================================================================ */

#define ASI_MAX_PROMPT_LEN        (32 * 1024)    /* 32KB prompt */
#define ASI_MAX_RESPONSE_LEN      (64 * 1024)    /* 64KB response */
#define ASI_MAX_SYSTEM_PROMPT     (8 * 1024)     /* 8KB system prompt */
#define ASI_MAX_MODELS            16
#define ASI_MODEL_NAME_LEN        128
#define ASI_MAX_CONTEXT_TOKENS    8192
#define ASI_SIGNATURE_LEN         64             /* SHA-512 signature */

/* ============================================================================
 * ASI Message IDs (for Mach IPC)
 * ============================================================================ */

typedef enum {
    /* Inference requests */
    ASI_MSG_INFER               = 0x1001,
    ASI_MSG_INFER_REPLY         = 0x1002,
    ASI_MSG_STREAM_INFER        = 0x1003,        /* Streaming inference */
    ASI_MSG_STREAM_CHUNK        = 0x1004,        /* Stream chunk */
    ASI_MSG_STREAM_END          = 0x1005,

    /* Model management */
    ASI_MSG_LOAD_MODEL          = 0x2001,
    ASI_MSG_LOAD_MODEL_REPLY    = 0x2002,
    ASI_MSG_UNLOAD_MODEL        = 0x2003,
    ASI_MSG_LIST_MODELS         = 0x2004,
    ASI_MSG_LIST_MODELS_REPLY   = 0x2005,
    ASI_MSG_SET_DEFAULT_MODEL   = 0x2006,

    /* Configuration */
    ASI_MSG_SET_CONFIG          = 0x3001,
    ASI_MSG_GET_CONFIG          = 0x3002,
    ASI_MSG_GET_CONFIG_REPLY    = 0x3003,

    /* Verification */
    ASI_MSG_SIGN_OUTPUT         = 0x4001,        /* Cryptographically sign AI output */
    ASI_MSG_SIGN_OUTPUT_REPLY   = 0x4002,
    ASI_MSG_VERIFY_OUTPUT       = 0x4003,        /* Verify AI output signature */
    ASI_MSG_VERIFY_OUTPUT_REPLY = 0x4004,

    /* System */
    ASI_MSG_GET_STATS           = 0x5001,
    ASI_MSG_GET_STATS_REPLY     = 0x5002,
    ASI_MSG_HEARTBEAT           = 0x5003,
    ASI_MSG_SHUTDOWN            = 0x5004,
} asi_msg_type_t;

/* ============================================================================
 * ASI Request Header
 * ============================================================================ */

typedef struct {
    mach_msg_header_t         header;
    mach_msg_size_t           msgh_descriptor_count;
    asi_msg_type_t            asi_type;          /* ASI message type */
    uint32_t                  request_id;        /* Unique request ID */
    task_t                    sender_task;       /* Calling task */
    uint32_t                  flags;             /* Request flags */
} asi_request_header_t;

/* ============================================================================
 * ASI Reply Header
 * ============================================================================ */

typedef struct {
    mach_msg_header_t         header;
    mach_msg_size_t           msgh_descriptor_count;
    asi_msg_type_t            asi_type;          /* Matching ASI message type */
    uint32_t                  request_id;        /* Original request ID */
    kern_return_t             result;            /* Operation result */
    uint32_t                  token_count;       /* Tokens generated */
    uint64_t                  latency_us;        /* Latency in microseconds */
} asi_reply_header_t;

/* ============================================================================
 * Request Flags
 * ============================================================================ */

#define ASI_FLAG_SIGN_OUTPUT        0x00000001  /* Sign output cryptographically */
#define ASI_FLAG_AUDIT_TRAIL        0x00000002  /* Record in audit trail */
#define ASI_FLAG_PRIORITY_HIGH      0x00000004  /* High priority inference */
#define ASI_FLAG_PRIORITY_LOW       0x00000008  /* Low priority (batch) */
#define ASI_FLAG_STREAMING          0x00000010  /* Stream response tokens */
#define ASI_FLAG_GPU_ACCELERATED    0x00000020  /* Require GPU acceleration */
#define ASI_FLAG_CACHE_ENABLED      0x00000040  /* Allow response caching */

/* ============================================================================
 * Inference Request
 * ============================================================================ */

typedef struct {
    mach_msg_header_t         header;
    mach_msg_size_t           msgh_descriptor_count;
    asi_msg_type_t            asi_type;
    uint32_t                  request_id;
    task_t                    sender_task;
    uint32_t                  flags;

    /* Prompt */
    char                      prompt[ASI_MAX_PROMPT_LEN];
    uint32_t                  prompt_len;

    /* System prompt (optional, for context) */
    char                      system_prompt[ASI_MAX_SYSTEM_PROMPT];
    uint32_t                  system_prompt_len;

    /* Model selection */
    char                      model_name[ASI_MODEL_NAME_LEN];

    /* Generation parameters */
    float                     temperature;      /* 0.0 - 2.0 */
    float                     top_p;            /* 0.0 - 1.0 */
    int32_t                   max_tokens;       /* Max tokens to generate */
    int32_t                   stop_count;       /* Number of stop strings */
    char                      stop_strings[8][128]; /* Stop strings */

    /* Seed for reproducibility */
    uint64_t                  seed;
    bool                      repeat_penalty;   /* Enable repetition penalty */

    /* Reply port */
    mach_port_t               reply_port;
} asi_infer_request_t;

/* ============================================================================
 * Inference Reply
 * ============================================================================ */

typedef struct {
    mach_msg_header_t         header;
    mach_msg_size_t           msgh_descriptor_count;
    asi_msg_type_t            asi_type;
    uint32_t                  request_id;
    kern_return_t             result;
    uint32_t                  token_count;
    uint64_t                  latency_us;

    /* Response */
    char                      response[ASI_MAX_RESPONSE_LEN];
    uint32_t                  response_len;

    /* Completion reason */
    uint32_t                  finish_reason;    /* 0=stop, 1=length, 2=error */

    /* Cryptographic signature (if ASI_FLAG_SIGN_OUTPUT set) */
    uint8_t                   signature[ASI_SIGNATURE_LEN];
    bool                      signed_;

    /* Cache info */
    bool                      from_cache;
    uint64_t                  cache_key;
} asi_infer_reply_t;

/* ============================================================================
 * Model Information
 * ============================================================================ */

typedef struct {
    char                      name[ASI_MODEL_NAME_LEN];
    char                      path[512];
    uint64_t                  param_count;      /* Number of parameters */
    uint32_t                  context_length;   /* Max context length */
    uint32_t                  vocab_size;       /* Vocabulary size */
    uint64_t                  memory_required;  /* Memory required (bytes) */
    bool                      loaded;           /* Currently loaded */
    bool                      default_model;    /* Default model */
    bool                      gpu_resident;     /* On GPU */
} asi_model_info_t;

/* ============================================================================
 * Model Load Request
 * ============================================================================ */

typedef struct {
    mach_msg_header_t         header;
    mach_msg_size_t           msgh_descriptor_count;
    asi_msg_type_t            asi_type;
    uint32_t                  request_id;
    uint32_t                  flags;

    char                      model_path[512];
    char                      model_name[ASI_MODEL_NAME_LEN];
    bool                      set_default;      /* Make default model */
    bool                      gpu_load;         /* Load onto GPU */
    uint32_t                  gpu_layer_count;  /* Layers on GPU (0=all) */

    mach_port_t               reply_port;
} asi_load_model_request_t;

typedef struct {
    mach_msg_header_t         header;
    mach_msg_size_t           msgh_descriptor_count;
    asi_msg_type_t            asi_type;
    uint32_t                  request_id;
    kern_return_t             result;
    uint64_t                  load_time_ms;     /* Model load time */
    uint64_t                  memory_used;      /* Memory consumed */
} asi_load_model_reply_t;

/* ============================================================================
 * ASI Configuration
 * ============================================================================ */

typedef struct {
    /* Generation defaults */
    float                     default_temperature;
    float                     default_top_p;
    int32_t                   default_max_tokens;

    /* Performance */
    uint32_t                  gpu_layer_count;  /* Layers on GPU */
    uint32_t                  thread_count;     /* CPU threads */
    uint32_t                  batch_size;       /* Batch size */

    /* Verification */
    bool                      signing_enabled;  /* Cryptographic signing */
    bool                      audit_enabled;    /* Audit trail */
    char                      audit_path[512];  /* Audit log path */

    /* Caching */
    bool                      cache_enabled;    /* Response caching */
    uint64_t                  cache_size_mb;    /* Cache size limit */
} asi_config_t;

/* ============================================================================
 * ASI Statistics
 * ============================================================================ */

typedef struct {
    uint64_t                  total_requests;   /* Total inference requests */
    uint64_t                  total_tokens;     /* Total tokens generated */
    uint64_t                  cache_hits;       /* Cache hits */
    uint64_t                  cache_misses;     /* Cache misses */
    uint64_t                  avg_latency_us;   /* Average latency */
    uint64_t                  max_latency_us;   /* Maximum latency */
    uint64_t                  min_latency_us;   /* Minimum latency */
    uint32_t                  active_requests;  /* Currently processing */
    uint32_t                  queue_depth;      /* Waiting in queue */
    uint32_t                  loaded_models;    /* Models currently loaded */
    uint64_t                  gpu_memory_used;  /* GPU memory in use */
    uint64_t                  cpu_memory_used;  /* CPU memory in use */
} asi_stats_t;

/* ============================================================================
 * ASI API Functions
 * ============================================================================ */

/* Initialize the ASI client */
kern_return_t asi_client_init(void);

/* Shutdown the ASI client */
void asi_client_shutdown(void);

/* Send an inference request (blocking) */
kern_return_t asi_infer(const char *prompt,
                        const char *system_prompt,
                        const char *model_name,
                        float temperature,
                        int32_t max_tokens,
                        char **response_out,
                        uint32_t *response_len_out);

/* Send an inference request with full control */
kern_return_t asi_infer_full(asi_infer_request_t *request,
                             asi_infer_reply_t *reply);

/* Load a model */
kern_return_t asi_load_model(const char *model_path,
                             const char *model_name,
                             bool set_default,
                             bool gpu_load);

/* List loaded models */
kern_return_t asi_list_models(asi_model_info_t *models,
                              uint32_t *model_count);

/* Get ASI statistics */
kern_return_t asi_get_stats(asi_stats_t *stats);

/* Get configuration */
kern_return_t asi_get_config(asi_config_t *config);

/* Set configuration */
kern_return_t asi_set_config(asi_config_t *config);

/* Verify AI output signature */
kern_return_t asi_verify_output(const char *output,
                                const uint8_t *signature,
                                bool *valid_out);

/* Check if AI server is available */
bool asi_server_available(void);

#endif /* ASI_H */