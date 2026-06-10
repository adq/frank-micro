/*
 * frank-micro — BBC Micro for RP2350
 * disc_stub.c — Stubs for unsupported disc formats.
 */
#include "disc_fsd.h"
#include "disc_rfi.h"
#include "disc_scp.h"
#include "disc_dfi.h"
#include "disc_kryo.h"

void disc_fsd_load(struct disc_struct* p, int has_file_name) { (void)p; (void)has_file_name; }
void disc_rfi_load(struct disc_struct* p)                    { (void)p; }
void disc_scp_load(struct disc_struct* p)                    { (void)p; }
void disc_dfi_load(struct disc_struct* p)                    { (void)p; }
void disc_kryo_load(struct disc_struct* p, const char* f)    { (void)p; (void)f; }
