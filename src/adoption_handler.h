#ifndef ADOPTION_HANDLER_H
#define ADOPTION_HANDLER_H

#include <Arduino.h>
#include "resonant_encryption.h"
#include "resonant_storage.h"
#include "resonant_frame.h"
#include "resonant_lr_radio.h"
#include "resonant_power_manager.h"
#include "resonant_log.h"

enum class TxContext {
    NONE,
    TELEMETRY,
    METRICS,
    COMMAND_RESPONSE,
    ACK,
    ADOPTION_ADVERTISE,
    ADOPTION_ACCEPT
};

class DeviceAdoptionHandler {
public:
    void init(ResonantEncryption* enc, ResonantStorage* store,
              ResonantFrame* frame, ResonantLRRadio* radio,
              ResonantPowerManager* power);

    // Process an incoming adoption request frame. Returns true if adoption succeeded.
    bool handleAdoptionRequest(const uint8_t* data, size_t dataLength,
                               const uint8_t* sourceID,
                               uint32_t& txSequenceNumber,
                               volatile TxContext& txContext);

    void sendAdoptionAdvertise(uint8_t sensorType, uint8_t hwVersion, uint8_t fwVersion,
                               uint32_t txSequenceNumber,
                               volatile TxContext& txContext);

    void sendAdoptionAccept(uint8_t destinationID[4],
                            uint32_t& txSequenceNumber,
                            volatile TxContext& txContext);

    void sendAdoptionAcceptCrypto(uint8_t destinationID[4],
                                  const uint8_t* challengeNonce,
                                  uint32_t& txSequenceNumber,
                                  volatile TxContext& txContext);

private:
    ResonantEncryption* _enc = nullptr;
    ResonantStorage* _store = nullptr;
    ResonantFrame* _frame = nullptr;
    ResonantLRRadio* _radio = nullptr;
    ResonantPowerManager* _power = nullptr;

    void getDeviceSensorId(uint8_t* sensorId);
};

#endif // ADOPTION_HANDLER_H
