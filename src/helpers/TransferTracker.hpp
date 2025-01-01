#pragma once
#include <base/time/PreciseTime.hpp>

#include <functional>

class TransferTracker {
  constexpr static size_t sample_window_in_seconds = 5;
  constexpr static size_t samples_per_second = 20;
  constexpr static size_t max_sample_count = sample_window_in_seconds * samples_per_second;

  constexpr static base::PreciseTime sampling_interval =
    base::PreciseTime::from_seconds(1.f / float(samples_per_second));
  constexpr static base::PreciseTime reporting_interval = base::PreciseTime::from_seconds(1.f);

  std::string transfer_verb;
  std::function<void(std::string_view)> display_callback;

  struct State {
    std::string name;
    uint64_t transferred_size{};
    uint64_t transferred_compressed_size{};
    uint64_t total_size{};
    bool is_compressed{};
    base::PreciseTime start_time{};
    base::PreciseTime last_report_time{};
    base::PreciseTime last_sample_time{};
  };
  State state_{};

  struct Sample {
    base::PreciseTime time{};
    uint64_t transferred_size{};
  };
  std::vector<Sample> samples;
  size_t next_sample_index = 0;

  void add_sample(const Sample& sample);

  bool get_min_max_sample(Sample& min_sample, Sample& max_sample) const;

  double calculate_download_speed(base::PreciseTime now) const;

 public:
  explicit TransferTracker(std::string transfer_verb,
                           std::function<void(std::string_view)> display_callback);

  void begin(const std::string& transfer_name, uint64_t transfer_size, bool is_compressed);
  void progress(uint64_t chunk_size, uint64_t compressed_size);
  void end();
};