#include <iostream>

#include "chat.pb.h"

int main() {
  im::chat::v1::ChatMessage msg;
  msg.set_message_id(1);
  std::cout << "ChatMessage compiled OK, message_id=" << msg.message_id() << "\n";
  return 0;
}
