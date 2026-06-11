#include "RapidJsonUtilty.hpp"

#include "rapidjson/allocators.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <fstream>
#include <vector>

#if defined(ARDUINO) && !defined(IS_SIMULATOR)
#include <Arduino.h>
#include <LittleFS.h>
#endif

namespace OMOTE::JSON {

namespace {

constexpr auto kParseFileFlags =
    rapidjson::ParseFlag::kParseCommentsFlag | rapidjson::ParseFlag::kParseIterativeFlag;

#if defined(ARDUINO) && !defined(IS_SIMULATOR)

char sJsonParsePool[32768];
rapidjson::MemoryPoolAllocator<> sJsonParseAlloc(sJsonParsePool, sizeof(sJsonParsePool));

uint32_t fnv1aHash(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

bool readLittleFsJson(const std::filesystem::path &aPathToJson, rapidjson::Document &doc) {
  const std::string path = aPathToJson.string();
  File f = LittleFS.open(path.c_str(), "r");
  if (!f)
    return false;
  const size_t sz = f.size();
  if (!sz || sz > 65536)
    return false;
  std::vector<char> buf(sz + 1);
  size_t n = 0;
  while (n < sz) {
    const int got = f.read(reinterpret_cast<uint8_t *>(buf.data()) + n, sz - n);
    if (got <= 0)
      break;
    n += static_cast<size_t>(got);
  }
  f.close();
  if (n != sz) {
    Serial.printf("[JSON] short read %s (%u/%u bytes)\n", path.c_str(), static_cast<unsigned>(n),
                  static_cast<unsigned>(sz));
    return false;
  }
  buf[sz] = '\0';
  const char *text = buf.data();
  size_t len = sz;
  if (len >= 3 && static_cast<uint8_t>(text[0]) == 0xEF && static_cast<uint8_t>(text[1]) == 0xBB &&
      static_cast<uint8_t>(text[2]) == 0xBF) {
    text += 3;
    len -= 3;
  }
  sJsonParseAlloc.Clear();
  rapidjson::Document tmp(&sJsonParseAlloc);
  tmp.Parse<kParseFileFlags>(text, len);
  if (tmp.HasParseError()) {
    const size_t errOff = tmp.GetErrorOffset();
    Serial.printf("[JSON] parse fail %s err=%u@%u hash=%08x heap=%u\n", path.c_str(),
                  static_cast<unsigned>(tmp.GetParseError()), static_cast<unsigned>(errOff),
                  fnv1aHash(reinterpret_cast<const uint8_t *>(text), len),
                  static_cast<unsigned>(ESP.getFreeHeap()));
    if (errOff < len) {
      const size_t start = errOff > 16 ? errOff - 16 : 0;
      const size_t end = std::min(len, errOff + 16);
      Serial.printf("[JSON] bytes@%u: ", static_cast<unsigned>(start));
      for (size_t i = start; i < end; ++i)
        Serial.printf("%02x ", static_cast<uint8_t>(text[i]));
      Serial.println();
    }
    doc.SetNull();
    return false;
  }
  doc.SetObject();
  doc.RemoveAllMembers();
  doc.CopyFrom(tmp, doc.GetAllocator());
  return true;
}
#endif

} // namespace

std::string ToString(const rapidjson::Document &aDoc) {
  rapidjson::StringBuffer buff;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buff);
  aDoc.Accept(writer);
  return std::string(buff.GetString());
}

std::string ToPrettyString(const rapidjson::Document &aDoc) {
  rapidjson::StringBuffer buff;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> prettyWrite(buff);
  prettyWrite.SetIndent(' ', 2);
  aDoc.Accept(prettyWrite);
  return std::string(buff.GetString());
}

const rapidjson::Value *GetNestedField(
    const rapidjson::Value &aValue, const std::vector<std::string> &aFields) {
  const rapidjson::Value *value = &aValue;
  for (const auto &field : aFields) {
    if (!value || !value->IsObject() || !value->HasMember(field.c_str())) {
      return nullptr;
    }
    value = &(*value)[field.c_str()];
  }
  return value;
}

rapidjson::Document GetDocument(
    const std::string &aStringToParse) {
  rapidjson::Document doc;
  doc.Parse<rapidjson::ParseFlag::kParseCommentsFlag>(aStringToParse.c_str());
  return doc;
}

rapidjson::Document GetDocument(const std::string_view &aStringToParse) {
  rapidjson::Document doc;
  doc.Parse<rapidjson::ParseFlag::kParseCommentsFlag>(aStringToParse.data(), aStringToParse.size());
  return doc;
}

rapidjson::Document GetDocument(const std::filesystem::path &aPathToJson) {
  rapidjson::Document doc;
#if defined(ARDUINO) && !defined(IS_SIMULATOR)
  readLittleFsJson(aPathToJson, doc);
  return doc;
#else
  std::ifstream file(aPathToJson);
  if (!file.is_open())
    return doc;
  rapidjson::IStreamWrapper fileStream(file);
  doc.ParseStream<kParseFileFlags>(fileStream);
  return doc;
#endif
}

DocumentFileWriteResult WriteDocumentToFile(
    const rapidjson::Document &aDoc,
    const std::filesystem::path &aPathToJson,
    bool aPretty) {
  std::ofstream file(aPathToJson, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    return DocumentFileWriteResult::FileOpenError;
  }

  std::string jsonStr;
  jsonStr = aPretty ? ToPrettyString(aDoc) : jsonStr = ToString(aDoc);

  file << jsonStr;
  if (file.fail()) {
    return DocumentFileWriteResult::WriteError;
  }

  file.close();
  return DocumentFileWriteResult::Success;
}

} // namespace OMOTE::JSON