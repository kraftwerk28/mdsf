// JSTP Record Serialization
//
// Copyright (c) 2016 Mykola Bilochub, Alexey Orlenko
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
// The code in this file is based upon the C++ implementation of JSTP parser
// and serializer originating from `https://github.com/NechaiDO/JSTP-cpp`.
// Original license:
//
// Copyright (c) 2016 Dmytro Nechai, Nikolai Belochub
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include "jsrs.h"
#include "jsrs-impl.h"

#include <cctype>
#include <clocale>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include <vector>

namespace jstp {

namespace jsrs {

v8::Local<v8::String> Stringify(v8::Isolate* isolate,
    v8::Local<v8::Value> value) {
  if (value->IsFunction()) {
    return v8::Local<v8::String>();
  }
  if (value->IsNumber()    ||
      value->IsBoolean()   ||
      value->IsUndefined() ||
      value->IsNull()) {
    return value->ToString(isolate->GetCurrentContext()).ToLocalChecked();
  } else if (value->IsDate()) {
    return serializer::StringifyDate(isolate, value.As<v8::Date>());
  } else if (value->IsString()) {
    return serializer::StringifyString(isolate, value.As<v8::String>());
  } else if (value->IsArray()) {
    return serializer::StringifyArray(isolate, value.As<v8::Array>());
  } else if (value->IsObject()) {
    return serializer::StringifyObject(isolate, value.As<v8::Object>());
  } else {
    return v8::Local<v8::String>();
  }
}

namespace serializer {

v8::Local<v8::String> StringifyDate(v8::Isolate* isolate,
    v8::Local<v8::Date> date) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Value> toISOString = date->Get(context,
    v8::String::NewFromUtf8(isolate, "toISOString")).ToLocalChecked();
  v8::Local<v8::Value> result = toISOString.As<v8::Function>()->Call(context,
    date, 0, nullptr).ToLocalChecked();
  v8::Local<v8::String> quotes = v8::String::NewFromUtf8(isolate, "\'");
  v8::Local<v8::String> res_str = result->ToString();
  res_str = v8::String::Concat(quotes, v8::String::Concat(res_str, quotes));
  return res_str->ToString();
}

v8::Local<v8::String> StringifyArray(v8::Isolate* isolate,
    v8::Local<v8::Array> array) {
  v8::Local<v8::String> comma =
    v8::String::NewFromUtf8(isolate, ",");

  v8::Local<v8::String> result =
    v8::String::NewFromUtf8(isolate, "[");

  uint32_t length = array->Length();

  for (uint32_t index = 0; index < length; index++) {
    v8::Local<v8::Value> value = array->Get(index);
    if (!value->IsUndefined()) {
      v8::Local<v8::String> chunk = jstp::jsrs::Stringify(isolate, value);
      if (chunk.IsEmpty()) continue;
      result = v8::String::Concat(result, chunk);
    }
    if (index != length - 1) {
      result = v8::String::Concat(result, comma);
    }
  }

  result = v8::String::Concat(result,
      v8::String::NewFromUtf8(isolate, "]"));

  return result;
}

v8::Local<v8::String> StringifyString(v8::Isolate* isolate,
    v8::Local<v8::String> string) {
  uint32_t length = string->Length();
  std::vector<char> result_str;
  result_str.reserve((length + 1) * 2);
  result_str.push_back('\'');
  v8::String::Utf8Value utf8string(string);
  const char* c_string = *utf8string;
  for (uint32_t i = 0; i < length; i++) {
    std::size_t offset;
    const char* ch = GetEscapedControlChar(c_string[i], &offset);
    if (ch) {
      for (std::size_t k = 0; k < offset; k++) {
        result_str.push_back(ch[k]);
      }
    } else {
      result_str.push_back(c_string[i]);
    }
  }

  result_str.push_back('\'');

  return v8::String::NewFromUtf8(isolate, result_str.data(),
      v8::NewStringType::kNormal, result_str.size()).ToLocalChecked();
}

const char* GetEscapedControlChar(char str, std::size_t* size) {
  constexpr static const char* control_chars[0x20] = {
    "\\u0000", "\\u0001", "\\u0002",
    "\\u0003", "\\u0004", "\\u0005",
    "\\u0006", "\\u0007", "\\u0008",
    "\\u0009", "\\u000a", "\\u000b",
    "\\u000c", "\\u000d", "\\u000e",
    "\\u000f", "\\u0010", "\\u0011",
    "\\u0012", "\\u0013", "\\u0014",
    "\\u0015", "\\u0016", "\\u0017",
    "\\u0018", "\\u0019", "\\u001a",
    "\\u001b", "\\u001c", "\\u001d",
    "\\u001e", "\\u001f"
  };

  *size = 2;

  switch (str) {
    case '\a': return "\\a";
    case '\b': return "\\b";
    case '\f': return "\\f";
    case '\n': return "\\n";
    case '\r': return "\\r";
    case '\t': return "\\t";
    case '\v': return "\\v";
    case '\\': return "\\\\";
    case '\'': return "\\'";
    case 0x7F: return "\\u007f";
    default:
      if (str < 0x20) {
        *size = 6;
        return control_chars[static_cast<std::size_t>(str)];
      } else {
        return nullptr;
      }
  }
}

v8::Local<v8::String> StringifyObject(v8::Isolate* isolate,
    v8::Local<v8::Object> object) {
  v8::Local<v8::String> comma = v8::String::NewFromUtf8(isolate, ",");
  v8::Local<v8::String> colon = v8::String::NewFromUtf8(isolate, ":");

  v8::Local<v8::String> result = v8::String::NewFromUtf8(isolate, "{");

  v8::Local<v8::Context> context = isolate->GetCurrentContext();

  v8::Local<v8::Array> keys = object->GetOwnPropertyNames(context)
                                      .ToLocalChecked();
  v8::Local<v8::String> chunk;
  bool first_defined = true;
  for (uint32_t i = 0; i < keys->Length(); i++) {
    v8::Local<v8::Value> key = keys->Get(context, i).ToLocalChecked();
    v8::Local<v8::Value> value = object->Get(context, key).ToLocalChecked();
    chunk = jstp::jsrs::Stringify(isolate, value);
    if (!value->IsUndefined() && !chunk.IsEmpty()) {
      if (i != 0 && first_defined) {
        result = v8::String::Concat(result, comma);
      }
      first_defined = true;
      result = v8::String::Concat(result,
          StringifyKey(isolate, key->ToString()));
      result = v8::String::Concat(result, colon);
      result = v8::String::Concat(result, chunk);
    } else {
      if (i == 0) first_defined = false;
    }
  }
  result = v8::String::Concat(result, v8::String::NewFromUtf8(isolate, "}"));
  return result;
}

v8::Local<v8::String> StringifyKey(v8::Isolate* isolate,
    v8::Local<v8::String> key) {
  v8::String::Utf8Value key_str(key.As<v8::Value>());
  if (!IsValidKey(isolate, key_str)) {
    return StringifyString(isolate, key);
  }
  return key;
}

bool IsValidKey(v8::Isolate* isolate, const v8::String::Utf8Value& key) {
  bool result = true;
  const char* key_str = *key;
  for (int i = 0; i < key.length(); i++) {
    if (key_str[i] == '_') continue;
    if ((i == 0 && !isalpha(key_str[i])) || !isalnum(key_str[i])) {
      result = false;
      break;
    }
  }
  return result;
}

}  // namespace serializer

v8::Local<v8::Value> Parse(v8::Isolate* isolate,
      const v8::String::Utf8Value& in) {
  const char* to_parse = deserializer::PrepareString(*in, in.length());
  deserializer::Type type;
  std::size_t size = strlen(to_parse);
  if (!deserializer::GetType(to_parse, to_parse + size, &type)) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Invalid type")));
    return v8::Undefined(isolate);
  }
  v8::Local<v8::Value> result =
      (deserializer::kParseFunctions[type])(isolate, to_parse,
                                            to_parse + size, &size);
  if (size != strlen(to_parse)) {
    isolate->ThrowException(v8::Exception::SyntaxError(
        v8::String::NewFromUtf8(isolate, "Invalid format")));
    return v8::Undefined(isolate);
  }
  delete[] to_parse;
  return result;
}

namespace deserializer {

const char* PrepareString(const char* str, std::size_t length) {
  char* result = new char[length + 1];
  bool string_mode = false;
  enum { kDisabled = 0, kOneline, kMultiline } comment_mode = kDisabled;
  std::size_t j = 0;
  for (std::size_t i = 0; i < length; i++) {
    if ((str[i] == '\"' || str[i] == '\'') && (i == 0 || str[i - 1] != '\\')) {
      string_mode = !string_mode;
    }
    if (!string_mode) {
      if (!comment_mode && str[i] == '/') {
        switch (str[i + 1]) {
          case '/':
            comment_mode = kOneline;
            break;
          case '*':
            comment_mode = kMultiline;
            break;
        }
      }
      if (!comment_mode && !std::isspace(str[i])) {
        result[j++] = str[i];
      }
      if ((comment_mode == kOneline && (str[i] == '\n' || str[i] == '\r')) ||
          (comment_mode == kMultiline && str[i - 1] == '*' && str[i] == '/')) {
        comment_mode = kDisabled;
      }
    } else {
      result[j++] = str[i];
    }
  }
  result[j] = '\0';
  return result;
}

v8::Local<v8::Value> ParseUndefined(v8::Isolate* isolate, const char* begin,
                                    const char* end, std::size_t* size) {
  if (*begin == ',' || *begin == ']') {
    *size = 0;
  } else if (*begin == 'u') {
    *size = 9;
  } else {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Invalid format of undefined value")));
  }
  return v8::Undefined(isolate);
}

v8::Local<v8::Value> ParseNull(v8::Isolate* isolate, const char* begin,
                               const char* end, std::size_t* size) {
  *size = 4;
  return v8::Null(isolate);
}

v8::Local<v8::Value> ParseBool(v8::Isolate* isolate, const char* begin,
                               const char* end, std::size_t* size) {
  v8::Local<v8::Value> result;
  if (begin + 4 <= end && strncmp(begin, "true", 4) == 0) {
    result = v8::True(isolate);
    *size = 4;
  } else if (begin + 5 <= end && strncmp(begin, "false", 5) == 0) {
    result = v8::False(isolate);
    *size = 5;
  } else {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate,
            "Invalid format: expected boolean")));
    result = v8::Undefined(isolate);
  }
  return result;
}

v8::Local<v8::Value> ParseNumber(v8::Isolate* isolate, const char* begin,
                                 const char* end, std::size_t* size) {
  v8::Local<v8::Number> result = v8::Number::New(isolate, std::atof(begin));
  *size = end - begin;
  std::size_t i = 0;
  while (begin[i] != ',' && begin[i] != '}' && begin[i] != ']' &&
         i < *size) i++;
  *size = i;
  return result;
}

v8::Local<v8::Value> ParseString(v8::Isolate* isolate, const char* begin,
                                 const char* end, std::size_t* size) {
  *size = end - begin;
  char* result = new char[*size + 1];
  std::memset(result, 0, *size + 1);
  enum { kApostrophe = 0, kQMarks} string_mode =
       (*begin == '\'') ? kApostrophe : kQMarks;
  bool is_ended = false;
  std::size_t res_index = 0;
  std::size_t out_offset, in_offset;
  for (std::size_t i = 1; i < *size; i++) {
    if (((string_mode == kQMarks && begin[i] == '\"') ||
         (string_mode == kApostrophe && begin[i] == '\'')) &&
          begin[i - 1] != '\\') {
      is_ended = true;
      *size = i + 1;
      result[res_index] = '\0';
      break;
    }
    if (begin[i] == '\\') {
      char* symb = GetControlChar(isolate, begin + ++i, &out_offset,
          &in_offset);
      if (!symb) {
        return v8::String::Empty(isolate);
      }
      std::strncpy(result + res_index, symb, out_offset);
      delete []symb;
      i += in_offset - 1;
      res_index += out_offset;
    } else {
      result[res_index++] = begin[i];
    }
  }
  if (!is_ended) {
    isolate->ThrowException(v8::Exception::SyntaxError(
    v8::String::NewFromUtf8(isolate, "Error while parsing string")));
    return v8::String::Empty(isolate);
  }
  return v8::String::NewFromUtf8(isolate, result, v8::NewStringType::kNormal,
                                 res_index).ToLocalChecked();
}

char* CodePointsToUtf8(unsigned int c, std::size_t* size) {
  char* result = new char[4];
  char* b = result;
  if (c < 0x80) {
    *b++ = c;
    *size = 1;
  } else if (c < 0x800) {
    *b++ = 192 + c / 64;
    *b++ = 128 + c % 64;
    *size = 2;
  } else if (c - 0xd800u < 0x800) {
    delete []result;
    return nullptr;
  } else if (c < 0x10000) {
     *b++ = 224 + c / 4096;
     *b++ = 128 + c / 64 % 64;
     *b++ = 128 + c % 64;
     *size = 3;
  } else if (c < 0x110000) {
     *b++ = 240 + c / 262144;
     *b++ = 128 + c / 4096 % 64;
     *b++ = 128 + c / 64 % 64;
     *b++ = 128 + c % 64;
     *size = 4;
  } else {
    delete []result;
    return nullptr;
  }
  return result;
}

unsigned int ReadHexNumber(const char* str, int len, bool* ok) {
  char t[5];
  char* end;
  std::strncpy(t, str, len);
  t[len] = '\0';
  unsigned int result = std::strtol(t, &end, 16);
  if (end - t != len) {
    *ok = false;
  } else {
    *ok = true;
  }
  return result;
}

char* GetControlChar(v8::Isolate* isolate, const char* str,
                     std::size_t* res_len, std::size_t* size) {
  char* result = new char[5];
  *size = 1;
  *res_len = 1;
  bool ok;
  switch (str[0]) {
    case 'a': *result = '\a'; break;
    case 'b': *result = '\b'; break;
    case 'f': *result = '\f'; break;
    case 'n': *result = '\n'; break;
    case 'r': *result = '\r'; break;
    case 't': *result = '\t'; break;
    case 'v': *result = '\v'; break;
    case '0': *result = '\0'; break;
    case '\\': *result = '\\'; break;
    case '\'': *result = '\''; break;
    case '"': *result = '"'; break;
    case 'x': {
      *result = ReadHexNumber(str + 1, 2, &ok);
      if (!ok) {
        isolate->ThrowException(v8::Exception::SyntaxError(
            v8::String::NewFromUtf8(isolate,
                "Invalid hexadecimal escape sequence")));
        return nullptr;
      }
      *size = 3;
      break;
    }
    case 'u': {
      unsigned int symb_code = ReadHexNumber(str + 1, 4, &ok);
      if (!ok) {
        isolate->ThrowException(v8::Exception::SyntaxError(
            v8::String::NewFromUtf8(isolate,
                "Invalid Unicode escape sequence")));
        return nullptr;
      }
      char* unicodeSymbol = CodePointsToUtf8(symb_code, res_len);
      if (!unicodeSymbol) {
        isolate->ThrowException(v8::Exception::SyntaxError(
            v8::String::NewFromUtf8(isolate, "Unknown Unicode symbol")));
        return nullptr;
      }
      *size = 5;
      delete []result;
      return unicodeSymbol;
    }
    default:
      isolate->ThrowException(v8::Exception::SyntaxError(
          v8::String::NewFromUtf8(isolate, "Invalid string format")));
      return nullptr;
  }

  return result;
}

v8::Local<v8::Value> ParseObject(v8::Isolate* isolate, const char* begin,
                                 const char* end, std::size_t* size) {
  bool key_mode = true;
  *size = end - begin;
  char current_key[kMaxKeyLength];
  v8::Local<v8::String> current_key_string = v8::Local<v8::String>();
  std::size_t current_length = 0;
  Type current_type;
  v8::Local<v8::Object> object = v8::Object::New(isolate);
  v8::Local<v8::Value> t;
  for (std::size_t i = 1; i < *size; i++) {
    if (key_mode) {
      if (begin[i] == ':') {
        key_mode = false;
        strncpy(current_key, begin + i - current_length, current_length);
        current_key[current_length] = '\0';
        current_length = 0;
      } else if (isalnum(begin[i]) || begin[i] == '_') {
        current_length++;
      } else if ((begin[i] == '\'' || begin[i] == '"') && begin[i-1] != '\\') {
        bool valid = GetType(begin + i, end, &current_type);
        if (valid && current_type == Type::kString) {
          std::size_t offset;
          current_key_string = ParseString(isolate, begin + i, end,
                                           &offset).As<v8::String>();
          i += offset - 1;
        } else {
          isolate->ThrowException(v8::Exception::SyntaxError(
              v8::String::NewFromUtf8(isolate,
                  "Invalid format in object: key is invalid string")));
          return v8::Object::New(isolate);
        }
      } else if (begin[i] == '}') {
        return object;  // In case of empty object
      } else {
        isolate->ThrowException(v8::Exception::SyntaxError(
            v8::String::NewFromUtf8(isolate,
                "Invalid format in object: key has invalid type")));
        return v8::Object::New(isolate);
      }
    } else {
      bool valid = GetType(begin + i, end, &current_type);
      if (valid) {
        t = (kParseFunctions[current_type])(isolate, begin + i, end,
                                       &current_length);
        if (!t->IsUndefined()) {
          auto check_result = [isolate](v8::Maybe<bool> value) {
            if (value.IsNothing()) {
              isolate->ThrowException(
                  v8::Exception::Error(v8::String::NewFromUtf8(isolate,
                      "Cannot add property to object")));
            }
          };

          if (!current_key_string.IsEmpty()) {
            v8::Maybe<bool> result = object->Set(isolate->GetCurrentContext(),
                current_key_string, t);
            check_result(result);
            current_key_string.Clear();
          } else {
            v8::Maybe<bool> result = object->Set(isolate->GetCurrentContext(),
                v8::String::NewFromUtf8(isolate, current_key), t);
            check_result(result);
          }
        }
        i += current_length;
        if (begin[i] != ',' && begin[i] != '}') {
          isolate->ThrowException(v8::Exception::SyntaxError(
          v8::String::NewFromUtf8(isolate,
              "Invalid format in object: missed comma")));
          return v8::Object::New(isolate);
        } else if (begin[i] == '}') {
          *size = i + 1;
          break;
        }
        current_key[0] = '\0';
        current_length = 0;
        key_mode = true;
      } else {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8(isolate, "Invalid type in object")));
        return v8::Object::New(isolate);
      }
    }
  }
  return object;
}

bool GetType(const char* begin, const char* end, Type* type) {
  bool result = true;
  switch (*begin) {
    case ',':
    case ']':
      *type = Type::kUndefined;
      break;
    case '{':
      *type = Type::kObject;
      break;
    case '[':
      *type = Type::kArray;
      break;
    case '\"':
    case '\'':
      *type = Type::kString;
      break;
    case 't':
    case 'f':
      *type = Type::kBool;
      break;
    case 'n':
      *type = Type::kNull;
      if (begin + 4 <= end) {
        result = (std::strncmp(begin, "null", 4) == 0);
      }
      break;
    case 'u':
      *type = Type::kUndefined;
      if (begin + 9 <= end) {
        result = (std::strncmp(begin, "undefined", 9) == 0);
      }
      break;
    default:
      result = false;
      if (isdigit(*begin) || *begin == '.' || *begin == '+' || *begin == '-') {
        *type = Type::kNumber;
        result = true;
      }
  }
  return result;
}

v8::Local<v8::Value> ParseArray(v8::Isolate* isolate, const char* begin,
                                const char* end, std::size_t* size) {
  Type current_type;
  v8::Local<v8::Array> array = v8::Array::New(isolate);
  std::size_t current_length = 0;
  *size = end - begin;
  if (*begin == '[' && *(begin + 1) == ']') {  // In case of empty array
    *size = 2;
    return array;
  }
  v8::Local<v8::Value> t;
  std::size_t current_element = 0;
  for (std::size_t i = 1; i < *size; i++) {
    bool valid = GetType(begin + i, end, &current_type);
    if (valid) {
      t = kParseFunctions[current_type](isolate,
          begin + i, end, &current_length);
      array->Set(current_element++, t);
      i += current_length;

      current_length = 0;
      if (begin[i] != ',' && begin[i] != ']') {
        isolate->ThrowException(v8::Exception::SyntaxError(
            v8::String::NewFromUtf8(isolate,
                "Invalid format in array: missed comma")));
        return v8::Array::New(isolate);
      } else if (begin[i] == ']') {
        *size = i + 1;
        break;
      }
    } else {
      isolate->ThrowException(v8::Exception::TypeError(
          v8::String::NewFromUtf8(isolate, "Invalid type in array")));
      return v8::Array::New(isolate);
    }
  }

  return array;
}

}  // namespace deserializer

namespace bindings {

void Stringify(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();

  if (args.Length() != 1) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Wrong number of arguments")));
    return;
  }

  v8::HandleScope scope(isolate);

  v8::Local<v8::String> result = jstp::jsrs::Stringify(isolate, args[0]);
  args.GetReturnValue().Set(result);
}

void Parse(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();

  if (args.Length() != 1) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Wrong number of arguments")));
    return;
  }
  if (!args[0]->IsString()) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, "Wrong argument type")));
    return;
  }

  v8::HandleScope scope(isolate);

  v8::String::Utf8Value str(args[0]->ToString());

  v8::Local<v8::Value> result = jstp::jsrs::Parse(isolate, str);
  args.GetReturnValue().Set(result);
}

void Init(v8::Local<v8::Object> target) {
  NODE_SET_METHOD(target, "stringify", Stringify);
  NODE_SET_METHOD(target, "parse", Parse);
}

NODE_MODULE(jsrs, Init);

}  // namespace bindings

}  // namespace jsrs

}  // namespace jstp
