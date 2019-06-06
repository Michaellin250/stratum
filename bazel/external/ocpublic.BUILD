#
# Copyright 2018-present Open Networking Foundation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

licenses(["notice"])  # Apache v2

package(
    default_visibility = [ "//visibility:public" ],
)

filegroup(
    name = "openconfig_yang_models",
    srcs = [
        "release/models/interfaces/openconfig-interfaces.yang",
        "release/models/interfaces/openconfig-if-ip.yang",
        "release/models/lacp/openconfig-lacp.yang",
        "release/models/platform/openconfig-platform.yang",
        "release/models/platform/openconfig-platform-linecard.yang",
        "release/models/platform/openconfig-platform-port.yang",
        "release/models/platform/openconfig-platform-transceiver.yang",
        "release/models/vlan/openconfig-vlan.yang",
        "release/models/system/openconfig-system.yang",
    ]
)

filegroup(
    name = "openconfig_yang_models_hdrs",
    srcs = glob(["release/models/**/*.yang"])
)