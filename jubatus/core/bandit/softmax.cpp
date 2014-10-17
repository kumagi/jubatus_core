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

#include "softmax.hpp"

#include <string>
#include <vector>
#include <cmath>
#include <numeric>
#include "../common/exception.hpp"
#include "select_by_weights.hpp"

namespace jubatus {
namespace core {
namespace bandit {

softmax::softmax(
    const jubatus::util::lang::shared_ptr<storage>& s,
    double tau)
    : bandit_base(s), tau_(tau) {
  if (tau <= 0) {
    throw JUBATUS_EXCEPTION(
        common::invalid_parameter("0 < tau"));
  }
}

std::string softmax::select_arm(const std::string& player_id) {
  const std::vector<std::string>& arms = get_arms();
  if (arms.empty()) {
    throw JUBATUS_EXCEPTION(
        common::exception::runtime_error("arm is not registered"));
  }

  const storage& s = get_storage();
  std::vector<double> weights;
  weights.reserve(arms.size());

  for (size_t i = 0; i < arms.size(); ++i) {
    double expectation = s.get_expectation(player_id, arms[i]);
    weights.push_back(std::exp(expectation / tau_));
  }
  return arms[select_by_weights(weights, rand_)];
}

}  // namespace bandit
}  // namespace core
}  // namespace jubatus
