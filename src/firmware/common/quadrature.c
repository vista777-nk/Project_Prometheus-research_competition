/** @file quadrature.c */
#include "quadrature.h"

/* 索引 = previous_state << 2 | current_state。
 * 合法 Gray 跳变为 ±1；不动为 0；两位同时翻转视为漏边沿/毛刺。 */
static const int8_t TRANSITION_DELTA[16] = {
     0, +1, -1,  0,
    -1,  0,  0, +1,
    +1,  0,  0, -1,
     0, -1, +1,  0
};

static uint8_t encode_state(bool phase_a, bool phase_b)
{
    return (uint8_t)(((phase_a ? 1u : 0u) << 1) | (phase_b ? 1u : 0u));
}

void quadrature_init(QuadratureDecoder *decoder, bool phase_a, bool phase_b)
{
    if (decoder == 0) {
        return;
    }
    decoder->count = 0;
    decoder->invalid_transitions = 0u;
    decoder->previous_state = encode_state(phase_a, phase_b);
    decoder->initialized = true;
}

void quadrature_update(QuadratureDecoder *decoder, bool phase_a, bool phase_b)
{
    if (decoder == 0) {
        return;
    }
    const uint8_t current = encode_state(phase_a, phase_b);
    if (!decoder->initialized) {
        quadrature_init(decoder, phase_a, phase_b);
        return;
    }

    const uint8_t transition = (uint8_t)((decoder->previous_state << 2) | current);
    const int8_t delta = TRANSITION_DELTA[transition];
    if (delta == 0 && current != decoder->previous_state) {
        decoder->invalid_transitions++;
    } else {
        decoder->count += delta;
    }
    decoder->previous_state = current;
}

uint16_t quadrature_count16(const QuadratureDecoder *decoder)
{
    return (decoder != 0) ? (uint16_t)decoder->count : 0u;
}
