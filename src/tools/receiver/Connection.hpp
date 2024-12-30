#pragma once
#include <net/protocol/ProtocolConnection.hpp>

#include <helpers/Hasher.hpp>
#include <helpers/TransferTracker.hpp>

#include <base/io/File.hpp>

#include <optional>

namespace receiver {

class Connection : public net::ProtocolConnection {
  std::string peer_address{};
  std::string receive_directory{};

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
  };
  std::optional<Download> download;
  Hasher download_hasher;
  TransferTracker download_tracker;

 protected:
  void cleanup();
  void on_error(ErrorType type, sock::Status status) override;
  void on_protocol_error(std::string_view description) override;
  void on_disconnected() override;

  bool to_fs_path(std::string_view virtual_path, std::string& fs_path);

  bool create_directory(std::string_view virtual_path);
  bool start_file_download(std::string_view virtual_path, uint64_t file_size);
  void process_downloaded_chunk(std::span<const uint8_t> chunk);
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
  explicit Connection(sock::SocketStream socket,
                      std::string peer_address,
                      std::string receive_directory);
};

}  // namespace receiver