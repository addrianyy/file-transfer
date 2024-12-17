#include "ProtocolConnection.hpp"

#include <base/text/Format.hpp>

static void write_packet_id(BinaryWriter& writer, net::PacketID packet_id) {
  writer.write_u16(uint16_t(packet_id));
}

static void write_string(BinaryWriter& writer, std::string_view string) {
  writer.write_bytes({reinterpret_cast<const uint8_t*>(string.data()), string.size()});
}

static bool read_string(BinaryReader& reader, std::string_view& string) {
  const auto size = reader.remaining_size();
  std::span<const uint8_t> bytes;
  if (!reader.read_bytes(size, bytes)) {
    return false;
  }
  string = std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  return true;
}

void net::ProtocolConnection::protocol_error(std::string_view description) {
  set_not_alive();
  on_protocol_error(description);
}

void net::ProtocolConnection::on_packet_received(BinaryReader reader) {
  uint16_t packet_id_u16{};
  if (!reader.read_u16(packet_id_u16)) {
    return protocol_error("failed to deserialize packet id");
  }

  const auto dispatch = [&, this](const auto& packet) {
    if (reader.remaining_size() == 0) {
      on_packet_received(packet);
    } else {
      protocol_error("failed to consume whole packet");
    }
  };

  const auto packet_id = PacketID{packet_id_u16};
  switch (packet_id) {
    case PacketID::ReceiverHello: {
      return dispatch(packets::ReceiverHello{});
    }
    case PacketID::SenderHello: {
      return dispatch(packets::SenderHello{});
    }
    case PacketID::Acknowledged: {
      uint8_t accepted{};
      if (!reader.read_u8(accepted)) {
        return protocol_error("failed to deserialize acknowledge packet");
      }
      return dispatch(packets::Acknowledged{
        .accepted = accepted != 0,
      });
    }
    case PacketID::CreateDirectory: {
      std::string_view path;
      if (!read_string(reader, path)) {
        return protocol_error("failed to deserialize create directory packet");
      }
      return dispatch(packets::CreateDirectory{
        .path = path,
      });
    }
    case PacketID::CreateFile: {
      uint64_t size{};
      if (!reader.read_u64(size)) {
        return protocol_error("failed to deserialize create file packet size");
      }

      std::string_view path;
      if (!read_string(reader, path)) {
        return protocol_error("failed to deserialize create file packet path");
      }

      return dispatch(packets::CreateFile{
        .path = path,
        .size = size,
      });
    }
    case PacketID::FileChunk: {
      const auto size = reader.remaining_size();
      std::span<const uint8_t> data;
      if (!reader.read_bytes(size, data)) {
        return protocol_error("failed to deserialize file chunk packet");
      }
      return dispatch(packets::FileChunk{
        .data = data,
      });
    }
    default: {
      return protocol_error(base::format("invalid packet id {}", packet_id_u16));
    }
  }
}

void net::ProtocolConnection::serialize_packet(BinaryWriter& writer,
                                               const packets::ReceiverHello& packet) {
  write_packet_id(writer, PacketID::ReceiverHello);
}
void net::ProtocolConnection::serialize_packet(BinaryWriter& writer,
                                               const packets::SenderHello& packet) {
  write_packet_id(writer, PacketID::SenderHello);
}
void net::ProtocolConnection::serialize_packet(BinaryWriter& writer,
                                               const packets::Acknowledged& packet) {
  write_packet_id(writer, PacketID::Acknowledged);
  writer.write_u8(packet.accepted ? 1 : 0);
}
void net::ProtocolConnection::serialize_packet(BinaryWriter& writer,
                                               const packets::CreateDirectory& packet) {
  write_packet_id(writer, PacketID::CreateDirectory);
  write_string(writer, packet.path);
}
void net::ProtocolConnection::serialize_packet(BinaryWriter& writer,
                                               const packets::CreateFile& packet) {
  write_packet_id(writer, PacketID::CreateFile);
  writer.write_u64(packet.size);
  write_string(writer, packet.path);
}
void net::ProtocolConnection::serialize_packet(BinaryWriter& writer,
                                               const packets::FileChunk& packet) {
  write_packet_id(writer, PacketID::FileChunk);
  writer.write_bytes(packet.data);
}

net::ProtocolConnection::ProtocolConnection(std::unique_ptr<sock::SocketStream> socket)
    : Connection(std::move(socket)) {}