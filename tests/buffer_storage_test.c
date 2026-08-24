#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

typedef struct FakeBackend {
    uint64_t next_handle;
    int creates;
    int uploads;
    int destroys;
    int fail_upload;
    uint64_t last_upload_offset;
    uint64_t last_upload_size;
    uint8_t last_upload_data[8];
} FakeBackend;

static int fake_create_buffer(void* session,
                              uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;

    assert(size_bytes > 0u);
    ++backend->creates;
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session,
                              uint64_t buffer,
                              uint64_t offset,
                              const void* data,
                              uint64_t size_bytes)
{
    FakeBackend* backend = session;

    assert(buffer != 0u);
    assert(data != NULL);
    assert(size_bytes > 0u);
    assert(size_bytes <= sizeof(backend->last_upload_data));
    ++backend->uploads;
    backend->last_upload_offset = offset;
    backend->last_upload_size = size_bytes;
    memcpy(backend->last_upload_data, data, (size_t)size_bytes);
    return backend->fail_upload ? -1 : 0;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    FakeBackend* backend = session;

    assert(object != 0u);
    ++backend->destroys;
    return 0;
}

int main(void)
{
    FakeBackend backend = {0};

    assert(RINGL_BUFFER_SIZE == 0x8764u);
    assert(RINGL_BUFFER_USAGE == 0x8765u);

    RinGLRinGpuOpsV1 ops = {
        .struct_size = sizeof(ops),
        .api_version = RINGL_API_VERSION,
        .create_buffer = fake_create_buffer,
        .upload_buffer = fake_upload_buffer,
        .destroy_object = fake_destroy_object,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
    };
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .ringpu = &binding,
    };
    RinGLContext* context = NULL;
    uint32_t buffer = 0u;
    const uint8_t initial_data[4] = {1u, 2u, 3u, 4u};
    const uint8_t sub_data[2] = {8u, 9u};
    const uint8_t replacement_data[8] = {0u};

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    ringl_gen_buffers(1, &buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, buffer);

    ringl_buffer_data_from_bytes(RINGL_ARRAY_BUFFER, 4, initial_data,
                                 sizeof(initial_data), RINGL_STATIC_DRAW);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_buffer_size(RINGL_ARRAY_BUFFER) == 4u);
    assert(ringl_get_buffer_usage(RINGL_ARRAY_BUFFER) == RINGL_STATIC_DRAW);
    assert(backend.creates == 1);
    assert(backend.uploads == 1);
    assert(backend.destroys == 0);

    ringl_buffer_sub_data_from_bytes(RINGL_ARRAY_BUFFER, 1, 2, sub_data,
                                     sizeof(sub_data));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_buffer_size(RINGL_ARRAY_BUFFER) == 4u);
    assert(ringl_get_buffer_usage(RINGL_ARRAY_BUFFER) == RINGL_STATIC_DRAW);
    assert(backend.creates == 2);
    assert(backend.uploads == 2);
    assert(backend.destroys == 1);
    assert(backend.last_upload_offset == 0u);
    assert(backend.last_upload_size == 4u);
    assert(backend.last_upload_data[0] == 1u);
    assert(backend.last_upload_data[1] == 8u);
    assert(backend.last_upload_data[2] == 9u);
    assert(backend.last_upload_data[3] == 4u);

    ringl_buffer_data_from_bytes(RINGL_ARRAY_BUFFER, 4, initial_data,
                                 sizeof(initial_data) - 1u,
                                 RINGL_STATIC_DRAW);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(ringl_get_buffer_size(RINGL_ARRAY_BUFFER) == 4u);
    assert(backend.creates == 2);
    assert(backend.uploads == 2);
    assert(backend.destroys == 1);

    ringl_buffer_sub_data_from_bytes(RINGL_ARRAY_BUFFER, 1, 2, sub_data,
                                     sizeof(sub_data) - 1u);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(backend.creates == 2);
    assert(backend.uploads == 2);
    assert(backend.destroys == 1);

    ringl_buffer_sub_data(RINGL_ARRAY_BUFFER, 3, 2, sub_data);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(backend.creates == 2);
    assert(backend.uploads == 2);
    assert(backend.destroys == 1);

    backend.fail_upload = 1;
    ringl_buffer_sub_data(RINGL_ARRAY_BUFFER, 0, 2, sub_data);
    assert(ringl_get_error() == RINGL_OUT_OF_MEMORY);
    assert(ringl_get_buffer_size(RINGL_ARRAY_BUFFER) == 4u);
    assert(ringl_get_buffer_usage(RINGL_ARRAY_BUFFER) == RINGL_STATIC_DRAW);
    assert(backend.creates == 3);
    assert(backend.uploads == 3);
    assert(backend.destroys == 2);

    backend.fail_upload = 0;
    ringl_buffer_data(RINGL_ARRAY_BUFFER, 8, replacement_data,
                      RINGL_DYNAMIC_DRAW);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_buffer_size(RINGL_ARRAY_BUFFER) == 8u);
    assert(ringl_get_buffer_usage(RINGL_ARRAY_BUFFER) == RINGL_DYNAMIC_DRAW);
    assert(backend.creates == 4);
    assert(backend.uploads == 4);
    assert(backend.destroys == 3);

    ringl_buffer_data(RINGL_ARRAY_BUFFER, 0, NULL, RINGL_DYNAMIC_DRAW);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_buffer_size(RINGL_ARRAY_BUFFER) == 0u);
    assert(ringl_get_buffer_usage(RINGL_ARRAY_BUFFER) == RINGL_DYNAMIC_DRAW);
    assert(backend.destroys == 4);

    ringl_context_destroy(context);
    assert(backend.destroys == 4);
    return 0;
}
