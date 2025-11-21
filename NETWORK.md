# Nano Node Network Implementation

## Overview

The Nano node network subsystem is a sophisticated P2P (peer-to-peer) networking layer that handles all communication between nodes in the Nano network. It implements a custom TCP-based protocol for message exchange, peer discovery, block propagation, vote dissemination, and consensus coordination.

### Key Characteristics

- **Pure TCP-based**: Uses TCP exclusively for all peer-to-peer communication
- **Custom Protocol**: Binary message format optimized for efficiency
- **Peer Discovery**: Automatic peer discovery through keepalive message exchange
- **Traffic Shaping**: Prioritized message queuing with bandwidth limiting
- **Security**: Connection filtering, rate limiting, and peer exclusion mechanisms
- **Scalability**: Designed to handle thousands of concurrent connections

### Architecture Layers

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  (Block Processor, Elections, Vote Processor, Bootstrap)    │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────┴────────────────────────────────────┐
│                   Message Processing Layer                   │
│          (Message Processor, Message Deserialization)        │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────┴────────────────────────────────────┐
│                     Network Layer                            │
│      (Network, TCP Channels, Flooding, Peer Management)      │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────┴────────────────────────────────────┐
│                    Transport Layer                           │
│   (TCP Listener, TCP Server, TCP Channel, TCP Socket)       │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────┴────────────────────────────────────┐
│                     Boost.Asio                               │
│              (Async I/O, Sockets, Coroutines)                │
└─────────────────────────────────────────────────────────────┘
```

## Network Architecture

### Core Components

#### 1. network (`nano::network`)

**File**: `nano/node/network.hpp`, `nano/node/network.cpp`

The central network coordinator that manages all high-level network operations.

**Responsibilities**:
- Peer management and discovery
- Message flooding (block propagation, vote dissemination)
- Keepalive message coordination
- Connection lifecycle management
- Peer reachout and connection attempts
- Network-wide operations (cleanup, capacity checks)

**Key Threads**:
- **cleanup_thread**: Removes stale connections and cleans up expired syn cookies
- **keepalive_thread**: Periodically floods keepalive messages to maintain connections
- **reachout_thread**: Attempts connections to peers learned from keepalive messages
- **reachout_cached_thread**: Attempts connections to cached peers from database

**Configuration** (`nano::network_config`):
```cpp
struct network_config {
    std::chrono::milliseconds peer_reachout{ 250ms };          // Delay between reachout attempts
    std::chrono::milliseconds cached_peer_reachout{ 1s };      // Delay for cached peer attempts
    size_t max_peers_per_ip{ 4 };                              // Max connections per IP
    size_t max_peers_per_subnetwork{ 16 };                     // Max connections per /64 subnet
    size_t duplicate_filter_size{ 1024 * 1024 };               // Message deduplication filter size
    uint64_t duplicate_filter_cutoff{ 60 };                    // Age cutoff for filter entries
    size_t minimum_fanout{ 2 };                                // Minimum message broadcast fanout
};
```

---

#### 2. tcp_channels (`nano::transport::tcp_channels`)

**File**: `nano/node/transport/tcp_channels.hpp`, `nano/node/transport/tcp_channels.cpp`

Container and manager for all active TCP connections.

**Data Structures**:

Uses Boost.Multi_index container with multiple indices for efficient lookups:

```cpp
boost::multi_index_container<channel_entry,
    random_access<...>,                    // Sequential access
    ordered_non_unique<last_bootstrap_attempt>,  // Bootstrap ordering
    hashed_unique<endpoint>,               // Lookup by endpoint
    hashed_non_unique<node_id>,            // Lookup by node ID
    ordered_non_unique<version>,           // Filter by protocol version
    hashed_non_unique<ip_address>,         // Lookup by IP address
    hashed_non_unique<subnetwork>          // Lookup by /64 subnet
>
```

**Operations**:
- `create()`: Create new channel from socket
- `find_channel()`: Find channel by endpoint
- `find_node_id()`: Find channel by node ID
- `erase()`: Remove channel
- `random_set()`: Get random sample of channels
- `list()`: Get filtered list of channels
- `keepalive()`: Send keepalive to all channels

**Connection Limits**:
- Enforces max connections per IP address
- Enforces max connections per /64 IPv6 subnet
- Tracks connection attempts separately

---

#### 3. tcp_listener (`nano::transport::tcp_listener`)

**File**: `nano/node/transport/tcp_listener.hpp`, `nano/node/transport/tcp_listener.cpp`

Manages both inbound (listening) and outbound (initiated) TCP connections.

**Connection Types**:
```cpp
enum class connection_type {
    inbound,   // Incoming connection from remote peer
    outbound,  // Outgoing connection initiated by us
};
```

**Accept Flow**:
```
Incoming Connection
        ↓
Check Limits (IP, subnet, inbound count)
        ↓
    Accepted?
    ↙     ↘
  Yes      No → Reject & Close
   ↓
Create Socket + Server
   ↓
Trigger connection_accepted event
   ↓
Start handshake in tcp_server
```

**Connection Limits**:
- `max_inbound`: Maximum inbound connections
- `max_outbound`: Maximum outbound connections
- Per-IP limits (inherited from network_config)
- Per-subnet limits (inherited from network_config)

**Configuration** (`nano::transport::tcp_config`):
```cpp
struct tcp_config {
    size_t max_inbound_connections{ 256 };
    size_t max_outbound_connections{ 16 };
    std::chrono::seconds io_timeout{ 15 };
    std::chrono::seconds connect_timeout{ 5 };
};
```

---

#### 4. tcp_server (`nano::transport::tcp_server`)

**File**: `nano/node/transport/tcp_server.hpp`, `nano/node/transport/tcp_server.cpp`

Handles per-connection message receiving and handshake coordination.

**Lifecycle States**:
```cpp
enum class handshake_status {
    abort,       // Close connection
    handshake,   // Continue handshake
    realtime,    // Transition to realtime messaging
    bootstrap,   // Transition to bootstrap protocol
};
```

**Message Flow**:
```
Socket Connected
       ↓
perform_handshake()
       ↓
   node_id_handshake exchange
       ↓
    Status?
    ↙  ↓  ↘
realtime bootstrap abort
   ↓      ↓       ↓
run_realtime()  →bootstrap  close()
   ↓              protocol
receive_message()
   ↓
Deserialize & Process
   ↓
Message Processor
```

**Responsibilities**:
- Perform node ID handshake
- Receive and deserialize messages
- Route messages to message processor
- Detect realtime vs. bootstrap protocol
- Buffer management for receiving

---

#### 5. tcp_channel (`nano::transport::tcp_channel`)

**File**: `nano/node/transport/tcp_channel.hpp`, `nano/node/transport/tcp_channel.cpp`

Represents a bidirectional communication channel with a remote peer. Handles message sending with traffic prioritization.

**Channel Queue**:

Messages are queued per traffic type with prioritization:

```cpp
class tcp_channel_queue {
    constexpr static size_t max_size = 32;      // Soft limit
    constexpr static size_t full_size = 128;    // Hard limit (4 * max_size)

    enum_array<traffic_type, queue_t> queues;   // One queue per traffic type
};
```

**Sending Flow**:
```
send() called
    ↓
Check bandwidth limiter
    ↓
Queue message with traffic type
    ↓
Async send coroutine picks messages
    ↓
Round-robin across traffic types
    ↓
Write to TCP socket
    ↓
Callback on completion
```

**Traffic Prioritization**:
Higher priority traffic types get more frequent sends:
- Vote messages: High priority
- Block messages: Medium-high priority
- Keepalive: Medium priority
- Bootstrap: Low priority

---

#### 6. tcp_socket (`nano::transport::tcp_socket`)

**File**: `nano/node/transport/tcp_socket.hpp`, `nano/node/transport/tcp_socket.cpp`

Thin wrapper around Boost.Asio TCP socket with timeout support.

**Features**:
- Async read/write operations
- Configurable timeouts
- Connection state tracking
- Endpoint information
- Close notifications

---

## Message Protocol

### Message Structure

All network messages follow a common structure:

```
┌─────────────────────────────────────────────┐
│           Message Header (8 bytes)          │
├─────────────────────────────────────────────┤
│                                             │
│          Message Payload (variable)         │
│                                             │
└─────────────────────────────────────────────┘
```

### Message Header Format

**File**: `nano/node/messages.hpp`

```cpp
class message_header {
    nano::network_type network;      // 2 bytes: Network ID (live/beta/dev)
    uint8_t version_max;              // 1 byte:  Maximum protocol version
    uint8_t version_using;            // 1 byte:  Current protocol version
    uint8_t version_min;              // 1 byte:  Minimum protocol version
    nano::message_type type;          // 1 byte:  Message type
    std::bitset<16> extensions;       // 2 bytes: Message-specific flags

    static constexpr size_t size = 8; // Total header size
};
```

**Field Descriptions**:

- **network**: Identifies which Nano network (live/beta/dev) - prevents cross-network communication
- **version_max**: Highest protocol version this node supports
- **version_using**: Protocol version used for this message
- **version_min**: Lowest protocol version this node supports
- **type**: One of the 15 message types (see below)
- **extensions**: Flexible bitset for message-specific flags and parameters

### Message Types

```cpp
enum class message_type : uint8_t {
    invalid            = 0x0,   // (Invalid/unused)
    not_a_type         = 0x1,   // (Placeholder)
    keepalive          = 0x2,   // Peer discovery and connection maintenance
    publish            = 0x3,   // Block broadcast
    confirm_req        = 0x4,   // Confirmation request (vote solicitation)
    confirm_ack        = 0x5,   // Confirmation acknowledgment (vote)
    bulk_pull          = 0x6,   // Bootstrap: request blocks by account
    bulk_push          = 0x7,   // Bootstrap: send blocks
    frontier_req       = 0x8,   // Bootstrap: request account frontiers
    node_id_handshake  = 0x0a,  // Node ID exchange and verification
    bulk_pull_account  = 0x0b,  // Bootstrap: pull by account with filters
    telemetry_req      = 0x0c,  // Request telemetry data
    telemetry_ack      = 0x0d,  // Telemetry data response
    asc_pull_req       = 0x0e,  // Ascending bootstrap pull request
    asc_pull_ack       = 0x0f,  // Ascending bootstrap pull response
};
```

### Message Descriptions

#### keepalive (0x02)

**Purpose**: Peer discovery and connection maintenance

**Payload**:
```cpp
struct keepalive {
    std::array<nano::endpoint, 8> peers;  // 8 peer endpoints (IP + port)
};
```

**Size**: Header (8 bytes) + 8 × (16 + 2) = 152 bytes

**Flow**:
1. Node periodically sends keepalive to all connected peers
2. Payload contains mix of: self endpoint, known peer endpoints
3. Recipients learn about new peers and attempt connections
4. Updates last_packet_received timestamp for connection liveness

**Types**:
- **Regular keepalive**: Contains random sample of known peers
- **Self keepalive**: Contains only self endpoint (25% of keepalives)

---

#### publish (0x03)

**Purpose**: Broadcast blocks to the network

**Payload**:
```cpp
struct publish {
    std::shared_ptr<nano::block> block;  // The block being published
    nano::network_filter::digest_t digest; // Hash for deduplication
};
```

**Extensions**:
- **[0x0f00]**: Block type (state/send/receive/open/change)
- **[0x0004]**: Originator flag (first broadcast vs. rebroadcast)

**Size**: Variable depending on block type (~200-250 bytes typical)

**Flow**:
1. Block created or received
2. Check duplicate filter (digest)
3. Broadcast to peers based on traffic type:
   - `block_broadcast_initial`: To PRs + random non-PRs
   - `block_broadcast`: To random selection
   - `block_broadcast_rpc`: Higher priority for RPC-submitted blocks

---

#### confirm_req (0x04)

**Purpose**: Request votes for block confirmation

**Payload**:
```cpp
struct confirm_req {
    std::vector<std::pair<block_hash, root>> roots_hashes;  // Block hash + root pairs
};
```

**Extensions**:
- **[0xf000]**: Count (V1) - number of hash pairs (1-15)
- **[0xf000 + 0x00f0]**: Count V2 - supports larger counts (up to 255)
- **[0x0001]**: V2 flag

**Size**: Header (8 bytes) + N × 64 bytes (N = number of blocks, max 255)

**Flow**:
1. Election started for block
2. Send confirm_req to representatives
3. Representatives respond with confirm_ack (votes)
4. Collect votes to reach quorum

---

#### confirm_ack (0x05)

**Purpose**: Vote for block confirmation

**Payload**:
```cpp
struct confirm_ack {
    std::shared_ptr<nano::vote> vote;  // Vote containing account, signature, hashes
    nano::network_filter::digest_t digest; // Hash for deduplication
};
```

**Vote Structure**:
```cpp
struct vote {
    nano::account account;                    // Representative account
    nano::signature signature;                // Ed25519 signature
    uint64_t timestamp;                       // Monotonic timestamp
    uint8_t duration;                         // Vote duration bits
    std::vector<nano::block_hash> hashes;     // Blocks being voted for
};
```

**Extensions**:
- **[0xf000 + 0x00f0]**: Count V2 - number of hashes in vote
- **[0x0001]**: V2 flag
- **[0x0004]**: Rebroadcasted flag

**Size**: Variable (~150-250 bytes depending on hash count)

**Flow**:
1. Representative receives confirm_req
2. Validates blocks
3. Signs vote with private key
4. Broadcasts confirm_ack to network
5. Elections collect votes
6. Quorum reached → block confirmed

---

#### node_id_handshake (0x0a)

**Purpose**: Establish peer identity and verify node ownership

**Payload** (Query):
```cpp
struct query_payload {
    nano::uint256_union cookie;  // Random challenge
};
```

**Payload** (Response):
```cpp
struct response_payload {
    nano::account node_id;       // Node's public key
    nano::signature signature;   // Signature of cookie
    optional<v2_payload> v2;     // V2: salt + genesis hash
};
```

**Extensions**:
- **[0x0001]**: Query flag
- **[0x0002]**: Response flag
- **[0x0004]**: V2 flag

**Handshake Flow**:
```
Node A                         Node B
   │                              │
   ├──────── Query (cookie) ─────→│
   │                              │
   │                      Generate signature
   │                       of cookie + node_id
   │                              │
   │←───── Response (signed) ─────┤
   │                              │
Verify signature               Verify signature
   │                              │
   ├──────── Query (cookie) ─────→│
   │                              │
   │←───── Response (signed) ─────┤
   │                              │
Both verified → Realtime communication begins
```

**Security**:
- Prevents impersonation (must have private key)
- Random cookies prevent replay attacks
- Syn cookie mechanism rate-limits handshake requests per IP
- V2 adds genesis hash validation to prevent cross-network connections

---

#### telemetry_req (0x0c) / telemetry_ack (0x0d)

**Purpose**: Exchange node telemetry information

**Payload** (ack):
```cpp
struct telemetry_data {
    nano::signature signature;         // Self-signed data
    nano::account node_id;              // Node identifier
    uint64_t block_count;               // Total blocks in ledger
    uint64_t cemented_count;            // Confirmed blocks
    uint64_t unchecked_count;           // Unprocessed blocks
    uint64_t account_count;             // Total accounts
    uint64_t bandwidth_cap;             // Bandwidth limit
    uint64_t uptime;                    // Node uptime (seconds)
    uint32_t peer_count;                // Connected peers
    uint8_t protocol_version;           // Protocol version
    nano::block_hash genesis_block;     // Genesis block hash
    uint8_t major_version;              // Software major version
    uint8_t minor_version;              // Software minor version
    uint8_t patch_version;              // Software patch version
    uint8_t pre_release_version;        // Pre-release version
    uint8_t maker;                      // Node implementation
    uint64_t timestamp;                 // Telemetry generation time
    uint64_t active_difficulty;         // Current PoW difficulty
};
```

**Uses**:
- Network health monitoring
- Peer capability discovery
- Software version tracking
- Representative weight verification

---

#### asc_pull_req (0x0e) / asc_pull_ack (0x0f)

**Purpose**: Ascending bootstrap protocol for efficient synchronization

**Pull Request Types**:
```cpp
enum class asc_pull_type : uint8_t {
    blocks        = 0x1,  // Request blocks
    account_info  = 0x2,  // Request account information
    frontiers     = 0x3,  // Request account frontiers
};
```

**Blocks Request**:
```cpp
struct blocks_payload {
    nano::hash_or_account start;  // Starting point
    uint8_t count;                 // Number of blocks requested
    hash_type start_type;          // account or block hash
};
```

**Flow**:
1. Requester sends asc_pull_req with ID
2. Responder looks up requested data
3. Responder sends asc_pull_ack with matching ID
4. Requester matches response to request via ID
5. Process received blocks/account info/frontiers

**Advantages** over legacy bootstrap:
- Request/response pairing via ID
- More granular control
- Better for targeted sync
- Supports account-based queries

---

#### bulk_pull (0x06) / bulk_push (0x07) / bulk_pull_account (0x0b) / frontier_req (0x08)

**Purpose**: Legacy bootstrap protocol messages

**Status**: Still supported for backwards compatibility, but ascending bootstrap (asc_pull) is preferred.

---

### Message Serialization

All messages use binary serialization via `nano::stream`:

```cpp
// Serialization
nano::stream stream;
header.serialize (stream);
payload.serialize (stream);

// Deserialization
nano::stream stream (buffer);
header.deserialize (stream);
payload.deserialize (stream);
```

**Binary Format**:
- Big-endian byte order (network byte order)
- Fixed-size types: Direct binary representation
- Variable-size types: Length-prefixed where necessary
- Hashes/public keys: Raw 32-byte values
- Signatures: Raw 64-byte values

---

## Connection Lifecycle

### Connection Establishment

#### Outbound Connection Flow

```
1. Peer Discovery
   ↓ (via keepalive messages or cached peers)
2. tcp_listener::connect()
   ↓
3. Create TCP socket
   ↓
4. Async connect with timeout
   ↓
   Success?
   ↙    ↘
 Yes     No → Log & cleanup
  ↓
5. Create tcp_socket + tcp_server
  ↓
6. Start tcp_server
  ↓
7. Perform handshake
  ↓
  Success?
  ↙    ↘
Yes     No → Close connection
 ↓
8. Create tcp_channel
 ↓
9. Register in tcp_channels
 ↓
10. Begin realtime messaging
```

#### Inbound Connection Flow

```
1. TCP Listener accepts connection
   ↓
2. Check connection limits
   ↓
   Within limits?
   ↙           ↘
 Yes            No → Reject & close
  ↓
3. Check IP/subnet limits
  ↓
  Within limits?
  ↙           ↘
Yes            No → Reject & close
 ↓
4. Check exclusion list
 ↓
 Not excluded?
 ↙          ↘
Yes          No → Reject & close
 ↓
5. Create tcp_socket + tcp_server
 ↓
6. Start tcp_server
 ↓
7. Perform handshake
 ↓
 Success?
 ↙       ↘
Yes       No → Close connection
 ↓
8. Create tcp_channel
 ↓
9. Register in tcp_channels
 ↓
10. Begin realtime messaging
```

### Node ID Handshake

The handshake establishes peer identity and prevents spoofing:

**Phase 1: Challenge Generation**

```
Initiator                                    Responder
    │                                            │
    │  1. Generate random cookie                │
    │  2. Track cookie in syn_cookies           │
    │                                            │
    ├────── node_id_handshake (query) ─────────→│
    │         {cookie: random_256bit}            │
    │                                            │
    │                                    3. Receive cookie
    │                                    4. Sign: sig = sign(node_id || cookie)
    │                                    5. Generate own cookie
    │                                            │
    │←────── node_id_handshake (response) ──────┤
    │         {node_id, signature}               │
    │         + query {cookie: random_256bit}    │
    │                                            │
```

**Phase 2: Verification**

```
Initiator                                    Responder
    │                                            │
    │  6. Verify signature                       │
    │  7. Validate cookie exists                 │
    │  8. Extract node_id                        │
    │  9. Sign responder's cookie                │
    │                                            │
    ├────── node_id_handshake (response) ───────→│
    │         {node_id, signature}               │
    │                                            │
    │                                   10. Verify signature
    │                                   11. Validate cookie
    │                                   12. Extract node_id
    │                                            │
    │←──────────── Handshake Complete ──────────→│
    │                                            │
    │         Both sides know each other's       │
    │            authenticated node IDs          │
```

**V2 Handshake Additions**:
- Includes genesis block hash to prevent cross-network connections
- Adds random salt for additional entropy
- Both values are included in signature

**Syn Cookie Mechanism**:

Protects against handshake flooding:

```cpp
class syn_cookies {
    std::unordered_map<endpoint, syn_cookie_info> cookies;
    std::unordered_map<ip_address, unsigned> cookies_per_ip;

    constexpr static size_t max_cookies_per_ip = ...;  // e.g., 8 on mainnet
    constexpr static duration cookie_timeout = 5s;
};
```

**Protection**:
- Limits handshake requests per IP
- Expires cookies after timeout
- Prevents resource exhaustion attacks
- Rate-limits handshake attempts

### Connection Maintenance

#### Keepalive System

Connections are kept alive through periodic keepalive messages:

**Keepalive Thread Loop**:
```cpp
void network::run_keepalive() {
    while (!stopped) {
        wait(keepalive_period);  // 5s on mainnet

        flood_keepalive(0.75f);       // 75% of peers: random peer list
        flood_keepalive_self(0.25f);  // 25% of peers: self-advertisement

        tcp_channels.keepalive();     // Send keepalives to all channels
    }
}
```

**Peer Selection for Keepalive Content**:
```cpp
std::array<endpoint, 8> peers;

// Fill with:
// - Some slots: self endpoint (for peer discovery)
// - Remaining: random sample of known peers

send_keepalive(channel, peers);
```

**Liveness Detection**:
- Track `last_packet_received` timestamp on each channel
- Connections with no traffic for `cleanup_cutoff` duration are closed
- Default cleanup_cutoff: 5 minutes (mainnet)

#### Cleanup Process

**Cleanup Thread Loop**:
```cpp
void network::run_cleanup() {
    while (!stopped) {
        wait(5s);  // Run every 5 seconds

        auto cutoff = now() - cleanup_cutoff;
        cleanup(cutoff);  // Remove stale connections

        syn_cookies.purge(syn_cookie_cutoff);  // Remove expired cookies
        filter.update();  // Age duplicate filter
    }
}
```

**Cleanup Actions**:
- Remove channels with `last_packet_received < cutoff`
- Remove expired syn cookies
- Update network duplicate filter epoch

### Connection Termination

Connections can be closed for various reasons:

**Graceful Close**:
```cpp
channel->close();  // User-initiated or planned shutdown
```

**Automatic Close**:
- Keepalive timeout (no traffic for cleanup_cutoff duration)
- Protocol violation (invalid messages)
- Handshake failure
- Resource exhaustion
- Peer exclusion

**Close Flow**:
```
1. channel->close() called
   ↓
2. Mark channel as closed
   ↓
3. Stop sending queue
   ↓
4. Close underlying TCP socket
   ↓
5. Remove from tcp_channels container
   ↓
6. Cleanup channel resources
   ↓
7. Trigger disconnect_observer callbacks
```

---

## Peer Discovery and Management

### Discovery Methods

#### 1. Keepalive-based Discovery

Primary mechanism for peer discovery:

```
Node A learns about Node C through Node B:

A ←→ B ←→ C

1. Node A connected to Node B
2. Node B sends keepalive to A
3. Keepalive contains C's endpoint
4. Node A extracts C's endpoint
5. Node A attempts connection to C
```

**Reachout Thread**:
```cpp
void network::run_reachout() {
    while (!stopped) {
        wait(merge_period);  // 500ms mainnet

        auto keepalive = tcp_channels.sample_keepalive();
        if (keepalive) {
            for (auto & peer : keepalive->peers) {
                if (track_reachout(peer)) {  // Rate limit
                    merge_peer(peer);  // Attempt connection
                }
            }
        }
    }
}
```

**Reachout Rate Limiting**:
- Tracks last reachout attempt per endpoint
- Minimum delay between attempts: `peer_reachout` (250ms default)
- Prevents connection spam

#### 2. Cached Peers

Peers are persisted to database for bootstrap:

```cpp
void network::run_reachout_cached() {
    while (!stopped) {
        wait(cached_peer_reachout);  // 1s default

        auto peers = load_peers_from_database();
        for (auto & peer : peers) {
            if (track_reachout(peer)) {
                merge_peer(peer);
            }
        }
    }
}
```

**Peer Database**:
- Table: `peers` (in store)
- Key: `endpoint_key` (IP + port)
- Value: `timestamp` (last contact)

#### 3. Preconfigured Peers

Bootstrap nodes and preconfigured peers:

```cpp
// In config file or code
std::vector<std::string> preconfigured_peers = {
    "peering.nano.org",
    "bootstrap.nano.org",
    // ...
};

// Resolved via DNS and added to peer list
```

#### 4. DNS Resolution

Domain names are resolved to IP addresses:

```cpp
boost::asio::ip::tcp::resolver resolver;
auto results = resolver.resolve(hostname, port);

for (auto & endpoint : results) {
    merge_peer(endpoint);
}
```

### Peer Selection Strategies

#### Random Selection

For message flooding, peers are selected randomly:

```cpp
std::unordered_set<std::shared_ptr<channel>> random_set(
    size_t max_count,
    uint8_t minimum_version = 0
) const {
    auto channels = list(minimum_version);  // Get all matching channels
    std::shuffle(channels.begin(), channels.end(), rng);

    std::unordered_set<std::shared_ptr<channel>> result;
    for (size_t i = 0; i < std::min(max_count, channels.size()); ++i) {
        result.insert(channels[i]);
    }
    return result;
}
```

#### Representative-based Selection

For initial block broadcasts:

```cpp
// Send to all PRs (principal representatives)
auto pr_channels = get_pr_channels();

// Plus random non-PRs
auto non_pr_sample = random_set_non_pr(fanout - pr_channels.size());

// Combine
auto targets = pr_channels + non_pr_sample;
```

**Principal Representative (PR)**:
- Representative with voting weight ≥ 0.1% of online weight
- PRs receive priority for blocks and confirmation requests
- Ensures fast propagation to high-weight representatives

#### Fanout Calculation

Number of peers to broadcast to:

```cpp
size_t fanout(float scale = 1.0f) const {
    auto base = std::max(
        config.minimum_fanout,
        static_cast<size_t>(std::sqrt(size()) * scale)
    );
    return std::min(base, size());
}
```

**Formula**: `fanout = √(peer_count) × scale`

- Scales with network size
- Adjustable via scale parameter
- Minimum fanout enforced
- Examples:
  - 100 peers: fanout ≈ 10
  - 400 peers: fanout ≈ 20
  - 10000 peers: fanout ≈ 100

### Connection Limits

#### Per-IP Limits

```cpp
bool max_ip_connections(tcp_endpoint const & endpoint) const {
    auto ip = endpoint.address();
    auto count = count_by_ip(ip);
    return count >= config.max_peers_per_ip;  // Default: 4
}
```

**Purpose**: Prevent single IP from consuming too many connections

#### Per-Subnet Limits

```cpp
bool max_subnetwork_connections(tcp_endpoint const & endpoint) const {
    auto subnet = map_address_to_subnetwork(endpoint.address());
    auto count = count_by_subnetwork(subnet);
    return count >= config.max_peers_per_subnetwork;  // Default: 16
}
```

**Subnetwork Mapping**:
- IPv4: Use full /32 address (no subnetting)
- IPv6: Use /64 prefix

**Purpose**: Prevent single organization/datacenter from dominating connections

#### Global Limits

```cpp
struct tcp_config {
    size_t max_inbound_connections = 256;
    size_t max_outbound_connections = 16;
};
```

**Asymmetry Rationale**:
- More inbound allows serving other nodes
- Limited outbound focuses on quality connections
- Inbound doesn't consume local resources for connection attempts

### Peer Exclusion

**File**: `nano/node/peer_exclusion.hpp`

Temporarily bans misbehaving peers:

```cpp
class peer_exclusion {
    constexpr static uint64_t score_limit = 2;
    constexpr static duration exclude_time_hours = 1h;
    constexpr static duration exclude_remove_hours = 24h;

    uint64_t add(tcp_endpoint const &);        // Increment score
    bool check(tcp_endpoint const &) const;    // Is peer excluded?
    void remove(tcp_endpoint const &);         // Remove exclusion
};
```

**Exclusion Triggers**:
- Protocol violations
- Invalid messages
- Excessive handshake failures
- Repeated bad behavior

**Exclusion Flow**:
```
Misbehavior detected
        ↓
Increment peer score
        ↓
    Score ≥ limit?
    ↙         ↘
  Yes          No → Continue
   ↓
Set exclude_until = now + 1h
   ↓
Disconnect peer
   ↓
Reject future connections for 1h
   ↓
After 24h: Remove from list
```

---

## Traffic Control and Bandwidth Management

### Traffic Types

**File**: `nano/node/transport/traffic_type.hpp`

```cpp
enum class traffic_type {
    generic,                    // Default/uncategorized
    bootstrap_server,           // Serving bootstrap requests
    bootstrap_requests,         // Making bootstrap requests
    block_broadcast,            // Block flooding
    block_broadcast_initial,    // Initial block broadcast (to PRs)
    block_broadcast_rpc,        // Blocks from RPC (higher priority)
    confirmation_requests,      // Vote solicitation
    keepalive,                  // Connection maintenance
    vote,                       // Vote messages
    vote_rebroadcast,           // Rebroadcasted votes
    vote_reply,                 // Vote responses
    rep_crawler,                // Representative crawling
    telemetry,                  // Telemetry exchange
    test,                       // Testing only
};
```

### Channel Queue Priority

Each TCP channel maintains separate queues per traffic type:

```cpp
class tcp_channel_queue {
    enum_array<traffic_type, queue_t> queues;

    size_t priority(traffic_type type) const {
        switch (type) {
            case traffic_type::vote:
            case traffic_type::vote_reply:
                return 10;  // Highest priority

            case traffic_type::block_broadcast_initial:
            case traffic_type::block_broadcast_rpc:
                return 8;

            case traffic_type::confirmation_requests:
                return 7;

            case traffic_type::block_broadcast:
                return 6;

            case traffic_type::keepalive:
                return 5;

            case traffic_type::telemetry:
                return 4;

            case traffic_type::bootstrap_requests:
                return 3;

            case traffic_type::bootstrap_server:
            case traffic_type::generic:
                return 2;  // Lowest priority

            default:
                return 1;
        }
    }
};
```

**Queue Limits**:
- `max_size = 32`: Soft limit per traffic type
- `full_size = 128`: Hard limit per traffic type
- Total per channel: up to 128 messages per traffic type

**Sending Algorithm**:
```cpp
value_t next() {
    // Round-robin across queues weighted by priority
    for (int attempts = 0; attempts < queues.size(); ++attempts) {
        current = next_queue(current);
        if (!queues[current].empty()) {
            return queues[current].pop_front();
        }
    }
    return empty;
}
```

**Effect**:
- Higher priority messages sent more frequently
- No starvation (all queues eventually serviced)
- Automatic load balancing

### Bandwidth Limiting

**File**: `nano/node/bandwidth_limiter.hpp`

```cpp
class bandwidth_limiter {
    rate_limiter limiter_generic;     // For most traffic
    rate_limiter limiter_bootstrap;   // For bootstrap traffic

    bool should_pass(size_t buffer_size, traffic_type type) {
        auto & limiter = select_limiter(type);
        return limiter.should_pass(buffer_size);
    }
};
```

**Rate Limiter**:

Token bucket algorithm:

```cpp
class rate_limiter {
    size_t limit;            // Bytes per second
    double burst_ratio;      // Burst multiplier (e.g., 3.0)
    size_t bucket_max;       // limit * burst_ratio
    size_t bucket_size;      // Current tokens

    bool should_pass(size_t size) {
        refill_tokens();  // Add tokens based on time elapsed

        if (bucket_size >= size) {
            bucket_size -= size;
            return true;  // Allow
        }
        return false;  // Drop
    }

    void refill_tokens() {
        auto elapsed = now() - last_refill;
        auto tokens = (elapsed.count() * limit) / 1s;
        bucket_size = std::min(bucket_size + tokens, bucket_max);
        last_refill = now();
    }
};
```

**Configuration**:
```cpp
struct bandwidth_limiter_config {
    size_t generic_limit;          // e.g., 5 MB/s
    double generic_burst_ratio;    // e.g., 3.0
    size_t bootstrap_limit;        // e.g., 2 MB/s
    double bootstrap_burst_ratio;  // e.g., 2.0
};
```

**Traffic Classification**:
- `bootstrap_server`, `bootstrap_requests` → bootstrap limiter
- All others → generic limiter

**Behavior**:
- Sustained traffic rate limited to `limit` bytes/second
- Bursts allowed up to `limit × burst_ratio` bytes
- Messages exceeding limits are dropped
- Per-node limits (not per-channel)

### Message Flooding

#### flood_block_initial

Broadcast block to PRs and random sample of non-PRs:

```cpp
size_t flood_block_initial(std::shared_ptr<block> const & block) const {
    auto pr_channels = list_pr();  // All principal representatives

    auto non_pr_count = fanout() - pr_channels.size();
    auto non_pr_channels = random_set_non_pr(non_pr_count);

    auto message = publish{ constants, block, true };  // is_originator = true

    size_t count = 0;
    for (auto & channel : pr_channels + non_pr_channels) {
        channel->send(message, traffic_type::block_broadcast_initial);
        count++;
    }

    return count;
}
```

**Purpose**: Rapid dissemination to high-weight representatives

#### flood_block

Broadcast block to random sample of peers:

```cpp
size_t flood_block(
    std::shared_ptr<block> const & block,
    traffic_type type
) const {
    auto message = publish{ constants, block, false };  // is_originator = false

    auto channels = random_set(fanout());

    size_t count = 0;
    for (auto & channel : channels) {
        channel->send(message, type);
        count++;
    }

    return count;
}
```

**Usage**: Rebroadcast of blocks received from other peers

#### flood_vote

Broadcast vote based on representative status:

```cpp
size_t flood_vote_pr(std::shared_ptr<vote> const & vote) const {
    // PRs broadcast to all peers
    auto message = confirm_ack{ constants, vote, false };

    auto channels = list();  // All channels

    for (auto & channel : channels) {
        channel->send(message, traffic_type::vote);
    }

    return channels.size();
}

size_t flood_vote_non_pr(std::shared_ptr<vote> const & vote, float scale) const {
    // Non-PRs broadcast to subset
    auto message = confirm_ack{ constants, vote, false };

    auto channels = random_set(fanout(scale));

    for (auto & channel : channels) {
        channel->send(message, traffic_type::vote);
    }

    return channels.size();
}
```

**Rationale**:
- PR votes are critical → broadcast widely
- Non-PR votes less impactful → broadcast to subset
- Reduces vote traffic while maintaining network visibility

#### flood_keepalive

Broadcast keepalive messages:

```cpp
size_t flood_keepalive(float scale = 1.0f) const {
    std::array<endpoint, 8> peers;
    random_fill(peers);  // Fill with random known peers

    auto message = keepalive{ constants };
    message.peers = peers;

    auto channels = random_set(fanout(scale));

    for (auto & channel : channels) {
        channel->send(message, traffic_type::keepalive);
    }

    return channels.size();
}
```

### Duplicate Message Filtering

**File**: `nano/lib/network_filter.hpp`

Prevents processing duplicate messages using probabilistic filter:

```cpp
class network_filter {
    using digest_t = uint128_t;  // SipHash digest
    using epoch_t = uint64_t;

    std::vector<entry> items;  // Hash table

    struct entry {
        digest_t digest;
        epoch_t epoch;
    };

    bool apply(uint8_t const * bytes, size_t count, digest_t * out = nullptr) {
        auto digest = hash(bytes, count);  // SipHash-2-4-128
        auto & element = get_element(digest);

        bool existed = compare(element, digest);

        element.digest = digest;
        element.epoch = current_epoch;

        if (out) *out = digest;

        return existed;  // true if duplicate
    }
};
```

**Algorithm**:
1. Hash message bytes using SipHash
2. Look up digest in hash table
3. If exists with recent epoch → duplicate
4. Otherwise → unique, insert/update entry
5. Periodically age out old entries

**Configuration**:
- `duplicate_filter_size`: Number of entries (default: 1M)
- `duplicate_filter_cutoff`: Age threshold in epochs (default: 60)

**Properties**:
- False negative probability: ~0 (SipHash collision probability)
- False positive probability: inversely proportional to filter size
- Memory: O(filter_size)
- Time: O(1) per message

**Usage**:
```cpp
nano::network_filter::digest_t digest;
bool is_duplicate = filter.apply(message_bytes, message_size, &digest);

if (is_duplicate) {
    // Drop message
    return;
}

// Process message
```

**Digests Stored in Messages**:
- `publish`: Contains digest for rebroadcast filtering
- `confirm_ack`: Contains digest for vote deduplication

---

## Message Processing

### Message Processor

**File**: `nano/node/message_processor.hpp`, `nano/node/message_processor.cpp`

Asynchronous message processing with fair queuing:

```cpp
class message_processor {
    fair_queue<entry_t, no_value> queue;  // Priority queue by channel

    using entry_t = std::pair<
        std::unique_ptr<message>,
        std::shared_ptr<channel>
    >;

    std::vector<std::thread> threads;  // Worker threads
};
```

**Configuration**:
```cpp
struct message_processor_config {
    size_t threads = std::clamp(hardware_concurrency() / 4, 1u, 2u);
    size_t max_queue = 64;  // Per-channel queue limit
};
```

**Processing Flow**:
```
Message arrives from network
        ↓
tcp_server::receive_message()
        ↓
Deserialize message + header
        ↓
message_processor.put(message, channel)
        ↓
Fair queue (round-robin by channel)
        ↓
Worker thread dequeues message
        ↓
message_processor.process(message, channel)
        ↓
Visitor pattern dispatches to handler
        ↓
Handler processes message (publish → block_processor, etc.)
```

**Fair Queuing**:

Prevents single channel from monopolizing processor:

```cpp
template<typename T, typename Tag>
class fair_queue {
    std::unordered_map<Tag, std::deque<T>> queues;  // One queue per tag (channel)
    std::deque<Tag> order;  // Round-robin order

    void push(T value, Tag tag) {
        queues[tag].push_back(value);
        if (!contains(order, tag)) {
            order.push_back(tag);
        }
    }

    T pop() {
        if (order.empty()) return nullopt;

        auto tag = order.front();
        order.pop_front();

        auto & queue = queues[tag];
        auto value = queue.front();
        queue.pop_front();

        if (!queue.empty()) {
            order.push_back(tag);  // Re-add if more messages
        } else {
            queues.erase(tag);  // Clean up empty queue
        }

        return value;
    }
};
```

**Benefits**:
- Fair CPU allocation across channels
- Prevents DoS from single source
- Maintains responsiveness

### Message Handlers

Messages are dispatched via visitor pattern:

```cpp
class message_visitor {
    virtual void keepalive(nano::keepalive const &);
    virtual void publish(nano::publish const &);
    virtual void confirm_req(nano::confirm_req const &);
    virtual void confirm_ack(nano::confirm_ack const &);
    virtual void node_id_handshake(nano::node_id_handshake const &);
    virtual void telemetry_req(nano::telemetry_req const &);
    virtual void telemetry_ack(nano::telemetry_ack const &);
    virtual void asc_pull_req(nano::asc_pull_req const &);
    virtual void asc_pull_ack(nano::asc_pull_ack const &);
    // ...
};

message->visit(visitor);  // Double dispatch to handler
```

**Handler Implementations**:

```cpp
void keepalive(nano::keepalive const & message) override {
    // Extract peer endpoints
    network.merge_peers(message.peers);

    // Update channel's last keepalive
    channel->set_last_keepalive(message);
}

void publish(nano::publish const & message) override {
    // Check duplicate filter
    if (network.filter.check(message.digest)) {
        stats.inc(duplicate_publish);
        return;
    }

    // Add to block processor
    block_processor.add(message.block, block_source::live);

    // Flood to other peers (if not originator)
    if (!message.is_originator()) {
        network.flood_block(message.block, traffic_type::block_broadcast);
    }
}

void confirm_req(nano::confirm_req const & message) override {
    // Check if we're a representative
    if (!node.is_representative()) {
        return;
    }

    // Generate votes for requested blocks
    for (auto & [hash, root] : message.roots_hashes) {
        auto vote = generate_vote(hash, root);
        if (vote) {
            network.flood_vote(vote);
        }
    }
}

void confirm_ack(nano::confirm_ack const & message) override {
    // Check duplicate filter
    if (network.filter.check(message.digest)) {
        stats.inc(duplicate_vote);
        return;
    }

    // Validate vote signature
    if (!validate_vote(message.vote)) {
        return;
    }

    // Process vote in active elections
    active_elections.vote(message.vote);

    // Flood to other peers if from PR
    if (is_pr(message.vote->account)) {
        network.flood_vote_pr(message.vote);
    }
}
```

---

## Security Features

### Connection Filtering

#### IP-based Filtering

```cpp
bool not_a_peer(nano::endpoint const & endpoint, bool allow_local) const {
    auto ip = endpoint.address();

    // Reject loopback (unless allowed)
    if (ip.is_loopback() && !allow_local) {
        return true;
    }

    // Reject multicast
    if (ip.is_multicast()) {
        return true;
    }

    // Reject unspecified (0.0.0.0 or ::)
    if (ip.is_unspecified()) {
        return true;
    }

    // Check exclusion list
    if (excluded_peers.check(endpoint)) {
        return true;
    }

    return false;
}
```

#### Subnet-based Filtering

For IPv6, connections are limited per /64 subnet:

```cpp
boost::asio::ip::address map_address_to_subnetwork(
    boost::asio::ip::address const & address
) {
    if (address.is_v6()) {
        auto v6 = address.to_v6();
        auto bytes = v6.to_bytes();

        // Zero out lower 64 bits (keep /64 prefix)
        std::fill(bytes.begin() + 8, bytes.end(), 0);

        return boost::asio::ip::address_v6(bytes);
    }

    // For IPv4, return address as-is
    return address;
}
```

### Rate Limiting

#### Handshake Rate Limiting

Syn cookie mechanism limits handshake requests per IP:

```cpp
std::optional<uint256_union> syn_cookies::assign(endpoint const & endpoint) {
    auto ip = endpoint.address();

    // Check per-IP limit
    if (cookies_per_ip[ip] >= max_cookies_per_ip) {
        return std::nullopt;  // Rate limited
    }

    // Generate cookie
    auto cookie = random_256bit();

    // Store cookie
    cookies[endpoint] = { cookie, now() };
    cookies_per_ip[ip]++;

    return cookie;
}
```

**Limits**:
- Mainnet: 8 handshakes per IP
- Dev/beta: 256 per IP (for testing)

#### Bandwidth Rate Limiting

Applied at channel send time:

```cpp
bool channel::send(message const & msg, traffic_type type, callback_t cb) {
    auto buffer = msg.to_shared_const_buffer();

    // Check bandwidth limiter
    if (!node.bandwidth_limiter.should_pass(buffer.size(), type)) {
        stats.inc(traffic_rejected, type);
        return false;  // Drop message
    }

    // Queue for sending
    return send_impl(msg, type, cb);
}
```

### DoS Protection

#### Message Deduplication

Network filter prevents processing duplicate messages:

```cpp
// Check if message already seen
if (network.filter.check(message_digest)) {
    stats.inc(duplicate_message);
    return;  // Drop silently
}

// Mark as seen
network.filter.apply(message_digest);

// Process message
process_message(message);
```

#### Connection Limits

Multiple layers of limits:
- Global inbound/outbound limits
- Per-IP connection limits
- Per-subnet connection limits
- Peer exclusion for misbehavior

#### Queue Limits

Per-channel send queue limits prevent memory exhaustion:

```cpp
bool tcp_channel_queue::push(traffic_type type, entry_t entry) {
    if (size(type) >= full_size) {
        return false;  // Queue full, drop message
    }

    queues[type].push_back(entry);
    total_size++;

    return true;
}
```

#### Fair Queuing

Message processor fair queue prevents single channel from monopolizing CPU:

```cpp
if (queue.size(channel) >= config.max_queue) {
    stats.inc(processor_queue_full);
    return false;  // Reject message
}

queue.push(message, channel);
```

### Peer Exclusion

Automatic exclusion for misbehaving peers:

```cpp
void network::exclude(std::shared_ptr<channel> const & channel) {
    auto endpoint = channel->get_remote_endpoint();

    // Add to exclusion list
    auto score = excluded_peers.add(endpoint);

    // Disconnect
    channel->close();
    tcp_channels.erase(endpoint);

    // Log
    logger.warn(log::type::network,
        "Excluded peer {} with score {}",
        endpoint, score);
}
```

**Triggers**:
- Invalid handshake
- Protocol violations
- Invalid message formats
- Repeated errors

**Duration**: 1 hour exclusion, removed from list after 24 hours

---

## Configuration

### Network Configuration

```toml
[node.network]
# Peer reachout interval (milliseconds)
peer_reachout = 250

# Cached peer reachout interval (milliseconds)
cached_peer_reachout = 1000

# Maximum peers per IP address
max_peers_per_ip = 4

# Maximum peers per /64 subnet (IPv6)
max_peers_per_subnetwork = 16

# Duplicate filter size (number of entries)
duplicate_filter_size = 1048576

# Duplicate filter age cutoff (seconds)
duplicate_filter_cutoff = 60

# Minimum fanout for message flooding
minimum_fanout = 2
```

### TCP Configuration

```toml
[node.tcp]
# Maximum inbound connections
max_inbound_connections = 256

# Maximum outbound connections
max_outbound_connections = 16

# I/O operation timeout (seconds)
io_timeout = 15

# Connection establishment timeout (seconds)
connect_timeout = 5
```

### Message Processor Configuration

```toml
[node.message_processor]
# Number of worker threads
threads = 2

# Maximum messages queued per channel
max_queue = 64
```

### Bandwidth Configuration

```toml
[node.bandwidth_limit]
# Generic traffic limit (bytes/second)
# 0 = unlimited
limit = 10485760  # 10 MB/s

# Burst ratio for generic traffic
burst_ratio = 3.0

# Bootstrap traffic limit (bytes/second)
bootstrap_limit = 5242880  # 5 MB/s

# Burst ratio for bootstrap traffic
bootstrap_burst_ratio = 2.0
```

### Peering Port

```toml
[node]
# Network peering port
# 0 = random available port
peering_port = 7075  # Mainnet default
```

---

## Performance Considerations

### Threading Model

The network subsystem uses multiple dedicated threads:

| Thread | Purpose | Count |
|--------|---------|-------|
| Boost.Asio I/O | Socket I/O operations | Configurable (default: CPU/2) |
| Message Processor | Message processing | Configurable (default: CPU/4, max 2) |
| Network Cleanup | Connection cleanup | 1 |
| Network Keepalive | Keepalive flooding | 1 |
| Network Reachout | Peer connection attempts | 1 |
| Cached Reachout | Cached peer attempts | 1 |

**Total**: ~8-16 threads for network operations on typical system

### Memory Usage

**Per Connection**:
- TCP socket: ~4 KB
- Channel queue: ~16 KB (128 messages max)
- Channel metadata: ~1 KB
- Total per connection: ~21 KB

**Global**:
- Duplicate filter: 1M entries × 24 bytes = ~24 MB
- Syn cookies: ~100 entries × 100 bytes = ~10 KB
- Peer exclusion: ~5K entries × 50 bytes = ~250 KB

**Example**:
- 256 connections: 256 × 21 KB = ~5.4 MB
- Global: ~24.3 MB
- **Total**: ~30 MB for network subsystem

### Bandwidth Usage

**Keepalive Traffic**:
- Message size: 152 bytes
- Frequency: Every 5 seconds to 75% of peers
- For 100 peers: (152 × 75) / 5 ≈ 2.3 KB/s
- For 256 peers: (152 × 192) / 5 ≈ 5.8 KB/s

**Block Propagation**:
- Block size: ~200-250 bytes
- Fanout: ~√(peer_count)
- For 100 peers, 10 blocks/sec: 250 × 10 × 10 = 25 KB/s
- With 256 peers: 250 × 10 × 16 = 40 KB/s

**Vote Propagation**:
- Vote size: ~150-200 bytes
- PRs broadcast to all, non-PRs to subset
- Highly variable based on election activity

**Typical Sustained Usage**:
- Idle node: ~10-50 KB/s
- Active node: ~100-500 KB/s
- Heavy activity: ~1-5 MB/s

### Optimization Techniques

1. **Zero-copy Message Sending**:
   - Uses `shared_const_buffer` to avoid copying
   - Messages serialized once, sent to multiple peers

2. **Message Deduplication**:
   - SipHash-based filter prevents reprocessing
   - O(1) lookup time
   - Saves CPU and bandwidth

3. **Fair Queuing**:
   - Prevents single source from monopolizing
   - Maintains responsiveness under load

4. **Traffic Prioritization**:
   - Critical messages (votes) sent first
   - Prevents head-of-line blocking

5. **Connection Pooling**:
   - Multi-index container enables O(1) lookups
   - Efficient channel selection for flooding

6. **Async I/O**:
   - Non-blocking operations
   - Coroutines for clean async code
   - Scales to thousands of connections

7. **Bandwidth Limiting**:
   - Token bucket algorithm
   - Smooth traffic shaping
   - Prevents network congestion

---

## Troubleshooting

### Connection Issues

**Symptom**: Unable to connect to peers

**Check**:
- Firewall allows TCP port 7075 (mainnet)
- Router has port forwarding configured
- `peering_port` is correct in config
- DNS resolution working (`peering.nano.org`)
- Not behind CGNAT (carrier-grade NAT)

**Logs**:
```
nano_node --log_level debug | grep network
```

### Peer Discovery Problems

**Symptom**: Low peer count

**Check**:
- `peer_reachout` not set to 0
- `cached_peer_reachout` not set to 0
- Preconfigured peers accessible
- Not isolated by exclusion

**Force peer connection**:
```bash
curl -d '{"action":"keepalive","address":"::ffff:192.0.2.1","port":"7075"}' \
  http://localhost:7076
```

### High Bandwidth Usage

**Symptom**: Excessive network traffic

**Check**:
- Bandwidth limits configured correctly
- Not running bootstrap excessively
- `max_inbound_connections` reasonable
- No traffic loops (misconfigured nodes)

**Reduce bandwidth**:
```toml
[node.bandwidth_limit]
limit = 5242880  # 5 MB/s
bootstrap_limit = 2621440  # 2.5 MB/s
```

### Handshake Failures

**Symptom**: Connections immediately close

**Check**:
- Both nodes on same network (live/beta/dev)
- Protocol versions compatible
- Clocks synchronized (for handshake timing)
- Not excluded by remote peer

**Logs**:
```
grep "handshake" nano_node.log
```

### Memory Issues

**Symptom**: High memory usage from network

**Check**:
- `max_inbound_connections` not excessive
- `duplicate_filter_size` reasonable
- No memory leaks (update to latest version)
- Channel queues not backed up

**Reduce memory**:
```toml
[node.network]
duplicate_filter_size = 524288  # 512K (half default)

[node.tcp]
max_inbound_connections = 128  # Reduce from 256
```

---

## Summary

The Nano node network implementation is a sophisticated P2P system that provides:

- **Efficient Communication**: Custom binary protocol optimized for low latency
- **Robust Peer Discovery**: Automatic peer discovery through keepalive messages
- **Fair Resource Allocation**: Traffic prioritization and fair queuing
- **Security**: Multiple layers of DoS protection and rate limiting
- **Scalability**: Handles thousands of concurrent connections efficiently
- **Flexibility**: Configurable limits and behaviors for different deployments

The network layer is the foundation for all inter-node communication in Nano, enabling fast block propagation, vote dissemination, and consensus coordination while maintaining security and preventing resource exhaustion.
