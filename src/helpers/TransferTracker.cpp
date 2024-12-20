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

double TransferTracker::calculate_download_speed(base::PreciseTime now) const {
  if (samples.size() < 2) {
    // Not enough samples for moving average.
    const auto transfer_time = now - state_.start_time;
    const auto download_speed =
      double(state_.transferred_size) / std::max(transfer_time.seconds(), 0.0001);

    return download_speed;
  }

  // Calculate download speed using moving average.
  // TODO: Remove old entries.

  base::PreciseTime min_time = samples[0].time;
  base::PreciseTime max_time = min_time;

  uint64_t min_size = samples[0].transferred_size;
  uint64_t max_size = min_size;

  for (const auto& sample : samples) {
    min_time = std::min(min_time, sample.time);
    max_time = std::max(max_time, sample.time);

    min_size = std::min(min_size, sample.transferred_size);
    max_size = std::max(max_size, sample.transferred_size);
  }

  const auto transfer_time = max_time - min_time;
  const auto transfered_size = max_size - min_size;

  return double(transfered_size) / std::max(transfer_time.seconds(), 0.0001);
}

void TransferTracker::begin(const std::string& transfer_name, uint64_t transfer_size) {
  const auto now = base::PreciseTime::now();

  state_ = {
    .name = transfer_name,
    .transferred_size = 0,
    .total_size = transfer_size,
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
    display_callback(base::format("{} file `{}` ({:.1f} {})...", transfer_verb, state_.name,
                                  readable_size, size_units));
  }
}

void TransferTracker::progress(uint64_t chunk_size) {
  const auto now = base::PreciseTime::now();

  state_.transferred_size += chunk_size;

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

    display_callback(base::format("finished {} file `{}` ({:.1f} {}) in {} ({:.1f} {}/s)",
                                  transfer_verb, state_.name, readable_size, size_units,
                                  transfer_time, readable_speed, speed_units));
  }

  state_ = {};
}

TransferTracker::TransferTracker(std::string transfer_verb,
                                 std::function<void(std::string_view)> display_callback)
    : transfer_verb(std::move(transfer_verb)), display_callback(std::move(display_callback)) {}