// Archived, excluded from the build (no longer referenced by CMakeLists.txt).
// chat.proto now sets `optimize_for = LITE_RUNTIME`, so generated ChatMessage
// no longer derives from google::protobuf::Message and has no
// GetReflection()/GetDescriptor(), and there is no DynamicMessageFactory
// support either. Kept only as a reference for what full-runtime reflection
// looked like before the lite switch.
#if 0
// Demonstrates google::protobuf reflection: walking a message's fields
// purely through its Descriptor/Reflection, without switching on the
// generated C++ class. The same PrintMessage() works both on a normal
// ChatMessage instance and on a DynamicMessage built only from the
// descriptor pool, which is the clearest proof that the printer never
// touches generated code.
#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "chat.pb.h"
#include "message_fixtures.h"

namespace {

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

std::string Indent(int depth) { return std::string(depth * 2, ' '); }

void PrintMessage(std::ostream& out, const Message& msg, int depth);

void PrintScalar(std::ostream& out, const Message& msg, const Reflection* refl,
                  const FieldDescriptor* fd) {
  switch (fd->cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      out << refl->GetInt32(msg, fd);
      break;
    case FieldDescriptor::CPPTYPE_INT64:
      out << refl->GetInt64(msg, fd);
      break;
    case FieldDescriptor::CPPTYPE_UINT32:
      out << refl->GetUInt32(msg, fd);
      break;
    case FieldDescriptor::CPPTYPE_UINT64:
      out << refl->GetUInt64(msg, fd);
      break;
    case FieldDescriptor::CPPTYPE_FLOAT:
      out << refl->GetFloat(msg, fd);
      break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
      out << refl->GetDouble(msg, fd);
      break;
    case FieldDescriptor::CPPTYPE_BOOL:
      out << (refl->GetBool(msg, fd) ? "true" : "false");
      break;
    case FieldDescriptor::CPPTYPE_STRING:
      out << '"' << refl->GetString(msg, fd) << '"';
      break;
    case FieldDescriptor::CPPTYPE_ENUM:
      out << refl->GetEnum(msg, fd)->name();
      break;
    default:
      out << "<unhandled cpp_type>";
  }
}

void PrintRepeatedScalar(std::ostream& out, const Message& msg, const Reflection* refl,
                          const FieldDescriptor* fd, int index) {
  switch (fd->cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      out << refl->GetRepeatedInt32(msg, fd, index);
      break;
    case FieldDescriptor::CPPTYPE_INT64:
      out << refl->GetRepeatedInt64(msg, fd, index);
      break;
    case FieldDescriptor::CPPTYPE_UINT32:
      out << refl->GetRepeatedUInt32(msg, fd, index);
      break;
    case FieldDescriptor::CPPTYPE_UINT64:
      out << refl->GetRepeatedUInt64(msg, fd, index);
      break;
    case FieldDescriptor::CPPTYPE_FLOAT:
      out << refl->GetRepeatedFloat(msg, fd, index);
      break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
      out << refl->GetRepeatedDouble(msg, fd, index);
      break;
    case FieldDescriptor::CPPTYPE_BOOL:
      out << (refl->GetRepeatedBool(msg, fd, index) ? "true" : "false");
      break;
    case FieldDescriptor::CPPTYPE_STRING:
      out << '"' << refl->GetRepeatedString(msg, fd, index) << '"';
      break;
    case FieldDescriptor::CPPTYPE_ENUM:
      out << refl->GetRepeatedEnum(msg, fd, index)->name();
      break;
    default:
      out << "<unhandled cpp_type>";
  }
}

// A map<K, V> field is, under the hood, a repeated message field whose
// entry type has exactly two fields: "key" (number 1) and "value" (number 2).
void PrintMapEntry(std::ostream& out, const Message& entry, int depth) {
  const Reflection* refl = entry.GetReflection();
  const auto* desc = entry.GetDescriptor();
  const FieldDescriptor* key_fd = desc->FindFieldByNumber(1);
  const FieldDescriptor* value_fd = desc->FindFieldByNumber(2);

  out << Indent(depth) << "\"" << refl->GetString(entry, key_fd) << "\" => ";
  if (value_fd->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
    out << "\n";
    PrintMessage(out, refl->GetMessage(entry, value_fd), depth + 1);
  } else {
    PrintScalar(out, entry, refl, value_fd);
    out << "\n";
  }
}

void PrintField(std::ostream& out, const Message& msg, const Reflection* refl,
                const FieldDescriptor* fd, int depth) {
  out << Indent(depth) << fd->cpp_type_name() << " " << fd->name() << " = " << fd->number();
  if (const auto* oneof = fd->real_containing_oneof()) {
    out << " [oneof " << oneof->name() << "]";
  }

  if (fd->is_map()) {
    int count = refl->FieldSize(msg, fd);
    out << " (map, " << count << " entries) {\n";
    for (int i = 0; i < count; ++i) {
      PrintMapEntry(out, refl->GetRepeatedMessage(msg, fd, i), depth + 1);
    }
    out << Indent(depth) << "}\n";
    return;
  }

  if (fd->is_repeated()) {
    int count = refl->FieldSize(msg, fd);
    out << " (repeated, " << count << " elements) [\n";
    for (int i = 0; i < count; ++i) {
      out << Indent(depth + 1) << "[" << i << "] ";
      if (fd->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
        out << "\n";
        PrintMessage(out, refl->GetRepeatedMessage(msg, fd, i), depth + 2);
      } else {
        PrintRepeatedScalar(out, msg, refl, fd, i);
        out << "\n";
      }
    }
    out << Indent(depth) << "]\n";
    return;
  }

  out << ": ";
  if (fd->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
    out << "\n";
    PrintMessage(out, refl->GetMessage(msg, fd), depth + 1);
  } else {
    PrintScalar(out, msg, refl, fd);
    out << "\n";
  }
}

void PrintMessage(std::ostream& out, const Message& msg, int depth) {
  const auto* descriptor = msg.GetDescriptor();
  const Reflection* refl = msg.GetReflection();

  out << Indent(depth) << descriptor->full_name() << " {\n";

  // ListFields() only returns fields that are actually set: singular fields
  // for which HasField() is true, and repeated/map fields with size > 0.
  // This is what skips unset oneof branches and absent optional submessages.
  std::vector<const FieldDescriptor*> set_fields;
  refl->ListFields(msg, &set_fields);
  for (const FieldDescriptor* fd : set_fields) {
    PrintField(out, msg, refl, fd, depth + 1);
  }

  out << Indent(depth) << "}\n";
}

}  // namespace

int main() {
  using im::chat::v1::ChatMessage;

  ChatMessage msg = BuildMergedForwardMessage();
  (*msg.mutable_extra())["locale"] = "zh-CN";
  (*msg.mutable_extra())["client"] = "android";

  std::cout << "== 1) Walking a statically-generated ChatMessage via Reflection ==\n\n";
  PrintMessage(std::cout, msg, 0);

  std::cout << "\n"
            << "== 2) Same walk over a DynamicMessage built only from the descriptor "
               "pool (no chat.pb.h types touched) ==\n\n";

  const auto* descriptor =
      google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
          "im.chat.v1.ChatMessage");
  if (descriptor == nullptr) {
    std::cerr << "descriptor lookup for im.chat.v1.ChatMessage failed\n";
    return 1;
  }

  google::protobuf::DynamicMessageFactory factory;
  std::unique_ptr<google::protobuf::Message> dynamic_msg(
      factory.GetPrototype(descriptor)->New());

  std::string serialized;
  if (!msg.SerializeToString(&serialized) || !dynamic_msg->ParseFromString(serialized)) {
    std::cerr << "failed to round-trip ChatMessage into the DynamicMessage\n";
    return 1;
  }

  PrintMessage(std::cout, *dynamic_msg, 0);

  return 0;
}
#endif  // 0
