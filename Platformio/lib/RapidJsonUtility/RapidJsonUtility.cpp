#include "RapidJsonUtilty.hpp"

#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#if defined(IS_SIMULATOR)
#include <fstream>
#else
// On-device (Rev1 and Rev5) the C++ std::fstream VFS path does not reliably map
// to the Arduino LittleFS mount, so read/write through the Arduino LittleFS API
// instead. This keeps the editor (config_http) and the firmware reader on one
// filesystem view. The simulator keeps std::fstream (runs on a PC).
#include <Arduino.h>
#include <LittleFS.h>
#include <string>
#endif

namespace OMOTE::JSON {

#if !defined(IS_SIMULATOR)
namespace {
// Arduino LittleFS.open() expects a path relative to the mount root
// ("/Scenes.json"), not the VFS path ("/littlefs/Scenes.json"). Strip the
// mount prefix if present so callers can keep passing FS_PATH-based paths.
std::string toLittleFsPath(const std::string &full) {
  static const std::string kPrefix = "/littlefs";
  std::string rel = full;
  if (rel.rfind(kPrefix, 0) == 0)
    rel = rel.substr(kPrefix.size());
  if (rel.empty())
    return "/";
  if (rel.front() != '/')
    rel.insert(rel.begin(), '/');
  return rel;
}

void ensureParentDir(const std::string &lfsPath) {
  const auto slash = lfsPath.find_last_of('/');
  if (slash == std::string::npos || slash == 0)
    return;
  const std::string dir = lfsPath.substr(0, slash);
  if (!LittleFS.exists(dir.c_str()))
    LittleFS.mkdir(dir.c_str());
}
} // namespace
#endif

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
#if defined(IS_SIMULATOR)
  std::ifstream file(aPathToJson);
  if (!file.is_open()) {
    return doc; // return empty doc if file couldn't be opened
  }
  rapidjson::IStreamWrapper fileStream(file);
  doc.ParseStream<rapidjson::ParseFlag::kParseCommentsFlag>(fileStream);
  return doc;
#else
  const std::string path = toLittleFsPath(aPathToJson.string());
  fs::File file = LittleFS.open(path.c_str(), "r");
  if (!file || file.isDirectory()) {
    if (file)
      file.close();
    return doc; // return empty doc if file couldn't be opened
  }
  std::string contents;
  contents.reserve(file.size());
  uint8_t buf[512];
  while (true) {
    const size_t n = file.read(buf, sizeof(buf));
    if (n == 0)
      break;
    contents.append(reinterpret_cast<const char *>(buf), n);
  }
  file.close();
  doc.Parse<rapidjson::ParseFlag::kParseCommentsFlag>(contents.c_str(), contents.size());
  return doc;
#endif
}

DocumentFileWriteResult WriteDocumentToFile(
    const rapidjson::Document &aDoc,
    const std::filesystem::path &aPathToJson,
    bool aPretty) {
  const std::string jsonStr = aPretty ? ToPrettyString(aDoc) : ToString(aDoc);

#if defined(IS_SIMULATOR)
  std::ofstream file(aPathToJson, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    return DocumentFileWriteResult::FileOpenError;
  }
  file << jsonStr;
  if (file.fail()) {
    return DocumentFileWriteResult::WriteError;
  }
  file.close();
  return DocumentFileWriteResult::Success;
#else
  const std::string path = toLittleFsPath(aPathToJson.string());
  ensureParentDir(path);
  fs::File file = LittleFS.open(path.c_str(), "w");
  if (!file) {
    return DocumentFileWriteResult::FileOpenError;
  }
  const size_t written =
      file.write(reinterpret_cast<const uint8_t *>(jsonStr.data()), jsonStr.size());
  file.close();
  if (written != jsonStr.size()) {
    return DocumentFileWriteResult::WriteError;
  }
  return DocumentFileWriteResult::Success;
#endif
}

} // namespace OMOTE::JSON