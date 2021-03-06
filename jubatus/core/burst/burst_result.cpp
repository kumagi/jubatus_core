// Jubatus: Online machine learning framework for distributed environment
// Copyright (C) 2014 Preferred Infrastructure and Nippon Telegraph and Telephone Corporation.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License version 2.1 as published by the Free Software Foundation.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#include "burst_result.hpp"

#include <vector>
#include <utility>

#include "input_window.hpp"
#include "result_window.hpp"
#include "engine.hpp"
#include "window_intersection.hpp"

using jubatus::util::lang::shared_ptr;

namespace jubatus {
namespace core {
namespace burst {

namespace {

int accumulate_d_vec(const burst_result& r) {
  const std::vector<batch_result>& batches = r.get_batches();
  int result = 0;
  for (size_t i = 0; i < batches.size(); ++i) {
    result += batches[i].d;
  }
  return result;
}

}  // namespace

burst_result::burst_result() {
}

burst_result::burst_result(
    const input_window& input,
    double scaling_param,
    double gamma,
    double costcut_threshold,
    const burst_result& prev_result,
    int max_reuse_batches) {
  const std::vector<batch_input>& input_batches = input.get_batches();
  const size_t n = input.get_batch_size();
  const int max_reuse = (std::min)(max_reuse_batches, static_cast<int>(n));

  // make vectors for engine
  std::vector<uint32_t> d_vec, r_vec;
  std::vector<double> burst_weights;
  d_vec.reserve(n);
  r_vec.reserve(n);
  burst_weights.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    d_vec.push_back(input_batches[i].d);
    r_vec.push_back(input_batches[i].r);
    burst_weights.push_back(-1);  // uncalculated
  }

  // reuse batch weights
  if (prev_result.p_) {
    const result_window& prev = *prev_result.p_;
    if (prev.get_start_pos() <= input.get_start_pos()) {
      const std::pair<int, int> intersection = get_intersection(prev, input);
      const std::vector<batch_result>& prev_results = prev.get_batches();
      for (int i = 0, j = intersection.first;
           i < max_reuse && j < intersection.second;
           ++i, ++j) {
        burst_weights[i] = prev_results[j].burst_weight;
      }
    }
  }

  // doit
  burst::burst_detect(d_vec, r_vec, burst_weights,
                      scaling_param, gamma, costcut_threshold);

  // store result
  p_.reset(new result_window(input, burst_weights));
}

burst_result::burst_result(const result_window& src)
    : p_(new result_window(src)) {
}

bool burst_result::is_valid() const {
  return p_.get() != NULL;
}

const double burst_result::invalid_pos = -1;

double burst_result::get_start_pos() const {
  return p_ ? p_->get_start_pos() : invalid_pos;
}
double burst_result::get_end_pos() const {
  return p_ ? p_->get_end_pos() : invalid_pos;
}
bool burst_result::contains(double pos) const {
  return p_ && p_->contains(pos);
}

int burst_result::get_batch_size() const {
  return p_ ? p_->get_batch_size() : 0;
}
double burst_result::get_batch_interval() const {
  return p_ ? p_->get_batch_interval() : 1;
}
double burst_result::get_all_interval() const {
  return p_ ? p_->get_all_interval() : 0;
}

bool burst_result::has_start_pos_older_than(double pos) const {
  if (!p_) {
    return false;
  }
  double pos0 = p_->get_start_pos();
  if (pos0 < pos) {
    return !window_position_near(pos0, pos, p_->get_batch_interval());
  } else {
    return false;
  }
}
bool burst_result::has_start_pos_newer_than(double pos) const {
  if (!p_) {
    return false;
  }
  double pos0 = p_->get_start_pos();
  if (pos0 > pos) {
    return !window_position_near(pos0, pos, p_->get_batch_interval());
  } else {
    return false;
  }
}
bool burst_result::has_same_start_pos_to(double pos) const {
  if (!p_) {
    return false;
  }
  double pos0 = p_->get_start_pos();
  return window_position_near(pos0, pos, p_->get_batch_interval());
}

bool burst_result::has_same_batch_interval(const burst_result& x) const {
  if (!p_ || !x.p_) {
    return false;
  }
  double interval_x = x.p_->get_batch_interval();
  return intersection_helper(*p_).has_batch_interval_equals_to(interval_x);
}

const std::vector<batch_result> empty_batch_results;

const std::vector<batch_result>& burst_result::get_batches() const {
  return p_ ? p_->get_batches() : empty_batch_results;
}

const batch_result& burst_result::get_batch_at(double pos) const {
  int i = p_ ? p_->get_index(pos) : -1;
  if (i < 0) {
    throw std::out_of_range("burst_result: pos is out of range");
  }
  return p_->get_batches()[i];
}

bool burst_result::is_bursted_at(double pos) const {
  int i = p_ ? p_->get_index(pos) : -1;
  if (i < 0) {
    return false;
  }
  return p_->get_batches()[i].is_bursted();
}

bool burst_result::is_bursted_at_latest_batch() const {
  if (!p_) {
    return false;
  }
  const std::vector<batch_result>& batches = p_->get_batches();
  return !batches.empty() && batches.back().is_bursted();
}

bool burst_result::mix(const burst_result& w) {
  if (!has_same_start_pos_to(w.get_start_pos()) ||
      !has_same_batch_interval(w) ||
      get_batch_size() != w.get_batch_size()) {
    return false;
  }

  if (p_ != w.p_) {
    if (accumulate_d_vec(*this) < accumulate_d_vec(w)) {
      p_ = w.p_;
    }
  }

  return true;
}

void burst_result::msgpack_pack(framework::packer& packer) const {
  if (!p_) {
    result_window r(invalid_pos);
    packer.pack(r);
  } else {
    packer.pack(*p_);
  }
}

void burst_result::msgpack_unpack(msgpack::object o) {
  shared_ptr<result_window> unpacked(new result_window());
  unpacked->msgpack_unpack(o);
  p_ = unpacked;
}

}  // namespace burst
}  // namespace core
}  // namespace jubatus
