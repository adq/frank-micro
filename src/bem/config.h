#ifndef __INC_CONFIG_H
#define __INC_CONFIG_H

extern ALLEGRO_CONFIG *bem_cfg;

void config_load(void);
void config_save(void);

int get_config_int(const char *sect, const char *key, int idefault);
bool get_config_bool(const char *sect, const char *key, bool bdefault);
const char *get_config_string(const char *sect, const char *key, const char *sdefault);
#ifndef NO_USE_WRITABLE_CONFIG
void set_config_int(const char *sect, const char *key, int value);
void set_config_bool(const char *sect, const char *key, bool value);
void set_config_string(const char *sect, const char *key, const char *value);
#else
static inline void set_config_int(const char *sect, const char *key, int value) {}
static inline void set_config_bool(const char *sect, const char *key, bool value) {}
static inline void set_config_string(const char *sect, const char *key, const char *value) {}
#endif

extern int8_t curmodel;
#ifndef NO_USE_TUBE
extern int8_t selecttube;
#endif

#endif
