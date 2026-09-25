/** @file ibus.c */
#include "ibus.h"

#include <string.h>

void ibus_parser_init(IBusParser *parser)
{
    if (parser == 0) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
}

static void resync(IBusParser *parser, uint8_t byte)
{
    parser->index = 0u;
    if (byte == IBUS_FRAME_LENGTH_BYTE) {
        parser->buffer[0] = byte;
        parser->index = 1u;
    }
}

bool ibus_parser_push(IBusParser *parser, uint8_t byte, IBusChannels *out)
{
    if (parser == 0 || out == 0) {
        return false;
    }

    if (parser->index == 0u) {
        if (byte == IBUS_FRAME_LENGTH_BYTE) {
            parser->buffer[parser->index++] = byte;
        }
        return false;
    }

    if (parser->index == 1u && byte != IBUS_SERVO_COMMAND) {
        parser->header_errors++;
        resync(parser, byte);
        return false;
    }

    parser->buffer[parser->index++] = byte;
    if (parser->index < IBUS_FRAME_LENGTH) {
        return false;
    }

    uint16_t expected = 0xFFFFu;
    for (uint8_t i = 0u; i < IBUS_FRAME_LENGTH - 2u; i++) {
        expected = (uint16_t)(expected - parser->buffer[i]);
    }
    const uint16_t received = (uint16_t)parser->buffer[30]
                            | ((uint16_t)parser->buffer[31] << 8);
    parser->index = 0u;
    if (received != expected) {
        parser->checksum_errors++;
        return false;
    }

    for (uint8_t channel = 0u; channel < IBUS_CHANNEL_COUNT; channel++) {
        const uint8_t offset = (uint8_t)(2u + channel * 2u);
        out->channels[channel] = (uint16_t)parser->buffer[offset]
                               | ((uint16_t)parser->buffer[offset + 1u] << 8);
    }
    parser->frames_ok++;
    return true;
}

float ibus_channel_unit(uint16_t value, uint16_t center,
                        uint16_t span, uint16_t deadband)
{
    if (span == 0u || deadband >= span) {
        return 0.0f;
    }
    int32_t delta = (int32_t)value - (int32_t)center;
    const int32_t magnitude = (delta < 0) ? -delta : delta;
    if (magnitude <= (int32_t)deadband) {
        return 0.0f;
    }
    float normalized = (float)(magnitude - (int32_t)deadband)
                     / (float)(span - deadband);
    if (normalized > 1.0f) {
        normalized = 1.0f;
    }
    return (delta < 0) ? -normalized : normalized;
}
