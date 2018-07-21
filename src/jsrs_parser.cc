// Copyright (c) 2016-2017 JSTP project authors. Use of this source code is
// governed by the MIT license that can be found in the LICENSE file.

#include "jsrs_parser.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

#include "common.h"
#include "unicode_utils.h"

using std::atof;
using std::function;
using std::isalnum;
using std::isalpha;
using std::isdigit;
using std::isinf;
using std::isnan;
using std::isxdigit;
using std::memset;
using std::ptrdiff_t;
using std::size_t;
using std::strncmp;
using std::strncpy;
using std::strtol;

using v8::Array;
using v8::False;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Null;
using v8::Number;
using v8::Object;
using v8::String;
using v8::True;
using v8::Undefined;
using v8::Value;

using jstp::unicode_utils::CodePointToUtf8;
using jstp::unicode_utils::IsWhiteSpaceCharacter;
using jstp::unicode_utils::IsLineTerminatorSequence;

namespace jstp {

namespace parser {

// Enumeration of supported JavaScript types used for deserialization
// function selection.
enum Type {
  kUndefined = 0, kNull, kBool, kNumber, kString, kArray, kObject, kDate
};

// Parses the type of the serialized JavaScript value at the position `begin`
// and before `end`. Returns true if it was able to detect the type, false
// otherwise.
static bool GetType(const char* begin, const char* end, Type* type);

// The table of parsing functions indexed with the values of the Type
// enumeration.
static constexpr MaybeLocal<Value> (*kParseFunctions[])(Isolate*,
                                                        const char*,
                                                        const char*,
                                                        size_t*) = {
  &internal::ParseUndefined,
  &internal::ParseNull,
  &internal::ParseBool,
  &internal::ParseNumber,
  &internal::ParseString,
  &internal::ParseArray,
  &internal::ParseObject
};

Local<Value> Parse(Isolate* isolate, const String::Utf8Value& in) {
  const char* str = *in;
  const size_t length = in.length();
  const char* end = str + length;

  Type type;

  size_t start_pos = internal::SkipToNextToken(str, end);
  if (!GetType(str + start_pos, end, &type)) {
    THROW_EXCEPTION(TypeError, "Invalid type");
    return Undefined(isolate);
  }

  size_t parsed_size = 0;
  MaybeLocal<Value> result = kParseFunctions[type](isolate,
                                                   str + start_pos,
                                                   end,
                                                   &parsed_size);

  if (result.IsEmpty()) {
    return Undefined(isolate);
  }

  parsed_size += internal::SkipToNextToken(str + start_pos + parsed_size, end);
  parsed_size += start_pos;

  if (length != parsed_size) {
    THROW_EXCEPTION(SyntaxError, "Invalid format");
    return Undefined(isolate);
  }

  return result.ToLocalChecked();
}

static bool GetType(const char* begin, const char* end, Type* type) {
  bool result = true;
  switch (*begin) {
    case ',':
    case ']': {
      *type = Type::kUndefined;
      break;
    }
    case '{': {
      *type = Type::kObject;
      break;
    }
    case '[': {
      *type = Type::kArray;
      break;
    }
    case '\"':
    case '\'': {
      *type = Type::kString;
      break;
    }
    case 't':
    case 'f': {
      *type = Type::kBool;
      break;
    }
    case 'n': {
      *type = Type::kNull;
      if (begin + 4 <= end) {
        result = (strncmp(begin, "null", 4) == 0);
      }
      break;
    }
    case 'u': {
      *type = Type::kUndefined;
      if (begin + 9 <= end) {
        result = (strncmp(begin, "undefined", 9) == 0);
      }
      break;
    }
    case 'N':
    case 'I': {
      *type = Type::kNumber;
      break;
    }
    default: {
      result = false;
      if (isdigit(*begin) || *begin == '.' || *begin == '+' || *begin == '-') {
        *type = Type::kNumber;
        result = true;
      }
    }
  }
  return result;
}

namespace internal {

// Returns true if `str` points to a multiline comment ending, false otherwise.
bool IsMultilineCommentEnd(const char* str, size_t* size) {
  if (str[0] == '*' && str[1] == '/') {
    *size = 2;
    return true;
  }
  return false;
}

// Returns count of bytes needed to skip to current comment ending.
size_t SkipToCommentEnd(const char* str, const char* end) {
  bool is_single_line;

  switch (str[1]) {
    case '/': {
      is_single_line = true;
      break;
    }
    case '*': {
      is_single_line = false;
      break;
    }
    default: {  // In case it is not a comment start
      return 0;
    }
  }

  function<bool(const char*, size_t*)> end_check_func;

  if (is_single_line) {
    end_check_func = IsLineTerminatorSequence;
  } else {
    end_check_func = IsMultilineCommentEnd;
  }

  const size_t size = end - str;

  size_t pos = 2;
  size_t current_size;

  for (; pos < size; pos++) {
    if (end_check_func(str + pos, &current_size)) {
      return pos + current_size;
    }
  }

  if (is_single_line) {
    return pos;
  } else {
    return 0;
  }
}

size_t SkipToNextToken(const char* str, const char* end) {
  size_t pos = 0;
  size_t current_size;
  const size_t size = end - str;

  while (pos < size) {
    if (IsWhiteSpaceCharacter(str + pos, &current_size) ||
        IsLineTerminatorSequence(str + pos, &current_size)) {
      pos += current_size;
    } else if (str[pos] == '/') {
      size_t to_skip = SkipToCommentEnd(str + pos, end);
      if (!to_skip) {
        break;
      }
      pos += to_skip;
    } else {
      break;
    }
  }

  return pos;
}

MaybeLocal<Value> ParseUndefined(Isolate*    isolate,
                                 const char* begin,
                                 const char* end,
                                 size_t*     size) {
  if (*begin == ',' || *begin == ']') {
    *size = 0;
  } else if (*begin == 'u') {
    *size = 9;
  } else {
    THROW_EXCEPTION(TypeError, "Invalid format of undefined value");
    return MaybeLocal<Value>();
  }
  return Undefined(isolate);
}

MaybeLocal<Value> ParseNull(Isolate*    isolate,
                            const char* begin,
                            const char* end,
                            size_t*     size) {
  *size = 4;
  return Null(isolate);
}

MaybeLocal<Value> ParseBool(Isolate*    isolate,
                            const char* begin,
                            const char* end,
                            size_t*     size) {
  MaybeLocal<Value> result;
  if (begin + 4 <= end && strncmp(begin, "true", 4) == 0) {
    result = True(isolate);
    *size = 4;
  } else if (begin + 5 <= end && strncmp(begin, "false", 5) == 0) {
    result = False(isolate);
    *size = 5;
  } else {
    THROW_EXCEPTION(TypeError, "Invalid format: expected boolean");
  }
  return result;
}

// Checks if a character is an octal digit.
inline bool IsOctalDigit(char character) {
  return character >= '0' && character <= '7';
}

MaybeLocal<Value> ParseNumber(Isolate*    isolate,
                              const char* begin,
                              const char* end,
                              size_t*     size) {
  bool negate_result = false;
  const char* number_start = begin;

  if (*begin == '+' || *begin == '-') {
    negate_result = *begin == '-';
    number_start++;
  }

  int base = 10;

  if (*number_start == '0') {
    number_start++;

    if (IsOctalDigit(*number_start)) {
      THROW_EXCEPTION(SyntaxError, "Use new octal literal syntax");
      return MaybeLocal<Value>();
    } else if (*number_start == 'b') {
      base = 2;
      number_start++;
    } else if (*number_start == 'o') {
      base = 8;
      number_start++;
    } else if (*number_start == 'x') {
      base = 16;
      number_start++;
    } else {
      number_start--;
    }
  }

  MaybeLocal<Value> result;

  if (base == 10) {
    result = ParseDecimalNumber(isolate, number_start, end, size,
                                negate_result);
  } else {
    result = ParseIntegerNumber(isolate, number_start, end, size,
                                base, negate_result);
    if (*size == 0) {
      THROW_EXCEPTION(SyntaxError, "Empty number value");
      return MaybeLocal<Value>();
    }
  }
  *size += number_start - begin;
  return result;
}

MaybeLocal<Value> ParseDecimalNumber(Isolate*    isolate,
                                     const char* begin,
                                     const char* end,
                                     size_t*     size,
                                     bool        negate_result) {
  char* number_end;
  double number = strtod(begin, &number_end);

  if (negate_result) {
    number = -number;
  }

  // strictly allow only "NaN" and "Infinity"
  if (isnan(number)) {
    if (strncmp(begin + 1, "aN", 2) != 0) {
      THROW_EXCEPTION(SyntaxError, "Invalid format: expected NaN");
      return MaybeLocal<Value>();
    }
  } else if (isinf(number)) {
    if (strncmp(begin + 1, "nfinity", 7) != 0) {
      THROW_EXCEPTION(SyntaxError, "Invalid format: expected Infinity");
      return MaybeLocal<Value>();
    }
  }

  *size = number_end - begin;
  return Number::New(isolate, number);
}

Local<Value> ParseIntegerNumber(Isolate*    isolate,
                                const char* begin,
                                const char* end,
                                size_t*     size,
                                int         base,
                                bool        negate_result) {
  char* number_end;
  int32_t value = strtol(begin, &number_end, base);
  if (negate_result) {
    value = -value;
  }
  *size = static_cast<size_t>(number_end - begin);
  return Integer::New(isolate, value);
}

static bool GetControlChar(Isolate*    isolate,
                           const char* str,
                           size_t*     res_len,
                           size_t*     size,
                           char*       write_to);

MaybeLocal<Value> ParseString(Isolate*    isolate,
                              const char* begin,
                              const char* end,
                              size_t*     size) {
  *size = end - begin;
  char* result = new char[*size + 1];

  enum { kApostrophe = 0, kQMarks} string_mode = (*begin == '\'') ?
                                                 kApostrophe :
                                                 kQMarks;
  bool is_ended = false;
  size_t res_index = 0;
  size_t out_offset, in_offset;

  for (size_t i = 1; i < *size; i++) {
    if ((string_mode == kQMarks     && begin[i] == '\"') ||
        (string_mode == kApostrophe && begin[i] == '\'')) {
      is_ended = true;
      *size = i + 1;
      break;
    }

    if (begin[i] == '\\') {
      if (IsLineTerminatorSequence(begin + i + 1, &in_offset)) {
        i += in_offset;
      } else {
        bool ok = GetControlChar(isolate, begin + ++i, &out_offset, &in_offset,
                                 result + res_index);
        if (!ok) {
          delete[] result;
          return MaybeLocal<Value>();
        }
        i += in_offset - 1;
        res_index += out_offset;
      }
    } else if (IsLineTerminatorSequence(begin + i, &in_offset)) {
      delete[] result;
      THROW_EXCEPTION(SyntaxError, "Unexpected line end in string");
      return MaybeLocal<Value>();
    } else {
      result[res_index++] = begin[i];
    }
  }

  if (!is_ended) {
    delete[] result;
    THROW_EXCEPTION(SyntaxError, "Error while parsing string");
    return MaybeLocal<Value>();
  }

  Local<String> result_str = String::NewFromUtf8(isolate, result,
      NewStringType::kNormal, static_cast<int>(res_index)).ToLocalChecked();
  delete[] result;
  return result_str;
}

static unsigned int ReadHexNumber(const char* str, size_t len, bool* ok);

// Parses a part of a JavaScript string representation after the backslash
// character (i.e., an escape sequence without \) into an unescaped control
// character and writes it to `write_to`.
// Returns true if no error occured, false otherwise.
static bool GetControlChar(Isolate*    isolate,
                           const char* str,
                           size_t*     res_len,
                           size_t*     size,
                           char*       write_to) {
  *size = 1;
  *res_len = 1;
  bool ok;
  switch (str[0]) {
    case 'b': {
      *write_to = '\b';
      break;
    }
    case 'f': {
      *write_to = '\f';
      break;
    }
    case 'n': {
      *write_to = '\n';
      break;
    }
    case 'r': {
      *write_to = '\r';
      break;
    }
    case 't': {
      *write_to = '\t';
      break;
    }
    case 'v': {
      *write_to = '\v';
      break;
    }

    case 'x': {
      *write_to = static_cast<char>(ReadHexNumber(str + 1, 2, &ok));
      if (!ok) {
        THROW_EXCEPTION(SyntaxError, "Invalid hexadecimal escape sequence");
        return false;
      }
      *size = 3;
      break;
    }

    case 'u': {
      unsigned int symb_code;
      if (isxdigit(str[1])) {
        symb_code = ReadHexNumber(str + 1, 4, &ok);
        *size = 5;
      } else if (str[1] == '{') {
        size_t hex_size;  // maximal hex is 10FFFF
        for (hex_size = 1;
             str[hex_size + 2] != '}' && hex_size <= 6;
             hex_size++) {
          if (str[hex_size + 2] == '\0') {
            THROW_EXCEPTION(SyntaxError, "Invalid Unicode code point escape");
            return false;
          }
        }
        symb_code = ReadHexNumber(str + 2, hex_size, &ok);
        *size = hex_size + 3;
      } else {
        ok = false;
      }

      if (!ok) {
        THROW_EXCEPTION(SyntaxError, "Invalid Unicode escape sequence");
        return false;
      }
      CodePointToUtf8(symb_code, res_len, write_to);
      break;
    }

    default: {
      *write_to = str[0];
    }
  }

  return true;
}

// Parses a hexadecimal number into unsigned int. Whether the parsing
// was successful is determined by the value of `ok`.
static unsigned int ReadHexNumber(const char* str, size_t len, bool* ok) {
  char t[6];
  char* end;
  strncpy(t, str, len);
  t[len] = '\0';
  unsigned int result = strtol(t, &end, 16);
  if (end - t != static_cast<ptrdiff_t>(len)) {
    *ok = false;
  } else {
    *ok = true;
  }
  return result;
}

MaybeLocal<String> ParseKeyInObject(Isolate*    isolate,
                                    const char* begin,
                                    const char* end,
                                    size_t*     size) {
  *size = end - begin;
  Local<String> result;
  if (begin[0] == '\'' || begin[0] == '"') {
    Type current_type;
    bool valid = GetType(begin, end, &current_type);
    if (valid && current_type == Type::kString) {
      size_t offset;
      MaybeLocal<Value> key = ParseString(isolate, begin, end, &offset);
      if (key.IsEmpty()) {
        return MaybeLocal<String>();
      }
      result = key.ToLocalChecked().As<String>();
      *size = offset;
      return result;
    } else {
      THROW_EXCEPTION(SyntaxError,
          "Invalid format in object: key is invalid string");
      return MaybeLocal<String>();
    }
  } else {
    size_t current_length = 0;
    for (size_t i = 0; i < *size; i++) {
      if (begin[i] == '_' || (i != 0 &&
                              isalnum(begin[i])) ||
                              isalpha(begin[i])) {
        current_length++;
      } else {
        if (current_length != 0) {
          result = String::NewFromUtf8(isolate, begin,
                                       NewStringType::kInternalized,
                                       static_cast<int>(current_length))
                                           .ToLocalChecked();
          break;
        } else {
          THROW_EXCEPTION(SyntaxError, "Unexpected identifier");
          return MaybeLocal<String>();
        }
      }
    }
    *size = current_length;
    return result;
  }
}

MaybeLocal<Value> ParseValueInObject(Isolate*    isolate,
                                     const char* begin,
                                     const char* end,
                                     size_t*     size) {
  Type current_type;
  bool valid = GetType(begin, end, &current_type);
  if (valid) {
    return (kParseFunctions[current_type])(isolate, begin, end, size);
  } else {
    THROW_EXCEPTION(TypeError, "Invalid type in object");
    return MaybeLocal<Value>();
  }
}

MaybeLocal<Value> ParseObject(Isolate*    isolate,
                              const char* begin,
                              const char* end,
                              size_t*     size) {
  bool key_mode = true;
  *size = end - begin;
  MaybeLocal<String> current_key;
  MaybeLocal<Value> current_value;
  size_t current_length = 0;
  auto result = Object::New(isolate);
  bool has_ended = false;

  for (size_t i = 1; i < *size; i++) {
    if (key_mode) {
      i += SkipToNextToken(begin + i, end);
      if (begin[i] == '}') {
        *size = i + 1;
        has_ended = true;
        break;
      }
      current_key = ParseKeyInObject(isolate,
                                     begin + i,
                                     end,
                                     &current_length);
      if (current_key.IsEmpty()) {
        return MaybeLocal<Value>();
      }
      i += current_length;
      i += SkipToNextToken(begin + i, end);
      if (begin[i] != ':') {
        THROW_EXCEPTION(SyntaxError, "Unexpected token");
        return MaybeLocal<Value>();
      }
    } else {
      i += SkipToNextToken(begin + i, end);
      if (begin[i] == ',') {
        THROW_EXCEPTION(SyntaxError, "Value is missing in object");
        return MaybeLocal<Value>();
      }
      current_value = ParseValueInObject(isolate,
                                         begin + i,
                                         end,
                                         &current_length);
      if (current_value.IsEmpty()) {
        return current_value;
      }
      Local<Value> value = current_value.ToLocalChecked();
      if (!value->IsUndefined()) {
        Maybe<bool> is_ok = result->Set(isolate->GetCurrentContext(),
                                        current_key.ToLocalChecked(),
                                        value);
        if (is_ok.IsNothing()) {
          THROW_EXCEPTION(Error, "Cannot add property to object");
          return MaybeLocal<Value>();
        }
      }
      i += current_length;
      i += SkipToNextToken(begin + i, end);
      if (begin[i] != ',' && begin[i] != '}') {
        THROW_EXCEPTION(SyntaxError, "Invalid format in object");
        return MaybeLocal<Value>();
      } else if (begin[i] == '}') {
        *size = i + 1;
        has_ended = true;
        break;
      }
    }
    key_mode = !key_mode;
  }

  if (!has_ended) {
    THROW_EXCEPTION(SyntaxError, "Missing closing brace in object");
    return MaybeLocal<Value>();
  }

  return result;
}

MaybeLocal<Value> ParseArray(Isolate*    isolate,
                             const char* begin,
                             const char* end,
                             size_t*     size) {
  auto array = Array::New(isolate);
  size_t current_length = 0;
  *size = end - begin;

  bool is_empty = true;
  bool has_ended = false;

  size_t current_element = 0;
  Type current_type;

  for (size_t i = 1; i < *size; i++) {
    i += SkipToNextToken(begin + i, end);
    if (is_empty && begin[i] == ']') {  // In case of empty array
      *size = i + 1;
      return array;
    }

    bool valid = GetType(begin + i, end, &current_type);
    if (valid) {
      MaybeLocal<Value> t = kParseFunctions[current_type](isolate,
                                                          begin + i,
                                                          end,
                                                          &current_length);
      if (t.IsEmpty()) {
        return t;
      }
      if (!(current_type == Type::kUndefined && begin[i] == ']')) {
        array->Set(static_cast<uint32_t>(current_element++),
                   t.ToLocalChecked());
        is_empty = false;
      }

      i += current_length;
      i += SkipToNextToken(begin + i, end);

      current_length = 0;

      if (begin[i] != ',' && begin[i] != ']') {
        THROW_EXCEPTION(SyntaxError, "Invalid format in array: missed comma");
        return MaybeLocal<Value>();
      } else if (begin[i] == ']') {
        *size = i + 1;
        has_ended = true;
        break;
      }
    } else {
      THROW_EXCEPTION(TypeError, "Invalid type in array");
      return MaybeLocal<Value>();
    }
  }

  if (!has_ended) {
    THROW_EXCEPTION(SyntaxError, "Missing closing bracket in array");
    return MaybeLocal<Value>();
  }

  return array;
}

}  // namespace internal

}  // namespace parser

}  // namespace jstp
