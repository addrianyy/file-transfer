#pragma once
#include <net/Connection.hpp>
#include <net/protocol/Packet.hpp>

#include <binary/BinaryReaderWriter.hpp>

namespace net {

class ProtocolConnection : public Connection {
  void on_packet_received(BinaryReader reader) override;

  static void serialize_packet(BinaryWriter& writer, const packets::ReceiverHello& packet);
  static void serialize_packet(BinaryWriter& writer, const packets::SenderHello& packet);
  static void serialize_packet(BinaryWriter& writer, const packets::Acknowledged& packet);
  static void serialize_packet(BinaryWriter& writer, const packets::CreateDirectory& packet);
  static void serialize_packet(BinaryWriter& writer, const packets::CreateFile& packet);
  static void serialize_packet(BinaryWriter& writer, const packets::FileChunk& packet);
  static void serialize_packet(BinaryWriter& writer, const packets::VerifyFile& packet);

 protected:
  template <typename T>
  bool send_packet(const T& packet) {
    return Connection::send_packet([&](BinaryWriter& writer) {
      ProtocolConnection::serialize_packet(writer, packet);
      return true;
    });
  }

  void protocol_error(std::string_view description);

  virtual void on_protocol_error(std::string_view description) = 0;

  virtual void on_packet_received(const packets::ReceiverHello& packet) = 0;
  virtual void on_packet_received(const packets::SenderHello& packet) = 0;
  virtual void on_packet_received(const packets::Acknowledged& packet) = 0;
  virtual void on_packet_received(const packets::CreateDirectory& packet) = 0;
  virtual void on_packet_received(const packets::CreateFile& packet) = 0;
  virtual void on_packet_received(const packets::FileChunk& packet) = 0;
  virtual void on_packet_received(const packets::VerifyFile& packet) = 0;

 public:
  explicit ProtocolConnection(sock::SocketStream socket);
};

}  // namespace net