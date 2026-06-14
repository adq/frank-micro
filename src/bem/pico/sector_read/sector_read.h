/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_SECTOR_READ_H
#define B_EM_PICO_SECTOR_READ_H

#include "pico/types.h"
#include "pico/util/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_COMPRESSED_SECTOR_READ
// statically allocate all state (this is the only option right now!)
#define USE_SINGLE_COMPRESSED_SECTOR_READ_INSTANCE
#define MAX_COMPRESSED_SECTOR_READ_PEEK 128
#endif

struct sector_read;

struct sector_buffer {
    struct mem_buffer buffer;
    uint8_t in_use;
};
/**
 * @param sr
 * @param sector the sector num
 * @param length the amount of data in the sector required
 * @param status a pointer to receive a status code if the rc == NULL
 * @return
 */
typedef struct sector_buffer *(*sector_read_acquire_buffer_func)(struct sector_read *sr, uint32_t sector);
typedef void (*sector_read_release_buffer_func)(struct sector_read *sr, struct sector_buffer *buffer);
typedef uint (*sector_read_check_available_func)(struct sector_read *sr, struct sector_buffer *buffer, uint wanted,
                                                 uint32_t timeout_ms);
typedef void (*sector_read_close_func)(struct sector_read *sector_read);

#ifndef NDEBUG
// only used by debug atm
typedef uint32_t (*sector_read_pos_func)(struct sector_read *sector_read);
#endif

enum sector_read_type {
    memory,
    compressed,
    xip
};

struct sector_read_funcs {
    sector_read_acquire_buffer_func acquire_buffer;
    sector_read_release_buffer_func release_buffer;
    sector_read_check_available_func check_available;
    sector_read_close_func close;
#ifndef NDEBUG
    enum sector_read_type type;
#endif
};

struct sector_read {
    const struct sector_read_funcs *funcs;
    size_t sector_size;
    size_t sector_count;
};

struct memory_sector_read;
struct compressed_sector_read;

struct sector_read *memory_sector_read_open(const uint8_t *buffer, size_t sector_size, size_t sector_count,
                                            bool free_buffer);

struct sector_read *xip_sector_read_open(const uint8_t *buffer, size_t sector_size, size_t sector_count);

#ifdef ENABLE_COMPRESSED_SECTOR_READ

struct sector_read *compressed_sector_read_open(struct sector_read *underlying, size_t uncompressed_size);

#endif

inline static struct sector_buffer *sector_read_acquire_buffer(struct sector_read *sr, uint32_t sector) {
    return sr->funcs->acquire_buffer(sr, sector);
}

inline static void sector_read_release_buffer(struct sector_read *sr, struct sector_buffer *buffer) {
    return sr->funcs->release_buffer(sr, buffer);
}

inline static uint sector_read_ensure_check_available(struct sector_read *sr, struct sector_buffer *buffer, uint wanted,
                                                      uint32_t timeout_us) {
    return sr->funcs->check_available(sr, buffer, wanted, timeout_us);
}

inline static void sector_read_close(struct sector_read *sector_read) {
    sector_read->funcs->close(sector_read);
}

#ifdef __cplusplus
}
#endif

#endif //SOFTWARE_SECTOR_READ_H
