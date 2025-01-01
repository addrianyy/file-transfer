#pragma once
#include <net/protocol/ProtocolConnection.hpp>

#include <helpers/ByteBuffer.hpp>
#include <helpers/Hasher.hpp>
#include <helpers/TransferTracker.hpp>

#include <base/io/File.hpp>
#include <base/macro/ClassTraits.hpp>

#include <optional>

#include <zstd.h>

namespace receiver {

class Connection : public net::ProtocolConnection {
  std::string peer_address{};
  std::string receive_directory{};

  ZSTD_DCtx* decompression_context{};

  enum class State {
    WaitingForHello,
    Idle,
    Downloading,
    WaitingForHash,
  };
  State state = State::WaitingForHello;

  struct Download {
    base::File file;

    std::string virtual_path;
    std::string fs_path;

    uint64_t file_size = 0;
    uint64_t downloaded_size = 0;

    bool is_compressed = false;
  };
  std::optional<Download> download;
  Hasher download_hasher;
  TransferTracker download_tracker;

  ByteBuffer decompression_buffer;

 protected:
  void cleanup();
  void on_error(ErrorType type, sock::Status status) override;
  void on_protocol_error(std::string_view description) override;
  void on_disconnected() override;

  bool to_fs_path(std::string_view virtual_path, std::string& fs_path);

  bool create_directory(std::string_view virtual_path);
  bool start_file_download(std::string_view virtual_path, uint64_t file_size, uint16_t flags);
  void process_downloaded_chunk(std::span<const uint8_t> download_chunk);
  void finish_chunks_download();
  bool verify_file(uint64_t hash);

  void on_packet_received(const net::packets::ReceiverHello& packet) override;
  void on_packet_received(const net::packets::SenderHello& packet) override;
  void on_packet_received(const net::packets::Acknowledged& packet) override;
  void on_packet_received(const net::packets::CreateDirectory& packet) override;
  void on_packet_received(const net::packets::CreateFile& packet) override;
  void on_packet_received(const net::packets::FileChunk& packet) override;
  void on_packet_received(const net::packets::VerifyFile& packet) override;

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(Connection)

  explicit Connection(sock::StreamSocket socket,
                      std::string peer_address,
                      std::string receive_directory);
  ~Connection() override;
};

}  // namespace receiver