// Copyright (c) 2020-2022 Pilz GmbH & Co. KG
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

#include <array>
#include <functional>
#include <istream>
#include <sstream>
#include <vector>

#include <fmt/format.h>

#include "psen_scan_v2_standalone/data_conversion_layer/diagnostics.h"
#include "psen_scan_v2_standalone/data_conversion_layer/io_pin_data.h"
#include "psen_scan_v2_standalone/data_conversion_layer/monitoring_frame_deserialization.h"
#include "psen_scan_v2_standalone/data_conversion_layer/monitoring_frame_msg.h"
#include "psen_scan_v2_standalone/data_conversion_layer/monitoring_frame_msg_builder.h"
#include "psen_scan_v2_standalone/data_conversion_layer/raw_processing.h"
#include "psen_scan_v2_standalone/data_conversion_layer/raw_scanner_data.h"
#include "psen_scan_v2_standalone/io_state.h"
#include "psen_scan_v2_standalone/util/logging.h"

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <std_msgs/UInt64.h>

namespace psen_scan_v2_standalone
{
namespace data_conversion_layer
{
namespace monitoring_frame
{
using namespace std::placeholders;

// Initialize ROS publisher globally
ros::Publisher diagnostic_pub;

void initializeRosPublisher(ros::NodeHandle& nh)
{
  diagnostic_pub = nh.advertise<std_msgs::UInt64>("diagnostics_bits", 1);
}

// Function to initialize ROS Node
void initializeRosNode(ros::NodeHandle& nh)
{
  // Initialize publisher
  initializeRosPublisher(nh);
}

//////////////////////////////////////

AdditionalFieldHeader::AdditionalFieldHeader(Id id, Length length) : id_(id), length_(length)
{
}

FixedFields::FixedFields(DeviceStatus device_status,
                         OpCode op_code,
                         WorkingMode working_mode,
                         TransactionType transaction_type,
                         configuration::ScannerId scanner_id,
                         FromTheta from_theta,
                         Resolution resolution)
  : device_status_(device_status)
  , op_code_(op_code)
  , working_mode_(working_mode)
  , transaction_type_(transaction_type)
  , scanner_id_(scanner_id)
  , from_theta_(from_theta)
  , resolution_(resolution)
{
}

static constexpr double toMeter(const uint16_t& value)
{
  if ((value == NO_SIGNAL_ARRIVED) || (value == SIGNAL_TOO_LATE))
  {
    return std::numeric_limits<double>::infinity();
  }
  return static_cast<double>(value) / 1000.;
}

static constexpr double toIntensities(const uint16_t& value)
{
  // Neglegt the first two bytes.
  uint16_t retval{ value };
  return static_cast<double>(retval & 0b0011111111111111);
}

monitoring_frame::Message deserialize(const data_conversion_layer::RawData& data, const std::size_t& num_bytes)
{
  data_conversion_layer::monitoring_frame::MessageBuilder msg_builder;

  std::stringstream ss;
  ss.write(data.data(), num_bytes);

  FixedFields frame_header = readFixedFields(ss);

  msg_builder.scannerId(frame_header.scannerId());
  msg_builder.fromTheta(frame_header.fromTheta());
  msg_builder.resolution(frame_header.resolution());

  bool end_of_frame{ false };
  while (!end_of_frame)
  {
    const AdditionalFieldHeader additional_header{ readAdditionalField(ss, num_bytes) };
    switch (static_cast<AdditionalFieldHeaderID>(additional_header.id()))
    {
      case AdditionalFieldHeaderID::scan_counter:
        if (additional_header.length() != NUMBER_OF_BYTES_SCAN_COUNTER)
        {
          throw AdditionalFieldUnexpectedSize(fmt::format("Length of scan counter field is {}, but should be {}.",
                                                          additional_header.length(),
                                                          NUMBER_OF_BYTES_SCAN_COUNTER));
        }
        uint32_t scan_counter_read_buffer;
        raw_processing::read<uint32_t>(ss, scan_counter_read_buffer);
        msg_builder.scanCounter(scan_counter_read_buffer);
        break;

      case AdditionalFieldHeaderID::measurements: {
        const size_t num_measurements{ static_cast<size_t>(additional_header.length()) /
                                       NUMBER_OF_BYTES_SINGLE_MEASUREMENT };
        std::vector<double> measurements;
        raw_processing::readArray<uint16_t, double>(ss, measurements, num_measurements, toMeter);
        msg_builder.measurements(measurements);
        break;
      }
      case AdditionalFieldHeaderID::end_of_frame:
        end_of_frame = true;
        break;

      case AdditionalFieldHeaderID::zone_set:
        if (additional_header.length() != NUMBER_OF_BYTES_ZONE_SET)
        {
          throw AdditionalFieldUnexpectedSize(fmt::format("Length of zone set field is {}, but should be {}.",
                                                          additional_header.length(),
                                                          NUMBER_OF_BYTES_ZONE_SET));
        }
        uint8_t zone_set_read_buffer;
        raw_processing::read<uint8_t>(ss, zone_set_read_buffer);
        msg_builder.activeZoneset(zone_set_read_buffer);
        break;

      case AdditionalFieldHeaderID::io_pin_data:
        if (additional_header.length() != io::RAW_CHUNK_LENGTH_IN_BYTES)
        {
          throw AdditionalFieldUnexpectedSize(fmt::format("Length of io state field is {}, but should be {}.",
                                                          additional_header.length(),
                                                          io::RAW_CHUNK_LENGTH_IN_BYTES));
        }
        msg_builder.iOPinData(io::deserializePins(ss));
        break;

      case AdditionalFieldHeaderID::diagnostics:
        msg_builder.diagnosticMessages(diagnostic::deserializeMessages(ss));
        break;

      case AdditionalFieldHeaderID::intensities: {
        const size_t num_measurements{ static_cast<size_t>(additional_header.length()) /
                                       NUMBER_OF_BYTES_SINGLE_MEASUREMENT };
        std::vector<double> intensities;
        raw_processing::readArray<uint16_t, double>(ss, intensities, num_measurements, std::bind(toIntensities, std::placeholders::_1));
        msg_builder.intensities(intensities);
        break;
      }
      default:
        throw DecodingFailure(
            fmt::format("Header Id {:#04x} unknown. Cannot read additional field of monitoring frame on position {}.",
                        additional_header.id(),
                        ss.tellp()));
    }
  }
  return msg_builder.build();
}

AdditionalFieldHeader readAdditionalField(std::istream& is, const std::size_t& max_num_bytes)
{
  auto const id = raw_processing::read<AdditionalFieldHeader::Id>(is);
  auto length = raw_processing::read<AdditionalFieldHeader::Length>(is);

  if (length >= max_num_bytes)
  {
    throw DecodingFailure(
        fmt::format("Length given in header of additional field is too large: {}, id: {:#04x}", length, id));
  }
  if (length > 0)
  {
    length--;
  }
  return AdditionalFieldHeader(id, length);
}

namespace io
{
PinData deserializePins(std::istream& is)
{
  PinData io_pin_data;

  raw_processing::read<
      std::array<uint8_t, 3 * (RAW_CHUNK_LENGTH_RESERVED_IN_BYTES + RAW_CHUNK_PHYSICAL_INPUT_SIGNALS_IN_BYTES)>>(is);

  raw_processing::read<std::array<uint8_t, RAW_CHUNK_LENGTH_RESERVED_IN_BYTES>>(is);
  deserializePinField(is, io_pin_data.input_state);

  raw_processing::read<std::array<uint8_t, RAW_CHUNK_LENGTH_RESERVED_IN_BYTES>>(is);
  deserializePinField(is, io_pin_data.output_state);

  return io_pin_data;
}
}  // namespace io

namespace diagnostic
{
std::vector<diagnostic::Message> deserializeMessages(std::istream& is)
{
    std::vector<diagnostic::Message> diagnostic_messages;

    // Read-in unused data fields
    raw_processing::read<std::array<uint8_t, diagnostic::RAW_CHUNK_UNUSED_OFFSET_IN_BYTES>>(is);

    // Initialize ROS NodeHandle
    ros::NodeHandle nh;
    initializeRosNode(nh);

    // Variable to accumulate bits in a 64-bit number
    uint64_t bit_accumulator = 0;
    size_t bit_position = 0;

    for (const auto& scanner_id : configuration::VALID_SCANNER_IDS)
    {
      for (size_t byte_n = 0; byte_n < diagnostic::RAW_CHUNK_LENGTH_FOR_ONE_DEVICE_IN_BYTES; byte_n++)
      {
        const auto raw_byte = raw_processing::read<uint8_t>(is);
        const std::bitset<8> raw_bits(raw_byte);

        for (size_t bit_n = 0; bit_n < raw_bits.size(); ++bit_n)
        {
          if (raw_bits.test(bit_n) && (diagnostic::ErrorType::unused != diagnostic::ERROR_BITS[byte_n][bit_n]))
          {
            diagnostic_messages.push_back(diagnostic::Message(static_cast<configuration::ScannerId>(scanner_id),
                                                              diagnostic::ErrorLocation(byte_n, bit_n)));
                                                              
            // Store the bit in the 64-bit accumulator
            bit_accumulator |= (1ULL << bit_position);
          }
          bit_position++;
          if (bit_position >= 64)  // Only store up to 64 bits
          {
              bit_position = 0;  // Reset bit position for the next byte
          }
        }
      }
    }

    std::bitset<64> bitset_bit_accu(bit_accumulator);
    std::string binary_string = bitset_bit_accu.to_string();

    // deserializeMessages is observed to be publishing at every 5ms 
    // lidar raw data is published at every 33ms unaware about 5ms frequency 
    // hence changing frequency could lead to a different frequency of data publishing.
    static int call_count = 0;  
    call_count++;
    if (call_count >= 20)
    {
        std_msgs::UInt64 diagnostic_msg;
        diagnostic_msg.data = bit_accumulator;
        diagnostic_pub.publish(diagnostic_msg);
        call_count = 0;
    }

    return diagnostic_messages;
}
}  // namespace diagnostic

FixedFields readFixedFields(std::istream& is)
{
  const auto device_status = raw_processing::read<FixedFields::DeviceStatus>(is);
  const auto op_code = raw_processing::read<FixedFields::OpCode>(is);
  const auto working_mode = raw_processing::read<FixedFields::WorkingMode>(is);
  const auto transaction_type = raw_processing::read<FixedFields::TransactionType>(is);
  const auto scanner_id = raw_processing::read<configuration::ScannerId>(is);

  const auto from_theta = raw_processing::read<int16_t, FixedFields::FromTheta>(is);
  const auto resolution = raw_processing::read<int16_t, FixedFields::Resolution>(is);

  // LCOV_EXCL_START
  if (OP_CODE_MONITORING_FRAME != op_code)
  {
    PSENSCAN_ERROR_THROTTLE(
        0.1, "monitoring_frame::Message", "Unexpected opcode during deserialization of MonitoringFrame.");
  }

  if (ONLINE_WORKING_MODE != working_mode)
  {
    PSENSCAN_ERROR_THROTTLE(0.1, "monitoring_frame::Message", "Invalid working mode (not online)");
  }

  if (GUI_MONITORING_TRANSACTION != transaction_type)
  {
    PSENSCAN_ERROR_THROTTLE(0.1, "monitoring_frame::Message", "Invalid transaction type.");
  }

  if (MAX_SCANNER_ID < static_cast<uint8_t>(scanner_id))
  {
    PSENSCAN_ERROR_THROTTLE(0.1, "monitoring_frame::Message", "Invalid Scanner id.");
  }
  // LCOV_EXCL_STOP

  return FixedFields(device_status, op_code, working_mode, transaction_type, scanner_id, from_theta, resolution);
}
}  // namespace monitoring_frame
}  // namespace data_conversion_layer
}  // namespace psen_scan_v2_standalone