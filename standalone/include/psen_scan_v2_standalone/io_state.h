// Copyright (c) 2021 Pilz GmbH & Co. KG
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PSEN_SCAN_V2_STANDALONE_IO_STATE_H
#define PSEN_SCAN_V2_STANDALONE_IO_STATE_H

#include <ostream>
#include <string>
#include <vector>

namespace psen_scan_v2_standalone
{
//! @brief Represents a single I/O pin.
class PinState
{
public:
  /**
   * @param pin_id Unique id of the pin inside an I/O record.
   * @param name Name connected to the pin.
   * @param state Binary state of the pin.
   */
  PinState(uint32_t pin_id, const std::string& name, bool state);
  bool operator==(const PinState& ps) const;
  bool operator!=(const PinState& ps) const;
  uint32_t id() const;
  std::string name() const;
  bool state() const;

private:
  uint32_t id_;
  std::string name_;
  bool state_;
};

std::ostream& operator<<(std::ostream& os, const PinState& pin_state);

//! @brief Represents the set of all I/Os of the scanner and their states.
class IOState
{
public:
  IOState() = default;
  IOState(std::vector<PinState> input, std::vector<PinState> output);
  const std::vector<PinState>& input() const;
  const std::vector<PinState>& output() const;

private:
  std::vector<PinState> input_{};
  std::vector<PinState> output_{};
};

std::ostream& operator<<(std::ostream& os, const IOState& io_state);
}  // namespace psen_scan_v2_standalone

#endif  // PSEN_SCAN_V2_STANDALONE_IO_STATE_H
