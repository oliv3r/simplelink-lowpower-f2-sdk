// Firmware version, used in mt_version.c
#ifndef CODE_REVISION_NUMBER
#define CODE_REVISION_NUMBER 20240710
#endif

// Required, otherwise firmware crashes after some uptime in some cases.
#define NVOCMP_RECOVER_FROM_COMPACT_FAILURE

// Required in order to use the extended MT API commands.
#define FEATURE_NVEXID 1

// Grants access to the security key data
#define MT_SYS_KEY_MANAGEMENT 1

// Increase by 1 to compensate for lag (default is 7)
#define NWK_INDIRECT_MSG_TIMEOUT 8

// Increase frame retries
#define ZMAC_MAX_FRAME_RETRIES 7
#define NWK_MAX_DATA_RETRIES 4

// Increase MAC buffers, multiplied default value by 10
#undef MAC_CFG_TX_DATA_MAX
#define MAC_CFG_TX_DATA_MAX 50
#undef MAC_CFG_TX_MAX
#define MAC_CFG_TX_MAX 80
#undef MAC_CFG_RX_MAX
#define MAC_CFG_RX_MAX 50

// From https://www.ti.com/lit/an/swra650b/swra650b.pdf
#define LINK_DOWN_TRIGGER 12
#define NWK_ROUTE_AGE_LIMIT 5
#define DEF_NWK_RADIUS 15
#define DEFAULT_ROUTE_REQUEST_RADIUS 8
#define ZDNWKMGR_MIN_TRANSMISSIONS 0
#define ROUTE_DISCOVERY_TIME 13
#define MTO_RREQ_LIMIT_TIME 5000

// Save memory (tables are not used)
#undef NWK_MAX_BINDING_ENTRIES
#define NWK_MAX_BINDING_ENTRIES 1
#undef APS_MAX_GROUPS
#define APS_MAX_GROUPS 1

// Increase NV pages to allow for bigger device tables
#undef NVOCMP_NVPAGES
#define NVOCMP_NVPAGES 3

// Disabling MULTICAST is required in order for proper group support.
// If MULTICAST is not disabled, the group adress is not included in the APS header
#define MULTICAST_ENABLED FALSE

// Increase the max number of boardcasts, the default broadcast delivery time is 3 seconds
// with the value below this will allow for 1 broadcast every 0.15 second
#define MAX_BCAST 30

// Reduce the APS ack wait duration from 6000 ms to 1000 ms (value * 2 = value in ms).
// This will make requests timeout quicker, in pratice the default timeout of 6000ms is too long.
#define APSC_ACK_WAIT_DURATION_POLLED 500

// Enable MTO routing
// MTO Routing will be used in addition to the standard AODV routing to provide additional route discovery opportunities.
// Especially useful for larger networks with multiple hops.
#define CONCENTRATOR_ENABLE TRUE
#define CONCENTRATOR_ROUTE_CACHE TRUE
#define CONCENTRATOR_DISCOVERY_TIME 60
#define MAX_RTG_SRC_ENTRIES 250
#define SRC_RTG_EXPIRY_TIME 10

// The number of simultaneous route discoveries in network
#define MAX_RREQ_ENTRIES 40

// Size of the conflicted address table
#define CONFLICTED_ADDR_TABLE_SIZE 15

// Number of devices which have associated directly through the coordinator
// This does not determine the upper limit in the number of nodes in the network,
// just the upper limit for number of nodes directly connected to a certain routing node
#define NWK_MAX_DEVICE_LIST 50

//  Determines the maximum number of devices in the Neighbor Table.
#define MAX_NEIGHBOR_ENTRIES 25

// Number of devices in the standard Routing Table, which is used for AODV routing.
// Only stores information for 1-hop routes, so this table does not need to be as big as the Source Route table.
#define MAX_RTG_ENTRIES 100
#define ROUTE_EXPIRY_TIME 2

// Determines the maximum number of “secure partners” that the network Trust Center (ZC) can support.
// This value will be the upper limit of Zigbee 3.0 devices which are allowed in the network
#define ZDSECMGR_TC_DEVICE_MAX 200
