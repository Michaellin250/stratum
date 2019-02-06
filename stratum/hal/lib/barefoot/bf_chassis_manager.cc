/* Copyright 2018-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "stratum/hal/lib/barefoot/bf_chassis_manager.h"

extern "C" {

#include "tofino/bf_pal/bf_pal_port_intf.h"

}

#include <map>
#include <memory>

#include "stratum/lib/constants.h"
#include "stratum/lib/macros.h"
#include "stratum/lib/utils.h"
#include "stratum/hal/lib/common/gnmi_events.h"
#include "stratum/hal/lib/common/phal_interface.h"
#include "stratum/hal/lib/common/writer_interface.h"
#include "stratum/hal/lib/common/utils.h"
#include "stratum/glue/integral_types.h"
#include "absl/base/thread_annotations.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/mutex.h"

namespace stratum {
namespace hal {
namespace barefoot {

absl::Mutex chassis_lock;

BFChassisManager::BFChassisManager(PhalInterface* phal_interface)
    : phal_interface_(phal_interface),
      unit_to_node_id_(),
      node_id_to_unit_(),
      node_id_to_port_id_to_port_state_() {}

BFChassisManager::~BFChassisManager() = default;

::util::Status BFChassisManager::PushChassisConfig(
     const ChassisConfig& config) {
  int unit(0);
  for (auto& node : config.nodes()) {
    unit_to_node_id_[unit] = node.id();
    node_id_to_unit_[node.id()] = unit;
    node_id_to_port_id_to_port_state_[node.id()] = {};
    unit++;
  }
  // TODO(antonin): handle singleton ports
  RETURN_IF_ERROR(RegisterEventWriters());
  initialized_ = true;
  return ::util::OkStatus();
}

::util::Status BFChassisManager::VerifyChassisConfig(
     const ChassisConfig& config) {
  return ::util::OkStatus();
}

::util::Status BFChassisManager::RegisterEventNotifyWriter(
    const std::shared_ptr<WriterInterface<GnmiEventPtr>>& writer) {
  absl::WriterMutexLock l(&gnmi_event_lock_);
  gnmi_event_writer_ = writer;
  return ::util::OkStatus();
}

::util::Status BFChassisManager::UnregisterEventNotifyWriter() {
  absl::WriterMutexLock l(&gnmi_event_lock_);
  gnmi_event_writer_ = nullptr;
  return ::util::OkStatus();
}

::util::StatusOr<PortState> BFChassisManager::GetPortState(
    uint64 node_id, uint32 port_id) {
  if (!initialized_) {
    return MAKE_ERROR(ERR_NOT_INITIALIZED) << "Not initialized!";
  }
  const int* unit = gtl::FindOrNull(node_id_to_unit_, node_id);
  if (unit == nullptr) {
    return MAKE_ERROR(ERR_INTERNAL) << "Unkonwn node id " << node_id;
  }

  auto* port_id_to_port_state =
      gtl::FindOrNull(node_id_to_port_id_to_port_state_, node_id);
  CHECK_RETURN_IF_FALSE(port_id_to_port_state != nullptr)
      << "Node " << node_id << " is not configured or not known.";
  const PortState* port_state_ptr =
      gtl::FindOrNull(*port_id_to_port_state, port_id);
  // TODO(antonin): Once we implement PushChassisConfig, port_state_ptr should
  // never be NULL
  if (port_state_ptr != nullptr && *port_state_ptr != PORT_STATE_UNKNOWN) {
    return *port_state_ptr;
  }

  // If state is unknown, query the state
  LOG(INFO) << "Querying state of port " << port_id << " in node " << node_id
            << " with BF_PAL";
  int state;
  auto bf_status = bf_pal_port_oper_state_get(
      static_cast<bf_dev_id_t>(*unit), static_cast<bf_dev_port_t>(port_id), &state);
  if (bf_status != BF_SUCCESS) {
    return MAKE_ERROR(ERR_INTERNAL) << "Error when querying port oper status";
  }
  PortState port_state = state ? PORT_STATE_UP : PORT_STATE_DOWN;
  LOG(INFO) << "State of port " << port_id << " in node " << node_id << ": "
            << PrintPortState(port_state);
  return port_state;
}

::util::Status BFChassisManager::GetPortCounters(
    uint64 node_id, uint32 port_id, PortCounters* counters) {
  if (!initialized_) {
    return MAKE_ERROR(ERR_NOT_INITIALIZED) << "Not initialized!";
  }
  const int* unit = gtl::FindOrNull(node_id_to_unit_, node_id);
  if (unit == nullptr) {
    return MAKE_ERROR(ERR_INTERNAL) << "Unkonwn node id " << node_id;
  }
  uint64_t stats[BF_NUM_RMON_COUNTERS];
  auto bf_status = bf_pal_port_all_stats_get(
      static_cast<bf_dev_id_t>(*unit), static_cast<bf_dev_port_t>(port_id), stats);
  if (bf_status != BF_SUCCESS) {
    return MAKE_ERROR(ERR_INTERNAL) << "Error when querying counters for port "
                                    << port_id << " in node " << node_id;
  }
  counters->set_in_octets(stats[bf_mac_stat_OctetsReceived]);
  counters->set_out_octets(stats[bf_mac_stat_OctetsTransmittedTotal]);
  counters->set_in_unicast_pkts(stats[bf_mac_stat_FramesReceivedwithUnicastAddresses]);
  counters->set_out_unicast_pkts(stats[bf_mac_stat_FramesTransmittedUnicast]);
  counters->set_in_broadcast_pkts(stats[bf_mac_stat_FramesReceivedwithBroadcastAddresses]);
  counters->set_out_broadcast_pkts(stats[bf_mac_stat_FramesTransmittedBroadcast]);
  counters->set_in_multicast_pkts(stats[bf_mac_stat_FramesReceivedwithMulticastAddresses]);
  counters->set_out_multicast_pkts(stats[bf_mac_stat_FramesTransmittedMulticast]);
  counters->set_in_discards(stats[bf_mac_stat_FramesDroppedBufferFull]);
  counters->set_out_discards(0);  // stat not available
  counters->set_in_unknown_protos(0);  // stat not meaningful
  counters->set_in_errors(stats[bf_mac_stat_FrameswithanyError]);
  counters->set_out_errors(stats[bf_mac_stat_FramesTransmittedwithError]);
  counters->set_in_fcs_errors(stats[bf_mac_stat_FramesReceivedwithFCSError]);
  return ::util::OkStatus();
}

::util::StatusOr<std::map<uint64, int>> BFChassisManager::GetNodeIdToUnitMap()
    const {
  if (!initialized_) {
    return MAKE_ERROR(ERR_NOT_INITIALIZED) << "Not initialized!";
  }
  return node_id_to_unit_;
}

std::unique_ptr<BFChassisManager> BFChassisManager::CreateInstance(
    PhalInterface* phal_interface) {
  return absl::WrapUnique(new BFChassisManager(phal_interface));
}

void BFChassisManager::SendPortOperStateGnmiEvent(
    uint64 node_id, uint64 port_id, PortState new_state) {
  absl::ReaderMutexLock l(&gnmi_event_lock_);
  if (!gnmi_event_writer_) return;
  // Allocate and initialize a PortOperStateChangedEvent event and pass it to
  // the gNMI publisher using the gNMI event notification channel.
  // The GnmiEventPtr is a smart pointer (shared_ptr<>) and it takes care of
  // the memory allocated to this event object once the event is handled by
  // the GnmiPublisher.
  if (!gnmi_event_writer_->Write(GnmiEventPtr(
          new PortOperStateChangedEvent(node_id, port_id, new_state)))) {
    // Remove WriterInterface if it is no longer operational.
    gnmi_event_writer_.reset();
  }
}

::util::Status PortStatusChangeCb(int unit,
                                  uint64 port_id,
                                  bool up,
                                  void *cookie) {
  auto* chassis_manager = static_cast<BFChassisManager*>(cookie);
  const uint64* node_id = gtl::FindOrNull(
      chassis_manager->unit_to_node_id_, unit);
  if (node_id == nullptr) {
    return MAKE_ERROR(ERR_INTERNAL) << "Unkonwn unit / device id " << unit;
  }
  PortState new_state = up ? PORT_STATE_UP : PORT_STATE_DOWN;
  LOG(INFO) << "State of port " << port_id << " in node " << *node_id << ": "
            << PrintPortState(new_state);
  absl::WriterMutexLock l(&chassis_lock);
  chassis_manager->node_id_to_port_id_to_port_state_[*node_id][port_id] = new_state;
  chassis_manager->SendPortOperStateGnmiEvent(
      *node_id, port_id, new_state);
  return ::util::OkStatus();
}

namespace {

bf_status_t PortStatusChangeCbInternal(bf_dev_id_t dev_id,
                                       bf_dev_port_t dev_port,
                                       bool up,
                                       void *cookie) {
  auto status = PortStatusChangeCb(static_cast<int>(dev_id),
                                   static_cast<uint64>(dev_port),
                                   up,
                                   cookie);
  return status.ok() ? BF_SUCCESS : BF_INTERNAL_ERROR;
}

}  // namespace

::util::Status BFChassisManager::RegisterEventWriters() {
  auto bf_status = bf_pal_port_status_notif_reg(
      PortStatusChangeCbInternal, this);
  if (bf_status != BF_SUCCESS) {
    return MAKE_ERROR(ERR_INTERNAL)
        << "Error when registering port status notification callback";
  }
  LOG(INFO) << "Port status notification callback registered successfully";
  return ::util::OkStatus();
}

::util::Status BFChassisManager::UnregisterEventWriters() {
  return ::util::OkStatus();
}

}  // namespace barefoot
}  // namespace hal
}  // namespace stratum