#include "adoption_handler.h"
#include <esp_efuse.h>

void DeviceAdoptionHandler::init(ResonantEncryption* enc, ResonantStorage* store,
                                  ResonantFrame* frame, ResonantLRRadio* radio,
                                  ResonantPowerManager* power) {
    _enc = enc;
    _store = store;
    _frame = frame;
    _radio = radio;
    _power = power;
}

bool DeviceAdoptionHandler::handleAdoptionRequest(const uint8_t* data, size_t dataLength,
                                                    const uint8_t* sourceID,
                                                    uint32_t& txSequenceNumber,
                                                    volatile TxContext& txContext) {
    LOG_I("Adoption request received!");
    LOG_I("Source ID: %02X:%02X:%02X:%02X",
        sourceID[0], sourceID[1], sourceID[2], sourceID[3]);

    // Spec payload: gateway_id(4) + ecdh_pubkey(64) + nonce(16) + ecdsa_sig(64) +
    //               tx_channel(1) + tx_interval(2) + cert_length(2) + gateway_der_cert(L)
    constexpr size_t MIN_ADOPTION_REQ_LEN = 4 + 64 + 16 + 64 + 1 + 2 + 2;

    if (_enc->isInitialized() && dataLength >= MIN_ADOPTION_REQ_LEN && data != nullptr) {
        size_t offset = 0;
        const uint8_t* gatewayId       = data + offset;  offset += 4;
        const uint8_t* gatewayPubKey   = data + offset;  offset += 64;
        const uint8_t* challengeNonce  = data + offset;  offset += 16;
        const uint8_t* gatewaySig      = data + offset;  offset += 64;
        uint8_t txChannel              = data[offset];    offset += 1;
        uint16_t txInterval            = ((uint16_t)data[offset] << 8) | data[offset + 1]; offset += 2;
        uint16_t certLength            = ((uint16_t)data[offset] << 8) | data[offset + 1]; offset += 2;

        LOG_I("Gateway ID: %02X:%02X:%02X:%02X",
            gatewayId[0], gatewayId[1], gatewayId[2], gatewayId[3]);
        LOG_D("TX Channel: %u, TX Interval: %u sec", txChannel, txInterval);

        const uint8_t* gatewayCert = nullptr;
        if (certLength > 0 && offset + certLength <= dataLength) {
            gatewayCert = data + offset;
        }

        // Verify gateway certificate chain
        bool certVerified = false;
        if (gatewayCert != nullptr && certLength > 0) {
            if (_enc->verifyCertChain(gatewayCert, certLength)) {
                LOG_I("Gateway certificate chain verified");
                certVerified = true;
            } else {
                LOG_W("Gateway certificate chain verification failed");
            }
        } else {
            LOG_D("No gateway cert in payload, skipping chain verification");
        }

        // Extract gateway long-term public key from cert and verify ECDSA signature
        bool sigVerified = false;
        if (certVerified && gatewayCert != nullptr) {
            uint8_t gwLongTermPubKey[ResonantEncryption::P256_PUBKEY_SIZE];
            if (_enc->extractPubKeyFromCert(gatewayCert, certLength, gwLongTermPubKey)) {
                uint8_t signedData[64 + 16];
                memcpy(signedData, gatewayPubKey, 64);
                memcpy(signedData + 64, challengeNonce, 16);
                if (_enc->verifySignature(gwLongTermPubKey, signedData, sizeof(signedData), gatewaySig)) {
                    LOG_I("Gateway ECDSA signature verified");
                    sigVerified = true;
                } else {
                    LOG_W("Gateway ECDSA signature verification failed");
                }
            } else {
                LOG_W("Failed to extract public key from gateway cert");
            }
        } else if (!certVerified && gatewayCert != nullptr) {
            LOG_W("Skipping signature verification (cert not verified)");
        } else {
            LOG_D("No cert available, proceeding without attestation (dev mode)");
            sigVerified = true;
        }

        if (!sigVerified && gatewayCert != nullptr) {
            LOG_E("Adoption rejected: mutual attestation failed");
            _power->markRxComplete();
            return false;
        }

        // ECDH + HKDF
        _store->setParentId(gatewayId);
        txSequenceNumber = 1;
        LOG_I("Parent ID stored, sequence number reset");

        uint8_t sharedSecret[ResonantEncryption::SHARED_SECRET_SIZE];
        uint8_t sensorId[4];
        getDeviceSensorId(sensorId);

        if (_enc->performECDH(gatewayPubKey, sharedSecret)) {
            if (_enc->deriveSessionKey(sharedSecret, sensorId, 4, gatewayId, 4)) {
                LOG_I("Session key derived from adoption handshake");
            } else {
                LOG_W("Session key derivation failed");
            }
            memset(sharedSecret, 0, sizeof(sharedSecret));
        } else {
            LOG_W("ECDH key agreement failed");
        }

        _power->markRxComplete();
        delay(150);
        uint8_t gwIdCopy[4];
        memcpy(gwIdCopy, gatewayId, 4);
        sendAdoptionAcceptCrypto(gwIdCopy, challengeNonce, txSequenceNumber, txContext);
    } else {
        _store->setParentId(sourceID);
        txSequenceNumber = 1;
        LOG_I("Parent ID stored (non-crypto adoption), sequence number reset");
        if (dataLength > 0 && data != nullptr) {
            LOG_D("Network config received: %zu bytes", dataLength);
        }
        _power->markRxComplete();
        delay(150);
        uint8_t srcCopy[4];
        memcpy(srcCopy, sourceID, 4);
        sendAdoptionAccept(srcCopy, txSequenceNumber, txContext);
    }

    return true;
}

void DeviceAdoptionHandler::sendAdoptionAdvertise(uint8_t sensorType, uint8_t hwVersion,
                                                    uint8_t fwVersion,
                                                    uint32_t txSequenceNumber,
                                                    volatile TxContext& txContext) {
    LOG_I("Sending discovery/adoption advertise frame...");

    uint8_t deviceCert[ResonantEncryption::MAX_CERT_SIZE];
    size_t deviceCertLen = ResonantEncryption::MAX_CERT_SIZE;
    const uint8_t* certPtr = nullptr;
    size_t certLen = 0;

    if (_enc->isInitialized() && _enc->getDeviceCert(deviceCert, &deviceCertLen)) {
        certPtr = deviceCert;
        certLen = deviceCertLen;
        LOG_I("Including device cert (%zu bytes) in discovery", certLen);
    }

    uint8_t capabilities = 0x01;
    uint16_t txInterval = _store->getUInt16(StorageKeys::SLEEP_DURATION, 5);

    FrameData frame = _frame->buildDiscoveryFrame(
        sensorType, hwVersion, fwVersion,
        capabilities, txInterval, certPtr, certLen, txSequenceNumber);

    txContext = TxContext::ADOPTION_ADVERTISE;
    uint8_t broadcastId[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    _radio->send(frame.frame, frame.size, broadcastId, false);
    delete[] frame.frame;
}

void DeviceAdoptionHandler::sendAdoptionAccept(uint8_t destinationID[4],
                                                uint32_t& txSequenceNumber,
                                                volatile TxContext& txContext) {
    LOG_I("Sending adoption accept to %02X:%02X:%02X:%02X",
        destinationID[0], destinationID[1], destinationID[2], destinationID[3]);
    uint8_t acceptData[1] = {0xAA};
    uint8_t options = ResonantFrame::buildOptionsV1(false);
    FrameData frame = _frame->buildAdoptionAcceptFrame(acceptData, 1, destinationID, options, txSequenceNumber);
    txSequenceNumber++;
    txContext = TxContext::ADOPTION_ACCEPT;
    _radio->send(frame.frame, frame.size);
    delete[] frame.frame;
}

void DeviceAdoptionHandler::sendAdoptionAcceptCrypto(uint8_t destinationID[4],
                                                      const uint8_t* challengeNonce,
                                                      uint32_t& txSequenceNumber,
                                                      volatile TxContext& txContext) {
    LOG_I("Sending crypto adoption accept to %02X:%02X:%02X:%02X",
        destinationID[0], destinationID[1], destinationID[2], destinationID[3]);

    uint8_t devicePubKey[ResonantEncryption::P256_PUBKEY_SIZE];
    if (!_enc->getPublicKey(devicePubKey)) {
        LOG_W("getPublicKey failed, falling back to plain accept");
        sendAdoptionAccept(destinationID, txSequenceNumber, txContext);
        return;
    }

    uint8_t toSign[64 + 16];
    memcpy(toSign, devicePubKey, 64);
    memcpy(toSign + 64, challengeNonce, 16);

    uint8_t signature[ResonantEncryption::P256_SIG_SIZE];
    if (!_enc->signData(toSign, sizeof(toSign), signature)) {
        LOG_W("signData failed, falling back to plain accept");
        sendAdoptionAccept(destinationID, txSequenceNumber, txContext);
        return;
    }

    uint8_t encryptedNonce[16];
    uint8_t iv[ResonantEncryption::GCM_IV_SIZE];
    uint8_t tag[ResonantEncryption::GCM_TAG_SIZE];

    if (!_enc->encryptGCM(challengeNonce, 16, nullptr, 0,
                           encryptedNonce, iv, tag)) {
        LOG_W("encryptGCM nonce proof failed, falling back to plain accept");
        sendAdoptionAccept(destinationID, txSequenceNumber, txContext);
        return;
    }

    uint8_t deviceCert[ResonantEncryption::MAX_CERT_SIZE];
    size_t deviceCertLen = ResonantEncryption::MAX_CERT_SIZE;
    bool hasCert = _enc->getDeviceCert(deviceCert, &deviceCertLen);
    if (!hasCert) {
        deviceCertLen = 0;
        LOG_D("No device cert available, sending without cert");
    }

    size_t payloadSize = 64 + 64 + 12 + 16 + 16 + 2 + deviceCertLen;
    uint8_t* payload = new uint8_t[payloadSize];
    if (payload == nullptr) {
        LOG_E("Payload allocation failed");
        sendAdoptionAccept(destinationID, txSequenceNumber, txContext);
        return;
    }

    size_t offset = 0;
    memcpy(payload + offset, devicePubKey, 64);       offset += 64;
    memcpy(payload + offset, signature, 64);          offset += 64;
    memcpy(payload + offset, iv, 12);                 offset += 12;
    memcpy(payload + offset, encryptedNonce, 16);     offset += 16;
    memcpy(payload + offset, tag, 16);                offset += 16;
    payload[offset] = (deviceCertLen >> 8) & 0xFF;    offset += 1;
    payload[offset] = deviceCertLen & 0xFF;           offset += 1;
    if (deviceCertLen > 0) {
        memcpy(payload + offset, deviceCert, deviceCertLen);
    }

    uint8_t options = ResonantFrame::buildOptionsV1(false);

    FrameData frame = _frame->buildAdoptionAcceptFrame(
        payload, payloadSize, destinationID, options, txSequenceNumber);
    txSequenceNumber++;
    txContext = TxContext::ADOPTION_ACCEPT;
    _radio->send(frame.frame, frame.size, destinationID, false);
    delete[] frame.frame;

    delete[] payload;
    LOG_I("Crypto adoption accept sent (%zu bytes: pubkey + sig + nonce proof + cert)", payloadSize);
}

void DeviceAdoptionHandler::getDeviceSensorId(uint8_t* sensorId) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    memcpy(sensorId, mac + 2, 4);
}
