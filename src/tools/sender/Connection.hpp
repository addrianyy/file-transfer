#pragma once
#include "FileListing.hpp"

#include <net/protocol/ProtocolConnection.hpp>

#include <helpers/Hasher.hpp>
#include <helpers/TransferTracker.hpp>

#include <base/containers/BinaryBuffer.hpp>
#include <base/io/File.hpp>
#include <base/macro/ClassTraits.hpp>

#include <vector>

#include <zstd.h>

namespace sender {

class Connection : public net::ProtocolConnection {
  std::vector<FileListing::Entry> send_entries;
  size_t current_send_entry = 0;

  ZSTD_CCtx* compression_context{};

  enum class State {
    WaitingForHello,
    Idle,
    WaitingForDirectoryCreationAcknowledgement,
    WaitingForFileCreationAcknowledgement,
    WaitingForUploadAcknowledgement,
    Finished,
  };
  State state = State::WaitingForHello;

  struct Upload {
    base::File file;

    std::string virtual_path;
    std::string fs_path;

    uint64_t file_size = 0;

    bool is_compressed = false;
  };
  std::optional<Upload> upload;
  Hasher upload_hasher;
  TransferTracker upload_tracker;

  base::BinaryBuffer chunk_buffer;
  base::BinaryBuffer compression_buffer;

 protected:
  void on_error(ErrorType type, sock::Status status) override;
  void on_protocol_error(std::string_view description) override;
  void on_disconnected() override;

  bool should_compress_file(std::string_view fs_path, size_t size) const;

  void create_directory(std::string_view virtual_path);

  void start_file_upload(std::string_view virtual_path, const std::string& fs_path);
  void upload_accepted_file();

  void process_send_entry(size_t index);
  void process_first_send_entry();
  void process_next_send_entry();

  void on_handshake_finished();
  void on_directory_creation_accepted();
  void on_file_creation_accepted();
  void on_upload_accepted();

  void on_packet_received(const net::packets::ReceiverHello& packet) override;
  void on_packet_received(const net::packets::SenderHello& packet) override;
  void on_packet_received(const net::packets::Acknowledged& packet) override;
  void on_packet_received(const net::packets::CreateDirectory& packet) override;
  void on_packet_received(const net::packets::CreateFile& packet) override;
  void on_packet_received(const net::packets::FileChunk& packet) override;
  void on_packet_received(const net::packets::VerifyFile& packet) override;

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(Connection)

  explicit Connection(sock::StreamSocket socket, std::vector<FileListing::Entry> send_entries);
  ~Connection() override;

  void start();

  bool finished() const;
};

}  // namespace sender