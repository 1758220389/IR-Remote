#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H
#include "ui.h"
#define CONFIG_STORE_BANK_SIZE 8192u
#define CONFIG_STORE_HEADER_SIZE 32u
#define CONFIG_STORE_SIGNAL_SIZE (6u + 2u * UI_SIGNAL_MAX_TIMINGS)
#define CONFIG_STORE_PROFILE_SIZE (UI_KEY_COUNT * CONFIG_STORE_SIGNAL_SIZE + 2u)
#define CONFIG_STORE_PAYLOAD_SIZE (UI_PROFILE_COUNT * CONFIG_STORE_PROFILE_SIZE)
/* Synchronous, foreground-only. read/program return nonzero on success.
 * Offset is within bank 0/1. erase_bank erases exactly eight 1KB pages.
 * Failed writes may be partial. Source profiles must stay stable throughout.
 * No heap, state shadow, or large local buffers are used.
 * read must be stable/reliable while Load validates then copies its source. */
typedef struct {
    void *context;
    int (*read)(void *context, unsigned bank, uint32_t offset,
                void *destination, uint32_t size);
    int (*erase_bank)(void *context, unsigned bank);
    int (*program_halfword)(void *context, unsigned bank, uint32_t offset,
                            uint16_t value);
} ConfigStoreBackend;
typedef struct { ConfigStoreBackend backend; } ConfigStore;
void ConfigStore_Init(ConfigStore *store, const ConfigStoreBackend *backend);
int ConfigStore_Load(ConfigStore *store, UiProfile profiles[UI_PROFILE_COUNT],
                     uint8_t *active);
int ConfigStore_Save(ConfigStore *store,
                     const UiProfile profiles[UI_PROFILE_COUNT], uint8_t active);
/* All int APIs return 1 on success, 0 on failure/no valid record.
 * SaveReplacement streams replacement in place of profiles[index] without
 * modifying source RAM. NULL replacement is plain Save; index is ignored.
 * All profile indices and active are zero-based (0..2).
 * Returns 0 without erasing for invalid source or a bank-scan read error. */
int ConfigStore_SaveReplacement(ConfigStore *store,
                     const UiProfile profiles[UI_PROFILE_COUNT], uint8_t active,
                     uint8_t replacement_index, const UiProfile *replacement);
/* Wire format, all integers little-endian, schema 2:
 * 0 magic u32 0x43465249, 4 schema u16, 6 header-size u16,
 * 8 generation u32 (serial arithmetic, rollover supported), 12 payload-size u32,
 * 16 timing-capacity u16, 18 profile-count u8, 19 key-count u8,
 * 20 active u16, 22 sizeof(UiProfile) u16, 24 CRC32 u32,
 * 28 reserved u16=0, 30 commit u16=0xA55A (programmed LAST).
 * CRC32 IEEE reflected polynomial EDB88320, initial/final XOR FFFFFFFF,
 * covers header bytes 0..23, reserved bytes 28..29, then entire payload.
 * Payload: each profile's keys as length u16, carrier_khz u16, format u16,
 * all timing slots u16, followed by bound_mask u8 and kind u8. No raw structs.
 * At capacity 256: signal 518 bytes, profile 2592 bytes, bank record 7808.
 * Schema 1 is rejected, NOT migrated: its signal layout is incompatible.
 * Back up old banks before upgrading; unsupported records are not defaults.
 * Schema/capacity/counts/sizeof must match; no packing/endianness assumptions.
 * Invalid mask bits, active, kind (>2), format (>1), or bound zero-length
 * rejects a candidate. RAW lengths are 0..capacity. Compact Bosch uses 38kHz,
 * length 12 (fixed OFF) or 18 (normal), byte-valued live slots, complements,
 * identical first two blocks, fixed headers and normal-frame checksum.
 * Dynamic kinds 1/2 require all five bindings and a valid 18-byte Bosch key0
 * ON template; kind 2 stores power OFF separately, not an OFF key0 template.
 * Kind 0 permits mixed raw/compact keys. Unused slots are preserved verbatim.
 * Load validates CRC and semantics BEFORE touching outputs. Returns 0 for
 * absent/invalid data, no defaults policy. A read failure during copy returns
 * 0 and may partially update outputs: reliable memory-mapped reads required.
 * Save validates source before erasing. Old newest bank never erased; target
 * is verified before/after commit. On failed/torn writes old bank survives.
 * Equal/half-range-ambiguous generations prefer bank 0 deterministically.
 */
#endif
