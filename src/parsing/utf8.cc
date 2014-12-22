#include "parsing/utf8.hpp"

#include <string>
#include <string.h>

// #include "unicode/utypes.h"
#include "rdb_protocol/datum_string.hpp"

namespace utf8 {

static unsigned int HIGH_BIT = 0x80;
static unsigned int HIGH_TWO_BITS = 0xC0;
static unsigned int HIGH_THREE_BITS = 0xE0;
static unsigned int HIGH_FOUR_BITS = 0xF0;
static unsigned int HIGH_FIVE_BITS = 0xF8;

inline bool is_standalone(char c) {
    // 0xxxxxxx - ASCII character
    return (c & HIGH_BIT) == 0;
}

inline bool is_twobyte_start(char c) {
    // 110xxxxx - two character multibyte
    return (c & HIGH_THREE_BITS) == HIGH_TWO_BITS;
}

inline bool is_threebyte_start(char c) {
    // 1110xxxx - three character multibyte
    return (c & HIGH_FOUR_BITS) == HIGH_THREE_BITS;
}

inline bool is_fourbyte_start(char c) {
    // 11110xxx - four character multibyte
    return (c & HIGH_FIVE_BITS) == HIGH_FOUR_BITS;
}

inline bool is_continuation(char c) {
    // 10xxxxxx - continuation character
    return ((c & HIGH_TWO_BITS) == HIGH_BIT);
}

inline unsigned int extract_bits(char c, unsigned int bits) {
    return ((c & ~bits) & 0xFF);
}

inline unsigned int continuation_data(char c) {
    return extract_bits(c, HIGH_TWO_BITS);
}

inline unsigned int extract_and_shift(char c, unsigned int bits, unsigned int amount) {
    return extract_bits(c, bits) << amount;
}

template <class Iterator>
inline bool is_valid_internal(const Iterator & begin, const Iterator & end,
                              reason_t *reason) {
    char32_t codepoint;
    Iterator cbegin = begin;
    Iterator cend = begin;
    while (cbegin != end) {
        cend = next_codepoint(cbegin, end, &codepoint, reason);
        if (*(reason->explanation) != 0) {
            // need to correct offset, because `next_codepoint`
            // computes from `cbegin` not `begin`
            reason->position += cbegin - begin;
            return false;
        }
        cbegin = cend;
    }
    return true;
}

bool is_valid(const datum_string_t &str) {
    reason_t reason;
    return is_valid_internal(str.data(), str.data() + str.size(), &reason);
}

bool is_valid(const std::string &str) {
    reason_t reason;
    return is_valid_internal(str.begin(), str.end(), &reason);
}

bool is_valid(const char *start, const char *end) {
    reason_t reason;
    return is_valid_internal(start, end, &reason);
}

bool is_valid(const datum_string_t &str, reason_t *reason) {
    return is_valid_internal(str.data(), str.data() + str.size(), reason);
}

bool is_valid(const std::string &str, reason_t *reason) {
    return is_valid_internal(str.begin(), str.end(), reason);
}

bool is_valid(const char *start, const char *end, reason_t *reason) {
    return is_valid_internal(start, end, reason);
}

template <class Iterator>
void iterator_t<Iterator>::advance(void) {
    if (seen_end) return;
    if (position == end) {
         // would be advancing to the last position
        seen_end = true;
        last_seen = 0;
        reason.position = 0;
        reason.explanation = "";
        return;
    }

    Iterator new_position = next_codepoint(position, end, &last_seen, &reason);
    if (saw_error()) {
        // need to correct offset, because `next_codepoint` computes from `position`
        reason.position += position - start;
    }
    position = new_position;
}

template <class Iterator>
Iterator && fail(const char *explanation,
                const Iterator &start, Iterator &&position,
                char32_t *codepoint, reason_t *reason) {
    reason->explanation = explanation;
    reason->position = position - start - 1; // -1 corrects for postincrement
    // U+FFFD is the "replacement character", commonly used
    // for parsing errors like this
    *codepoint = U'\uFFFD';
    return std::move(position);
}

template <class Iterator>
const char *is_valid_continuation_byte(const Iterator &position, const Iterator &end) {
    if (position == end) {
        return "Expected continuation byte, saw end of string";
    } else if (!is_continuation(*position)) {
        return "Expected continuation byte, saw something else";
    } else {
        return 0;
    }
}

template <class Iterator>
Iterator && next_codepoint(const Iterator &start, const Iterator &end,
                          char32_t *codepoint, reason_t *reason) {
    Iterator position = start;
    reason->explanation = "";
    reason->position = 0;

    if (position == end) {
        return std::move(position);
    }

    char current = *position++;
    const char *explanation;

    if (is_standalone(current)) {
        // 0xxxxxxx - ASCII character
        *codepoint = current;
        return std::move(position);
    } else if (is_twobyte_start(current)) {
        // 110xxxxx - two character multibyte
        *codepoint = extract_and_shift(current, HIGH_THREE_BITS, 6);
        explanation = is_valid_continuation_byte(position, end);
        if (explanation != 0) {
            return fail(explanation, start, std::move(position), codepoint, reason);
        }
        *codepoint |= continuation_data(*position++);
        if (*codepoint < 0x0080) {
            // Not minimum bytes required to represent character, so wrong
            return fail("Overlong encoding seen",
                        start, std::move(position), codepoint, reason);
        } else {
            return std::move(position);
        }
    } else if (is_threebyte_start(current)) {
        // 1110xxxx - three character multibyte
        *codepoint = extract_and_shift(current, HIGH_FOUR_BITS, 12);
        explanation = is_valid_continuation_byte(position, end);
        if (explanation != 0) {
            return fail(explanation, start, std::move(position), codepoint, reason);
        }
        *codepoint |= continuation_data(*position++) << 6;
        explanation = is_valid_continuation_byte(position, end);
        if (explanation != 0) {
            return fail(explanation, start, std::move(position), codepoint, reason);
        }
        *codepoint |= continuation_data(*position++);
        if (*codepoint < 0x0800) {
            // Not minimum bytes required to represent character, so wrong
            return fail("Overlong encoding seen",
                        start, std::move(position), codepoint, reason);
        } else {
            return std::move(position);
        }
    } else if (is_fourbyte_start(current)) {
        // 11110xxx - four character multibyte
        *codepoint = extract_and_shift(current, HIGH_FIVE_BITS, 18);
        explanation = is_valid_continuation_byte(position, end);
        if (explanation != 0) {
            return fail(explanation, start, std::move(position), codepoint, reason);
        }
        *codepoint |= continuation_data(*position++) << 12;
        explanation = is_valid_continuation_byte(position, end);
        if (explanation != 0) {
            return fail(explanation, start, std::move(position), codepoint, reason);
        }
        *codepoint |= continuation_data(*position++) << 6;
        explanation = is_valid_continuation_byte(position, end);
        if (explanation != 0) {
            return fail(explanation, start, std::move(position), codepoint, reason);
        }
        *codepoint |= continuation_data(*position++);
        if (*codepoint < 0x10000) {
            // Not minimum bytes required to represent character, so wrong
            return fail("Overlong encoding seen",
                        start, std::move(position), codepoint, reason);
        } else if (*codepoint > 0x10FFFF) {
            // UTF-8 defined by RFC 3629 to end at U+10FFFF, as Unicode does
            return fail("Non-Unicode character encoded (beyond U+10FFFF)",
                        start, std::move(position), codepoint, reason);
        } else {
            return std::move(position);
        }
    } else {
        // high bit character outside of a surrogate context
        return fail("Invalid initial byte seen", start, std::move(position),
                    codepoint, reason);
    }
}

template <class Iterator>
Iterator && next_textual_element(const Iterator &begin, const Iterator &end,
                                const std::function<bool(char32_t)> &keep_going,
                                reason_t *reason) {
    char32_t codepoint;
    Iterator cbegin = begin;
    Iterator cend = begin;
    do {
        cend = next_codepoint(cbegin, end, &codepoint, reason);
        if (*(reason->explanation) != 0) {
            // need to correct offset, because `next_codepoint`
            // computes from `cbegin` not `begin`
            reason->position += cbegin - begin;
            // ensure we advance at least a bit
            if (cbegin == begin) {
                return std::move(cend);
            } else {
                return std::move(cbegin);
            }
        }
        if (cbegin != begin && !keep_going(codepoint)) {
            // first codepoint is free
            return std::move(cbegin);
        }
        cbegin = cend;
    } while (cbegin != end);
    return std::move(cbegin);
}

/*
inline bool is_combining_character(char32_t c) {
    return (U_GET_GC_MASK(c) & U_GC_M_MASK);
}
*/

template const char *&&
next_codepoint<const char *>(const char * const &start, const char * const &end,
                             char32_t *codepoint, reason_t *reason);
template std::string::iterator &&
next_codepoint<std::string::iterator>(const std::string::iterator &start,
                                      const std::string::iterator &end,
                                      char32_t *codepoint, reason_t *reason);
template std::string::const_iterator &&
next_codepoint<std::string::const_iterator>(const std::string::const_iterator &start,
                                            const std::string::const_iterator &end,
                                            char32_t *codepoint, reason_t *reason);

template const char *&&
next_textual_element<const char *>(const char * const &start, const char * const &end,
                                   const std::function<bool(char32_t)> &keep_going,
                                   reason_t *reason);
template std::string::iterator &&
next_textual_element<std::string::iterator>(const std::string::iterator &start,
                                            const std::string::iterator &end,
                                            const std::function<bool(char32_t)> &keep_going,
                                            reason_t *reason);
template std::string::const_iterator &&
next_textual_element<std::string::const_iterator>(const std::string::const_iterator &start,
                                            const std::string::const_iterator &end,
                                            const std::function<bool(char32_t)> &keep_going,
                                            reason_t *reason);

template class iterator_t<std::string::iterator>;
template class iterator_t<std::string::const_iterator>;
template class iterator_t<const char *>;

} // namespace utf8
