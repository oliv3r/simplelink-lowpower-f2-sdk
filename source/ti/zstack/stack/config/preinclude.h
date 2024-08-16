#define MT_SYS_KEY_MANAGEMENT 1
#define FEATURE_NVEXID 1

// Increase by 1 to compensate for lag (default is 7)
#define NWK_INDIRECT_MSG_TIMEOUT 8

// Increase frame retries
#define ZMAC_MAX_FRAME_RETRIES 7
#define NWK_MAX_DATA_RETRIES 4

// Increase MAC buffers
#undef MAC_CFG_TX_DATA_MAX
#define MAC_CFG_TX_DATA_MAX 64
#undef MAC_CFG_TX_MAX
#define MAC_CFG_TX_MAX MAC_CFG_TX_DATA_MAX * 1.5
#undef MAC_CFG_RX_MAX
#define MAC_CFG_RX_MAX MAC_CFG_TX_DATA_MAX * 2

// Save memory
#undef NWK_MAX_BINDING_ENTRIES
#define NWK_MAX_BINDING_ENTRIES 1
#undef APS_MAX_GROUPS
#define APS_MAX_GROUPS 1

/**
 * Increase NV pages to 3 to allow for bigger device tables
 * Estimate size based on https://software-dl.ti.com/simplelink/esd/simplelink_cc13x2_26x2_sdk/4.40.00.44/exports/docs/zigbee/html/zigbee/z-stack-overview.html?highlight=nvocmp_nvpages#undefined
 * C2652RB, CC26X2R1 and CC1352_P2
 *  NWK_MAX_DEVICE_LIST = 50 * 23 bytes = 1150
 *  ZDSECMGR_TC_DEVICE_MAX = 200 * 27 bytes = 5400
 *  NWK_MAX_ADDRESSES = NWK_MAX_DEVICES + NWK_MAX_REFLECTOR_ENTRIES + NWK_MAX_SECURE_PARTNERS = (51 + 1 + 205) * 19 bytes = 4883
 *      NWK_MAX_DEVICES = NWK_MAX_DEVICE_LIST + 1 = 50 + 1 = 51
 *      NWK_MAX_REFLECTOR_ENTRIES = NWK_MAX_BINDING_ENTRIES = 1
 *      NWK_MAX_SECURE_PARTNERS = 5 + ZDSECMGR_TC_DEVICE_MAX = 5 + 200 = 205
 *  OTHERS = 2300 bytes
 *  TOTAL = 1150 + 6750 + 4883 + 2300 = 15083 = 0x3AEB
 *  0x3AEB=15083 requires 2 storage pages (0x4000=16384) and 1 compaction page = 3 pages
 *
 * CC1352P7
 *  NWK_MAX_DEVICE_LIST = 50 * 23 bytes = 1150
 *  ZDSECMGR_TC_DEVICE_MAX = 300 * 27 bytes = 8100
 *  NWK_MAX_ADDRESSES = NWK_MAX_DEVICES + NWK_MAX_REFLECTOR_ENTRIES + NWK_MAX_SECURE_PARTNERS = (51 + 1 + 305) * 19 bytes = 6783
 *      NWK_MAX_DEVICES = NWK_MAX_DEVICE_LIST + 1 = 50 + 1 = 51
 *      NWK_MAX_REFLECTOR_ENTRIES = NWK_MAX_BINDING_ENTRIES = 1
 *      NWK_MAX_SECURE_PARTNERS = 5 + ZDSECMGR_TC_DEVICE_MAX = 5 + 300 = 305
 *  OTHERS = 2300 bytes
 *  TOTAL = 1150 + 8100 + 6783 + 2300 = 18333 = 0x479D
 *  0x479D=18333 requires 3 storage pages (0x6000=24576) and 1 compaction page = 4 pages
 *
 * For CC2652RB, CC26X2R1 and CC1352_P2 max seems to be 3
 * For CC1352P7 we use 4
 */
#undef NVOCMP_NVPAGES
#ifdef DeviceFamily_CC13X2X7
    #define NVOCMP_NVPAGES 4
#else
    #define NVOCMP_NVPAGES 3
#endif

// Heap config
// https://software-dl.ti.com/simplelink/esd/simplelink_cc13x2_26x2_sdk/4.40.00.44/exports/docs/zigbee/html/memory/memory_management.html#configuring-the-heap
// https://e2e.ti.com/support/wireless-connectivity/zigbee-thread-group/zigbee-and-thread/f/zigbee-thread-forum/1052120/cc2652r-mac-no-resources-0x1a-errors-complete-crash-with-simplelink-cc13xx-cc26xx-sdk_5-30-00-56/3897554#3897554
// Previously a static heap was used: #define HEAPMGR_CONFIG 0x0 with #define HEAPMGR_SIZE 0x7B0C (31500)
// However now we use autoheap size (HEAPMGR_CONFIG 0x80) which is the default
// From the default/*.map file we can find out the heap size after compilation:
// CC2652RB, CC26X2R1 and CC1352_P2: heapEnd (20012000) - heapStart (+-20008b1d) = 0x955B = 38235
// CC1352P7: heapEnd (20022000) - heapStart (+-20008b09) = 0x194F7 = 103671

// Disabling MULTICAST is required in order for proper group support.
// If MULTICAST is not disabled, the group address is not included in the APS header
#define MULTICAST_ENABLED FALSE

// Increase the max number of broadcasts, the default broadcast delivery time is 3 seconds
// with the value below this will allow for 1 broadcast every 0.15 second
#define MAX_BCAST 30

/**
 * Reduce the APS ack wait duration from 6000 ms to 1000 ms (value * 2 = value in ms).
 * This will make requests timeout quicker, in practice the default timeout of 6000ms is too long.
 */
#define APSC_ACK_WAIT_DURATION_POLLED 500

// From https://www.ti.com/lit/an/swra650b/swra650b.pdf
#define LINK_DOWN_TRIGGER 12
#define NWK_ROUTE_AGE_LIMIT 5
#define DEF_NWK_RADIUS 15
#define DEFAULT_ROUTE_REQUEST_RADIUS 8
#define ZDNWKMGR_MIN_TRANSMISSIONS 0
#define ROUTE_DISCOVERY_TIME 13
#define MTO_RREQ_LIMIT_TIME 5000
#define CONCENTRATOR_ENABLE TRUE
#define CONCENTRATOR_ROUTE_CACHE TRUE

/**
 * Default is 60 but should be larger for bigger networks:
 *   "As the network grows in size and/or number of concentrators, it is important to adjust the Maximum Time Between Broadcasts
 *   to be longer so that the network is not overwhelmed by several concentrators doing periodic MTORRs in a short window of time"
 * https://community.silabs.com/s/article/guidelines-for-large-dense-networks-with-emberznet-pro?language=en_US
 */
#define CONCENTRATOR_DISCOVERY_TIME 120


/**
 * Number of devices which have associated directly through the coordinator, i.e. determines the size of the Association Table.
 * This includes ZEDs which have associated through the ZC directly, and it can also potentially include ZRs which
 * have associated through the ZC directly. This does not determine the upper limit in the number of nodes in the network,
 * just the upper limit for number of nodes directly connected to a certain routing node.
 * Should not be too big as it can put a lot of stress on the coordinator
 */
#define NWK_MAX_DEVICE_LIST 50

/**
 * Determines the maximum number of devices in the Neighbor Table for a particular routing device.
 * The Neighbor Table contains other routing devices that are within direct radio range, but are not in your
 * Association Table. It is populated when a routing device receives a Link Status message from another routing
 * device within the network, and this table contains information about Link Cost for each hop, etc.
 */
#define MAX_NEIGHBOR_ENTRIES 50


// Determines the max amount of Zigbee 3.0 devices in the network (not i.e. Zigbee 1.2 devices!)
#ifdef DeviceFamily_CC13X2X7
    #define ZDSECMGR_TC_DEVICE_MAX 300
#else
    #define ZDSECMGR_TC_DEVICE_MAX 200
#endif

/**
 * Determines the size of the Source Routing table. Each concentrator source route table entry stores a linked list of hops for the
 * entirety of a route, which is stored on the heap.
 * SRC_RTG_EXPIRY_TIME is the timeout
 */
#define MAX_RTG_SRC_ENTRIES 500
#define SRC_RTG_EXPIRY_TIME 254

/**
 * Number of devices in the standard Routing Table, which is used for AODV routing.
 * Only stores information for 1-hop routes, so this table does not need to be as big as the Source Route table.
 * ROUTE_EXPIRY_TIME is the timeout
 */
#define MAX_RTG_ENTRIES 250
#define ROUTE_EXPIRY_TIME 254
