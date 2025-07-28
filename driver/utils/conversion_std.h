#pragma once

#include "driver/utils/string_pool.h"

#include <codecvt>
#include <locale>
#include <string>
#include <type_traits>

class UnicodeConversionContext {
public:
    StringPool string_pool{10};

//  std::locale source_locale;
//  std::locale destination_locale;
#if !defined(_MSC_VER) || _MSC_VER >= 1920
    std::wstring_convert<std::codecvt_utf8<char16_t>, char16_t> UCS2_converter_char16;
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> UCS2_converter_char32;
#endif
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> UCS2_converter_wchar;
};

// In future, this will become an aggregate context that will do proper date/time, etc., conversions also.
using DefaultConversionContext = UnicodeConversionContext;

inline std::string toUTF8(const char * src, const std::locale & locale, SQLLEN length = SQL_NTS) {

    // TODO: implement and use conversion from the specified locale.

    throw std::runtime_error("not implemented");
}

inline decltype(auto) toUTF8(const signed char * src, const std::locale & locale, SQLLEN length = SQL_NTS) {
    return toUTF8(reinterpret_cast<const char *>(src), locale, length);
}

inline decltype(auto) toUTF8(const unsigned char * src, const std::locale & locale, SQLLEN length = SQL_NTS) {
    return toUTF8(reinterpret_cast<const char *>(src), locale, length);
}

inline std::string toUTF8(const char * src, SQLLEN length = SQL_NTS) {
    if (!src || (length != SQL_NTS && length <= 0))
        return {};

    try {
        // Workaround for UnixODBC Unicode client vs ANSI driver string encoding issue:
        // strings may be reported with a fixed length that also includes a trailing null character.
#if defined(WORKAROUND_ENABLE_TRIM_TRAILING_NULL)
        if (src && length > 0 && src[length - 1] == '\0')
            --length;
#endif

        // Determine actual length
        std::size_t actual_length = (length == SQL_NTS ? std::strlen(src) : static_cast<std::size_t>(length));
        
        if (actual_length == 0) {
            return {};
        }

        // Unicode replacement character in UTF-8: 0xEF 0xBF 0xBD
        const char replacement_char[] = "\xEF\xBF\xBD";
        
        std::string result;
        result.reserve(actual_length * 2); // Reserve extra space for potential replacements
        
        for (std::size_t i = 0; i < actual_length; ) {
            unsigned char ch = static_cast<unsigned char>(src[i]);
            
            // Handle null bytes (replace with replacement character)
            if (ch == 0) {
                result += replacement_char;
                ++i;
                continue;
            }
            
            // Handle control characters (except tab, newline, carriage return)
            if (ch < 0x20 && ch != 0x09 && ch != 0x0A && ch != 0x0D) {
                result += replacement_char;
                ++i;
                continue;
            }
            
            // Handle ASCII characters (0x20-0x7F and allowed control chars)
            if (ch <= 0x7F) {
                result += static_cast<char>(ch);
                ++i;
                continue;
            }
            
            // Handle UTF-8 multi-byte sequences
            if ((ch & 0xE0) == 0xC0) {
                // 2-byte sequence
                if (i + 1 < actual_length && 
                    (static_cast<unsigned char>(src[i + 1]) & 0xC0) == 0x80) {
                    // Valid 2-byte UTF-8 sequence
                    result += static_cast<char>(ch);
                    result += src[i + 1];
                    i += 2;
                } else {
                    // Invalid sequence, replace
                    result += replacement_char;
                    ++i;
                }
            } else if ((ch & 0xF0) == 0xE0) {
                // 3-byte sequence
                if (i + 2 < actual_length && 
                    (static_cast<unsigned char>(src[i + 1]) & 0xC0) == 0x80 &&
                    (static_cast<unsigned char>(src[i + 2]) & 0xC0) == 0x80) {
                    // Valid 3-byte UTF-8 sequence
                    result += static_cast<char>(ch);
                    result += src[i + 1];
                    result += src[i + 2];
                    i += 3;
                } else {
                    // Invalid sequence, replace
                    result += replacement_char;
                    ++i;
                }
            } else if ((ch & 0xF8) == 0xF0) {
                // 4-byte sequence
                if (i + 3 < actual_length && 
                    (static_cast<unsigned char>(src[i + 1]) & 0xC0) == 0x80 &&
                    (static_cast<unsigned char>(src[i + 2]) & 0xC0) == 0x80 &&
                    (static_cast<unsigned char>(src[i + 3]) & 0xC0) == 0x80) {
                    // Valid 4-byte UTF-8 sequence
                    result += static_cast<char>(ch);
                    result += src[i + 1];
                    result += src[i + 2];
                    result += src[i + 3];
                    i += 4;
                } else {
                    // Invalid sequence, replace
                    result += replacement_char;
                    ++i;
                }
            } else {
                // Invalid UTF-8 start byte, replace
                result += replacement_char;
                ++i;
            }
        }
        
        return result;
    } catch (const std::exception&) {
        // Ultimate fallback - return safe replacement characters for the entire string
        if (length == SQL_NTS) {
            return "\xEF\xBF\xBD"; // Single replacement character
        } else {
            std::string fallback;
            fallback.reserve(static_cast<std::size_t>(length) * 3);
            for (SQLLEN i = 0; i < length; ++i) {
                fallback += "\xEF\xBF\xBD";
            }
            return fallback;
        }
    } catch (...) {
        // Absolute fallback for any unexpected exception
        return "\xEF\xBF\xBD";
    }
}

inline std::string toUTF8(const char16_t * src, SQLLEN length = SQL_NTS) {
    if (!src || (length != SQL_NTS && length <= 0))
        return {};

    try {
        std::wstring_convert<std::codecvt_utf8<char16_t>, char16_t> convert;
        
        if (length == SQL_NTS) {
            // Calculate length manually to handle potential null characters
            std::size_t actual_length = 0;
            while (src[actual_length] != 0) {
                ++actual_length;
            }
            if (actual_length == 0) {
                return {};
            }
            
            // Use range constructor for safety
            std::basic_string<char16_t> input(src, actual_length);
            return convert.to_bytes(input);
        } else {
            // Use specified length, but validate for null characters
            std::basic_string<char16_t> input;
            input.reserve(static_cast<std::size_t>(length));
            
            for (SQLLEN i = 0; i < length; ++i) {
                char16_t ch = src[i];
                if (ch == 0) {
                    // Replace null with replacement character (U+FFFD)
                    input += static_cast<char16_t>(0xFFFD);
                } else {
                    input += ch;
                }
            }
            
            return convert.to_bytes(input);
        }
    } catch (const std::exception&) {
        // Fallback: create a safe ASCII version with replacement characters
        std::string result;
        std::size_t actual_length = (length == SQL_NTS) ? std::char_traits<char16_t>::length(src) : static_cast<std::size_t>(length);
        result.reserve(actual_length * 3); // UTF-8 replacement char is 3 bytes
        
        for (std::size_t i = 0; i < actual_length; ++i) {
            char16_t ch = src[i];
            if (ch == 0 || ch > 0x7F) {
                // Use UTF-8 replacement character for null or non-ASCII
                result += "\xEF\xBF\xBD";
            } else if (ch < 0x20 && ch != 0x09 && ch != 0x0A && ch != 0x0D) {
                // Replace control characters
                result += "\xEF\xBF\xBD";
            } else {
                // Safe ASCII character
                result += static_cast<char>(ch);
            }
        }
        
        return result;
    }
}

inline std::string toUTF8(const char32_t * src, SQLLEN length = SQL_NTS) {
    if (!src || (length != SQL_NTS && length <= 0))
        return {};

    try {
        std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> convert;
        
        if (length == SQL_NTS) {
            // Calculate length manually and validate
            std::size_t actual_length = 0;
            while (src[actual_length] != 0) {
                ++actual_length;
            }
            if (actual_length == 0) {
                return {};
            }
            
            std::basic_string<char32_t> input;
            input.reserve(actual_length);
            
            for (std::size_t i = 0; i < actual_length; ++i) {
                char32_t ch = src[i];
                if (ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) {
                    // Invalid Unicode code point, replace with replacement character
                    input += static_cast<char32_t>(0xFFFD);
                } else {
                    input += ch;
                }
            }
            
            return convert.to_bytes(input);
        } else {
            std::basic_string<char32_t> input;
            input.reserve(static_cast<std::size_t>(length));
            
            for (SQLLEN i = 0; i < length; ++i) {
                char32_t ch = src[i];
                if (ch == 0 || ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) {
                    // Replace invalid or null characters
                    input += static_cast<char32_t>(0xFFFD);
                } else {
                    input += ch;
                }
            }
            
            return convert.to_bytes(input);
        }
    } catch (const std::exception&) {
        // Fallback
        std::string result;
        std::size_t actual_length = (length == SQL_NTS) ? std::char_traits<char32_t>::length(src) : static_cast<std::size_t>(length);
        result.reserve(actual_length * 3);
        
        for (std::size_t i = 0; i < actual_length; ++i) {
            char32_t ch = src[i];
            if (ch == 0 || ch > 0x7F || ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) {
                result += "\xEF\xBF\xBD";
            } else if (ch < 0x20 && ch != 0x09 && ch != 0x0A && ch != 0x0D) {
                result += "\xEF\xBF\xBD";
            } else {
                result += static_cast<char>(ch);
            }
        }
        
        return result;
    }
}

inline std::string toUTF8(const wchar_t * src, SQLLEN length = SQL_NTS) {
    if (!src || (length != SQL_NTS && length <= 0))
        return {};

    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> convert;
        
        if (length == SQL_NTS) {
            // Calculate length manually and validate
            std::size_t actual_length = 0;
            while (src[actual_length] != 0) {
                ++actual_length;
            }
            if (actual_length == 0) {
                return {};
            }
            
            std::basic_string<wchar_t> input;
            input.reserve(actual_length);
            
            for (std::size_t i = 0; i < actual_length; ++i) {
                wchar_t ch = src[i];
                if (ch == 0) {
                    input += static_cast<wchar_t>(0xFFFD);
                } else {
                    input += ch;
                }
            }
            
            return convert.to_bytes(input);
        } else {
            std::basic_string<wchar_t> input;
            input.reserve(static_cast<std::size_t>(length));
            
            for (SQLLEN i = 0; i < length; ++i) {
                wchar_t ch = src[i];
                if (ch == 0) {
                    input += static_cast<wchar_t>(0xFFFD);
                } else {
                    input += ch;
                }
            }
            
            return convert.to_bytes(input);
        }
    } catch (const std::exception&) {
        // Fallback
        std::string result;
        std::size_t actual_length = (length == SQL_NTS) ? std::wcslen(src) : static_cast<std::size_t>(length);
        result.reserve(actual_length * 3);
        
        for (std::size_t i = 0; i < actual_length; ++i) {
            wchar_t ch = src[i];
            if (ch == 0 || ch > 0x7F) {
                result += "\xEF\xBF\xBD";
            } else if (ch < 0x20 && ch != 0x09 && ch != 0x0A && ch != 0x0D) {
                result += "\xEF\xBF\xBD";
            } else {
                result += static_cast<char>(ch);
            }
        }
        
        return result;
    }
}

inline decltype(auto) toUTF8(const signed char * src, SQLLEN length = SQL_NTS) {
    return toUTF8(reinterpret_cast<const char *>(src), length);
}

inline decltype(auto) toUTF8(const unsigned char * src, SQLLEN length = SQL_NTS) {
    return toUTF8(reinterpret_cast<const char *>(src), length);
}

inline decltype(auto) toUTF8(const unsigned short * src, SQLLEN length = SQL_NTS) {
    static_assert(sizeof(unsigned short) == sizeof(char16_t), "unsigned short doesn't match char16_t exactly");
    return toUTF8(reinterpret_cast<const char16_t *>(src), length);
}

inline std::string toUTF8(const std::string & src, const std::locale & locale) {

    // TODO: implement and use conversion to the specified locale.

    throw std::runtime_error("not implemented");
}

// Returns cref to the original, to avoid string copy in no-op case, for now.
inline const std::string & toUTF8(const std::string & src) {

    // TODO: convert to the current locale by default?

    return src;
}

template <typename CharType>
inline decltype(auto) toUTF8(const std::basic_string<CharType> & src) {
    return toUTF8(src.c_str(), src.size());
}

template <typename CharType>
inline decltype(auto) fromUTF8(const std::string & src, UnicodeConversionContext & context); // Leave unimplemented for general case.

template <>
inline decltype(auto) fromUTF8<char>(const std::string & src, UnicodeConversionContext & context) {

    // TODO: implement conversion between specified locales.

    return src;
}

template <>
inline decltype(auto) fromUTF8<signed char>(const std::string & src, UnicodeConversionContext & context) {
    auto && converted = fromUTF8<char>(src, context);
    return std::basic_string<signed char>{converted.begin(), converted.end()};
}

template <>
inline decltype(auto) fromUTF8<unsigned char>(const std::string & src, UnicodeConversionContext & context) {
    auto && converted = fromUTF8<char>(src, context);
    return std::basic_string<unsigned char>{converted.begin(), converted.end()};
}

#if !defined(_MSC_VER) || _MSC_VER >= 1920
template <>
inline decltype(auto) fromUTF8<char16_t>(const std::string & src, UnicodeConversionContext & context) {
    return context.UCS2_converter_char16.from_bytes(src);
}

template <>
inline decltype(auto) fromUTF8<char32_t>(const std::string & src, UnicodeConversionContext & context) {
    return context.UCS2_converter_char32.from_bytes(src);
}
#endif

template <>
inline decltype(auto) fromUTF8<wchar_t>(const std::string & src, UnicodeConversionContext & context) {
    return context.UCS2_converter_wchar.from_bytes(src);
}

#if !defined(_MSC_VER) || _MSC_VER >= 1920
template <>
inline decltype(auto) fromUTF8<unsigned short>(const std::string & src, UnicodeConversionContext & context) {
    static_assert(sizeof(unsigned short) == sizeof(char16_t), "unsigned short doesn't match char16_t exactly");
    auto && converted = fromUTF8<char16_t>(src, context);
    return std::basic_string<unsigned short>{converted.begin(), converted.end()};
}
#endif

template <typename CharType>
inline decltype(auto) fromUTF8(const std::string & src) {
    UnicodeConversionContext context;
    return fromUTF8<CharType>(src, context);
}

template <typename CharType>
inline void fromUTF8(const std::string & src, std::basic_string<CharType> & dest, UnicodeConversionContext & context,
    typename std::enable_if_t<
        std::is_assignable_v<std::basic_string<CharType>, decltype(fromUTF8<CharType>(src))>
    > * = nullptr
) {
    dest = fromUTF8<CharType>(src, context);
}

template <typename CharType>
inline void fromUTF8(const std::string & src, std::basic_string<CharType> & dest, UnicodeConversionContext & context,
    typename std::enable_if_t<
        !std::is_assignable_v<std::basic_string<CharType>, decltype(fromUTF8<CharType>(src))> &&
        std::is_assignable_v<CharType, typename decltype(fromUTF8<CharType>(src))::char_type>
    > * = nullptr
) {
    auto && converted = fromUTF8<CharType>(src, context);
    dest.clear();
    dest.reserve(converted.size() + 1);
    dest.assign(converted.begin(), converted.end());
}

template <typename CharType>
inline decltype(auto) fromUTF8(const std::string & src, std::basic_string<CharType> & dest) {
    UnicodeConversionContext context;
    return fromUTF8<CharType>(src, dest, context);
}
