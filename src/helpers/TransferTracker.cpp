#include "TransferTracker.hpp"
#include "SizeFormatter.hpp"

#include <base/text/Format.hpp>

#include <algorithm>

void TransferTracker::add_sample(const Sample& sample) {
  if (samples.size() < max_sample_count) {
    samples.push_back(sample);
  } else {
    samples[next_sample_index] = sample;
    next_sample_index++;
    if (next_sample_index >= max_sample_count) {
      next_sample_index = 0;
    }
  }
}

bool TransferTracker::get_min_max_sample(Sample& min_sample, Sample& max_sample) const {
  if (samples.size() < 2) {
    return false;
  }

  if (samples.size() < max_sample_count) {
    min_sample = samples[0];
    max_sample = samples[samples.size() - 1];
    return true;
  }

  const size_t min_sample_index = next_sample_index;
  const size_t max_sample_index =
    next_sample_index == 0 ? samples.size() - 1 : next_sample_index - 1;

  min_sample = samples[min_sample_index];
  max_sample = samples[max_sample_index];

  return true;
}

double TransferTracker::calculate_download_speed(base::PreciseTime now) const {
  // Calculate download speed using moving average.
  Sample min_sample, max_sample;
  if (!get_min_max_sample(min_sample, max_sample)) {
    // Not enough samples for moving average.
    const auto transfer_time = now - state_.start_time;
    const auto download_speed =
      double(state_.transferred_size) / std::max(transfer_time.seconds(), 0.0001);

    return download_speed;
  }

  auto newest_sample_time = max_sample.time;
  const auto time_since_newest_sample = now - newest_sample_time;

  // No samples were received in the sampling window.
  if (time_since_newest_sample >= base::PreciseTime::from_seconds(sample_window_in_seconds + 1)) {
    return 0.0;
  }

  // Too long passed since last sample, take it into account.
  if (time_since_newest_sample >= base::PreciseTime::from_seconds(0.25f)) {
    newest_sample_time = now;
  }

  const auto transfer_time = newest_sample_time - min_sample.time;
  const auto transfered_size = max_sample.transferred_size - min_sample.transferred_size;

  return double(transfered_size) / std::max(transfer_time.seconds(), 0.0001);
}

void TransferTracker::begin(const std::string& transfer_name,
                            uint64_t transfer_size,
                            bool is_compressed) {
  const auto now = base::PreciseTime::now();

  state_ = {
    .name = transfer_name,
    .transferred_size = 0,
    .total_size = transfer_size,
    .is_compressed = is_compressed,
    .start_time = now,
    .last_report_time = now,
    .last_sample_time = now,
  };

  {
    samples.clear();
    next_sample_index = 0;
  }

  {
    const auto [readable_size, size_units] =
      SizeFormatter::bytes_to_readable_units(state_.total_size);
    display_callback(base::format("{} file `{}` {}({:.1f} {})...", transfer_verb, state_.name,
                                  is_compressed ? "[compressed] " : "", readable_size, size_units));
  }
}

void TransferTracker::progress(uint64_t chunk_size, uint64_t compressed_size) {
  const auto now = base::PreciseTime::now();

  state_.transferred_size += chunk_size;
  state_.transferred_compressed_size += compressed_size;

  if (now - state_.last_sample_time >= sampling_interval) {
    add_sample(Sample{
      .time = now,
      .transferred_size = state_.transferred_size,
    });
    state_.last_sample_time = now;
  }

  if (now - state_.last_report_time >= reporting_interval) {
    const auto transfer_percentage =
      (float(state_.transferred_size) / float(state_.total_size)) * 100.f;

    const auto [readable_transferred_size, transferred_size_units] =
      SizeFormatter::bytes_to_readable_units(state_.transferred_size);
    const auto [readable_total_size, total_size_units] =
      SizeFormatter::bytes_to_readable_units(state_.total_size);

    const auto download_speed = calculate_download_speed(now);

    const auto [readable_speed, speed_units] =
      SizeFormatter::bytes_to_readable_units(uint64_t(download_speed));

    const auto remaining_size = double(state_.total_size - state_.transferred_size);
    const auto remaining_time =
      base::PreciseTime::from_seconds(remaining_size / std::max(double(download_speed), 1.0));

    display_callback(base::format("`{}`: {:.1f}% - {:.1f}{}/{:.1f}{} - {:.1f} {}/s - remaining {}",
                                  state_.name, transfer_percentage, readable_transferred_size,
                                  transferred_size_units, readable_total_size, total_size_units,
                                  readable_speed, speed_units, remaining_time));

    state_.last_report_time = now;
  }
}

void TransferTracker::end() {
  {
    const auto now = base::PreciseTime::now();

    const auto transfer_time = now - state_.start_time;
    const auto transfer_speed =
      float(state_.total_size) / std::max(transfer_time.seconds(), 0.0001);

    const auto [readable_size, size_units] =
      SizeFormatter::bytes_to_readable_units(state_.total_size);
    const auto [readable_speed, speed_units] =
      SizeFormatter::bytes_to_readable_units(uint64_t(transfer_speed));

    std::string compression_info{};
    if (state_.is_compressed) {
      const auto compression_ratio =
        state_.total_size == 0
          ? 0
          : (float(state_.transferred_compressed_size) / float(state_.total_size));
      compression_info = base::format(", compression {:.1f}%", compression_ratio * 100.f);
    }

    display_callback(base::format("finished {} file `{}` ({:.1f} {}) in {} ({:.1f} {}/s){}",
                                  transfer_verb, state_.name, readable_size, size_units,
                                  transfer_time, readable_speed, speed_units, compression_info));
  }

  state_ = {};
}

TransferTracker::TransferTracker(std::string transfer_verb,
                                 std::function<void(std::string_view)> display_callback)
    : transfer_verb(std::move(transfer_verb)), display_callback(std::move(display_callback)) {}