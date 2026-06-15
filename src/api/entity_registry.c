#include "esphome_api.h"
#include "esphome_api.pb.h"

#include <string.h>
#include <stdio.h>

#define MAX_ENTITIES 64

/**
 * Represents a single entity mapping retrieved from the ESPHome device.
 * Used to translate friendly string IDs into the 32-bit keys required by the protocol.
 */
typedef struct {
    uint32_t key;           // The 32-bit numeric key assigned by the ESPHome device
    char object_id[64];     // The human-readable string ID or name of the entity
    char state[64];         // The most recently received state string
    uint32_t legacy_type;   // The ESPH_MSG_* type ID (e.g. ESPH_MSG_LIST_ENTITIES_SWITCH_RESPONSE)
} entity_entry_t;

static entity_entry_t registry[MAX_ENTITIES];
static size_t registry_count = 0;

// ---------------------------------------------------------------------------
// Store entity info
// ---------------------------------------------------------------------------
/**
 * Add a new entity to the internal registry.
 * This is called automatically during the esph_wait_list_entities_done loop.
 *
 * @param object_id   The string ID or name of the entity
 * @param key         The numeric key assigned by the device
 * @param legacy_type The protobuf message type ID that defined this entity
 */
void esph_registry_add(const char *object_id, uint32_t key, uint32_t legacy_type)
{
    if (registry_count >= MAX_ENTITIES)
        return;

    registry[registry_count].key = key;
    strncpy(registry[registry_count].object_id,
            object_id,
            sizeof(registry[registry_count].object_id)-1);
    registry[registry_count].state[0] = '\0'; // Initialize empty
    registry[registry_count].legacy_type = legacy_type;

    registry_count++;
}

// ---------------------------------------------------------------------------
// Update entity state
// ---------------------------------------------------------------------------
/**
 * Updates the cached string representation of an entity's current state.
 * This is called automatically by `esph_run_step` whenever a StateResponse
 * (like SensorStateResponse or SwitchStateResponse) is received.
 *
 * @param key       The 32-bit numeric key of the entity to update
 * @param state_str The new state as a formatted string (e.g. "ON", "23.50")
 */
void esph_registry_update_state(uint32_t key, const char *state_str)
{
    for (size_t i = 0; i < registry_count; i++) {
        if (registry[i].key == key) {
            strncpy(registry[i].state, state_str, sizeof(registry[i].state) - 1);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Print all entities cleanly
// ---------------------------------------------------------------------------
void esph_print_all_entities(void)
{
    printf("\n==================================================\n");
    printf("              ESPHOME ENTITY LIST\n");
    printf("==================================================\n");
    for (size_t i = 0; i < registry_count; i++) {
        const char *type_str = "Unknown";
        switch(registry[i].legacy_type) {
            case 12: type_str = "BinarySensor"; break; // ESPH_MSG_LIST_ENTITIES_BINARY_SENSOR_RESPONSE
            case 14: type_str = "Cover"; break;
            case 16: type_str = "Sensor"; break;
            case 17: type_str = "Switch"; break;
            case 18: type_str = "TextSensor"; break;
        }
        
        printf("[%12s] %-30s : %s\n", type_str, registry[i].object_id, 
               registry[i].state[0] ? registry[i].state : "(No State Received)");
    }
    printf("==================================================\n\n");
}

// ---------------------------------------------------------------------------
// Lookup key by entity_id
// ---------------------------------------------------------------------------
/**
 * Lookup the numeric key for a given entity string ID.
 * This is required to send commands (like SwitchCommandRequest) to the device.
 *
 * @param entity_id The string ID or name of the entity
 * @return The 32-bit numeric key, or 0 if not found
 */
uint32_t esph_registry_lookup_key(const char *entity_id)
{
    // If entity_id has a dot (e.g. "switch.lamp"), skip the prefix.
    // Otherwise, treat the entire string as the target name.
    const char *target = entity_id;
    const char *dot = strchr(entity_id, '.');
    if (dot) {
        target = dot + 1;
    }

    for (size_t i = 0; i < registry_count; i++) {
        if (strcmp(registry[i].object_id, target) == 0)
            return registry[i].key;
    }

    return 0;
}
