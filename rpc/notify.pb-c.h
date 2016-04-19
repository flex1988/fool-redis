/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: notify.proto */

#ifndef PROTOBUF_C_notify_2eproto__INCLUDED
#define PROTOBUF_C_notify_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1000000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1002001 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _Notify__Test Notify__Test;


/* --- enums --- */


/* --- messages --- */

struct  _Notify__Test
{
  ProtobufCMessage base;
  int32_t program;
  int32_t version;
  int32_t method;
  char *str;
};
#define NOTIFY__TEST__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&notify__test__descriptor) \
    , 0, 0, 0, NULL }


/* Notify__Test methods */
void   notify__test__init
                     (Notify__Test         *message);
size_t notify__test__get_packed_size
                     (const Notify__Test   *message);
size_t notify__test__pack
                     (const Notify__Test   *message,
                      uint8_t             *out);
size_t notify__test__pack_to_buffer
                     (const Notify__Test   *message,
                      ProtobufCBuffer     *buffer);
Notify__Test *
       notify__test__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   notify__test__free_unpacked
                     (Notify__Test *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Notify__Test_Closure)
                 (const Notify__Test *message,
                  void *closure_data);

/* --- services --- */

typedef struct _Notify__NotifyService_Service Notify__NotifyService_Service;
struct _Notify__NotifyService_Service
{
  ProtobufCService base;
  void (*notify)(Notify__NotifyService_Service *service,
                 const Notify__Test *input,
                 Notify__Test_Closure closure,
                 void *closure_data);
};
typedef void (*Notify__NotifyService_ServiceDestroy)(Notify__NotifyService_Service *);
void notify__notify_service__init (Notify__NotifyService_Service *service,
                                   Notify__NotifyService_ServiceDestroy destroy);
#define NOTIFY__NOTIFY_SERVICE__BASE_INIT \
    { &notify__notify_service__descriptor, protobuf_c_service_invoke_internal, NULL }
#define NOTIFY__NOTIFY_SERVICE__INIT(function_prefix__) \
    { NOTIFY__NOTIFY_SERVICE__BASE_INIT,\
      function_prefix__ ## notify  }
void notify__notify_service__notify(ProtobufCService *service,
                                    const Notify__Test *input,
                                    Notify__Test_Closure closure,
                                    void *closure_data);

/* --- descriptors --- */

extern const ProtobufCMessageDescriptor notify__test__descriptor;
extern const ProtobufCServiceDescriptor notify__notify_service__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_notify_2eproto__INCLUDED */
