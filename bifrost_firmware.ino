/**
 * @file bifrost_firmware.ino
 * @brief 180° Phase Inversion Management, 81st Harmonic Imprinting,
 *        and Coordinate Shift Control
 * @status: SYSTEM FREEZE ACTIVE -- HARDWARE DEPENDENCY LOCK ENFORCED
 * @3-6-9: 180° inversion, 5708.07 Hz imprinting, 6 pucks at 60°
 */

#include <Arduino.h>
#include <SPI.h>

// ============================================================================
// 3-6-9 HARMONIC CONSTANTS
// ============================================================================
#define BASE_CLOCK_HZ 70.47f
#define CARRIER_WAVE_HZ 634.23f          // 9 × 70.47 Hz
#define IMPRINTING_SCAN_HZ 5708.07f      // 81 × 70.47 Hz
#define PHASE_INVERSION_DEG 180.0f
#define MAX_PUCKS 9
#define DEFAULT_PUCKS 6
#define PIN_SPACING_DEG 60.0f
#define PHASE_MAX 16384                  // 14-bit DDS resolution

// ============================================================================
// PIN DEFINITIONS
// ============================================================================
// AD9959 DDS SPI Bus (Transmitter + Receiver Phase Control)
#define DDS_CS    5
#define DDS_SCK   18
#define DDS_SDI   23
#define DDS_SDO   19
#define DDS_UPDATE 4
#define DDS_RESET 2

// Puck Interface Pins (Up to 9 pucks)
const int puck_pins[MAX_PUCKS] = {8, 9, 10, 11, 12, 13, 14, 15, 16};

// Transmitter/Receiver Control
#define TRANSMITTER_ENABLE 17
#define RECEIVER_ENABLE 18
#define COORDINATE_LOCK 19

// Anchor Ring Temperature Sensor
#define ANCHOR_TEMP_SENSOR A0

// ============================================================================
// GLOBAL SYSTEM STATE REGISTERS
// ============================================================================
volatile uint16_t bf_transmitter_phases[MAX_PUCKS];
volatile uint16_t bf_receiver_phases[MAX_PUCKS];
volatile uint8_t bf_puck_status[MAX_PUCKS];  // 0=Empty, 1=Active
volatile float bf_coherence = 1.0f;
volatile float bf_anchor_temp = 20.0f;
volatile bool bf_coordinate_lock = false;
volatile uint32_t bf_imprinting_counter = 0;

// ============================================================================
// HARDWARE TIMER INTERRUPT (70.47 Hz Base Clock)
// ============================================================================
hw_timer_t * bifrost_timer = NULL;

void IRAM_ATTR bifrost_clock_interrupt() {
    bf_imprinting_counter++;
    
    // 1. Update transmitter and receiver phases for all active pucks
    for (int puck = 0; puck < MAX_PUCKS; puck++) {
        if (bf_puck_status[puck] == 1) {
            // Transmitter: 0° reference
            float tx_angle = puck * (2 * M_PI / MAX_PUCKS);
            bf_transmitter_phases[puck] = (uint16_t)((tx_angle / (2 * M_PI)) * PHASE_MAX) & 0x3FFF;
            
            // Receiver: 180° inversion
            float rx_angle = tx_angle + radians(PHASE_INVERSION_DEG);
            bf_receiver_phases[puck] = (uint16_t)((rx_angle / (2 * M_PI)) * PHASE_MAX) & 0x3FFF;
        }
    }
    
    // 2. SPI transfer to DDS
    digitalWrite(DDS_CS, LOW);
    for (int puck = 0; puck < MAX_PUCKS; puck++) {
        if (bf_puck_status[puck] == 1) {
            SPI.transfer16(bf_transmitter_phases[puck]);
            SPI.transfer16(bf_receiver_phases[puck]);
        }
    }
    digitalWrite(DDS_CS, HIGH);
    digitalWrite(DDS_UPDATE, HIGH);
    delayMicroseconds(1);
    digitalWrite(DDS_UPDATE, LOW);
    
    // 3. Check anchor ring temperature
    int raw = analogRead(ANCHOR_TEMP_SENSOR);
    bf_anchor_temp = 20.0f + (raw / 4095.0f) * 30.0f;  // 20-50°C range
}

// ============================================================================
// SETUP
// ============================================================================
