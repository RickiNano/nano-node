# Nano Block Types

Nano uses a unique block-lattice architecture where each account has its own blockchain. This document explains the different block types and their purposes.

## Block-Lattice Architecture

Unlike traditional blockchains where all transactions are in a single chain, Nano uses a **block-lattice** structure:

- Each account has its own independent blockchain (account-chain)
- Transactions are split into send and receive blocks on different chains
- Sending and receiving are asynchronous operations
- The sender creates a send block on their chain
- The receiver creates a receive block on their chain
- This enables parallel processing and instant finality per account

## Block Type Evolution

Nano has evolved through two generations of block types:

### Legacy Block Types (Pre-State)

Originally, Nano had four specialized block types:
- **send** - Send funds to another account
- **receive** - Receive funds from a pending send
- **open** - Create a new account and receive funds
- **change** - Change account representative

**Status**: These are legacy block types. While still supported for backward compatibility, new blocks should use state blocks.

### Modern Block Type

- **state** - Universal block type that can perform all operations

**Status**: State blocks are the modern, recommended approach. Since protocol version 1 (Epoch 1), state blocks are the primary block type.

## Block Type Details

### 1. Send Block (Legacy)

Sends funds from one account to another.

#### Structure

```cpp
class send_block {
    nano::block_hash previous;      // Hash of previous block in sender's chain
    nano::account destination;       // Destination account
    nano::amount balance;            // Sender's new balance AFTER send
    nano::signature signature;       // Ed25519 signature
    uint64_t work;                   // Proof of work
};
```

#### Purpose

- Deducts funds from sender's account
- Creates a pending transaction for the receiver
- Updates sender's balance
- The amount sent is calculated as: `previous_balance - new_balance`

#### Example Flow

```
Account A Balance: 100 NANO
                ↓
        Send 30 NANO to Account B
                ↓
    [Send Block on Account A's chain]
    - previous: hash of last block
    - destination: Account B
    - balance: 70 NANO (100 - 30)
                ↓
Account A Balance: 70 NANO
Account B has 30 NANO pending
```

#### Processing

1. Validates signature against sender's public key
2. Validates proof-of-work
3. Checks previous block exists and is the account head
4. Verifies new balance < previous balance
5. Creates pending entry for destination
6. Updates sender's account info

### 2. Receive Block (Legacy)

Receives funds from a pending send block.

#### Structure

```cpp
class receive_block {
    nano::block_hash previous;      // Hash of previous block in receiver's chain
    nano::block_hash source;         // Hash of the send block being received
    nano::signature signature;       // Ed25519 signature
    uint64_t work;                   // Proof of work
};
```

#### Purpose

- Accepts pending funds
- Increases receiver's balance
- Removes pending entry
- The amount received comes from the source send block

#### Example Flow

```
Account B has 30 NANO pending from Account A
                ↓
        Receive pending funds
                ↓
    [Receive Block on Account B's chain]
    - previous: hash of last block (or 0 if opening)
    - source: hash of send block from Account A
                ↓
Account B Balance: Original + 30 NANO
Pending entry removed
```

#### Processing

1. Validates signature against receiver's public key
2. Validates proof-of-work
3. Checks source send block exists
4. Verifies pending entry exists for this account/source pair
5. Adds send amount to receiver's balance
6. Deletes pending entry

### 3. Open Block (Legacy)

Creates a new account by receiving the first funds.

#### Structure

```cpp
class open_block {
    nano::block_hash source;         // Hash of the send block being received
    nano::account representative;    // Initial representative for voting
    nano::account account;           // Account being opened (public key)
    nano::signature signature;       // Ed25519 signature
    uint64_t work;                   // Proof of work
};
```

#### Purpose

- Opens a new account (first block in account-chain)
- Receives the first funds
- Sets initial representative
- No previous block (this is the genesis block for the account)

#### Example Flow

```
Account C does not exist yet
Account A sends 50 NANO to Account C
                ↓
    [Open Block on Account C's chain]
    - source: hash of send block from Account A
    - representative: nano_1rep...
    - account: nano_3c... (Account C)
                ↓
Account C created with 50 NANO balance
```

#### Processing

1. Validates signature against account public key
2. Validates proof-of-work
3. Verifies account doesn't already exist
4. Checks source send block exists and has pending entry
5. Creates new account with balance from source
6. Sets representative for voting weight
7. Deletes pending entry

### 4. Change Block (Legacy)

Changes the account's representative without transferring funds.

#### Structure

```cpp
class change_block {
    nano::block_hash previous;       // Hash of previous block in chain
    nano::account representative;    // New representative
    nano::signature signature;       // Ed25519 signature
    uint64_t work;                   // Proof of work
};
```

#### Purpose

- Updates the representative for consensus voting
- Does not change balance
- Representatives vote with the weight of accounts that delegated to them
- Used for load balancing voting power

#### Example Flow

```
Account D Balance: 100 NANO
Current Representative: nano_1old...
                ↓
        Change representative
                ↓
    [Change Block on Account D's chain]
    - previous: hash of last block
    - representative: nano_1new...
                ↓
Account D Balance: 100 NANO (unchanged)
Representative: nano_1new... (updated)
Voting weight moved from old to new representative
```

#### Processing

1. Validates signature against account's public key
2. Validates proof-of-work
3. Checks previous block exists and is account head
4. Moves voting weight from old to new representative
5. Updates account's representative field
6. Balance remains unchanged

### 5. State Block (Modern, Universal)

The universal block type that can perform any operation: send, receive, change representative, or epoch upgrade.

#### Structure

```cpp
class state_block {
    nano::account account;           // Account that owns this block
    nano::block_hash previous;       // Previous block (0 if opening)
    nano::account representative;    // Representative for voting
    nano::amount balance;            // Account balance AFTER this block
    nano::link link;                 // Context-dependent field:
                                     //   - Send: destination account
                                     //   - Receive/Open: source block hash
                                     //   - Change: 0
                                     //   - Epoch: epoch link
    nano::signature signature;       // Ed25519 signature
    uint64_t work;                   // Proof of work
};
```

#### Operation Determination

The state block's operation is determined by comparing the new balance with the previous balance:

```cpp
if (balance < previous_balance) {
    // SEND operation
    amount_sent = previous_balance - balance;
    destination = link (interpreted as account);
}
else if (balance > previous_balance && link != 0) {
    // RECEIVE operation
    amount_received = balance - previous_balance;
    source_block = link (interpreted as block_hash);
}
else if (balance == previous_balance && link != 0) {
    // Could be RECEIVE (amount = 0) or special operation
}
else if (balance == previous_balance && link == 0) {
    // CHANGE operation (representative only)
}

// Special case:
if (previous == 0) {
    // OPEN operation (first block in account)
}
```

#### Example: State Block as Send

```
Account E Balance: 200 NANO
                ↓
        Send 75 NANO to Account F
                ↓
    [State Block on Account E's chain]
    - account: nano_3e...
    - previous: hash of last block
    - representative: nano_1rep... (unchanged)
    - balance: 125 NANO
    - link: nano_3f... (Account F, as destination)
                ↓
Account E Balance: 125 NANO
Account F has 75 NANO pending
```

#### Example: State Block as Receive

```
Account F has 75 NANO pending
Account F Balance: 50 NANO
                ↓
        Receive pending funds
                ↓
    [State Block on Account F's chain]
    - account: nano_3f...
    - previous: hash of last block
    - representative: nano_1rep... (unchanged)
    - balance: 125 NANO (50 + 75)
    - link: <hash of send block> (as source)
                ↓
Account F Balance: 125 NANO
Pending entry removed
```

#### Example: State Block as Change

```
Account G Balance: 100 NANO
                ↓
        Change representative only
                ↓
    [State Block on Account G's chain]
    - account: nano_3g...
    - previous: hash of last block
    - representative: nano_1newrep... (CHANGED)
    - balance: 100 NANO (unchanged)
    - link: 0 (no source, no destination)
                ↓
Account G Balance: 100 NANO (unchanged)
Representative updated
```

#### Example: State Block as Open

```
Account H does not exist
Account E sends 60 NANO to Account H
                ↓
        Account H opens with state block
                ↓
    [State Block on Account H's chain]
    - account: nano_3h...
    - previous: 0 (first block)
    - representative: nano_1rep...
    - balance: 60 NANO
    - link: <hash of send block> (source)
                ↓
Account H created with 60 NANO
```

#### Processing

State block processing is more complex than legacy blocks:

1. Validates signature against account public key
2. Validates proof-of-work
3. Checks if epoch block (special link value)
4. If account exists:
   - Verifies previous block matches account head
   - Determines operation by comparing balances
   - For sends: creates pending entry
   - For receives: validates and removes pending entry
   - For changes: just updates representative
5. If account doesn't exist (open):
   - Verifies previous is 0
   - Validates source send block
   - Creates new account

#### Advantages of State Blocks

- **Unified**: One block type for all operations
- **Self-contained**: Balance is explicit, not calculated
- **Efficient**: Can change representative during send/receive
- **Pruning-friendly**: Contains all account state
- **Simplified**: Easier to implement and validate

## Epoch Blocks

Epoch blocks are special state blocks used for protocol upgrades.

#### Structure

Same as state block, but with special characteristics:
- Link field contains epoch link (special magic value)
- Signed by epoch signer (network-defined authority)
- Balance equals previous balance (no value transfer)
- Upgrades the account to a new epoch version

#### Purpose

- Enable protocol upgrades without hard forks
- Upgrade accounts to support new features
- Accounts can be upgraded individually
- Epoch 1: Introduced state blocks
- Epoch 2: Updated work difficulty

#### Epochs

```cpp
enum class epoch : uint8_t {
    invalid = 0,
    unspecified = 1,
    epoch_0 = 2,  // Original protocol
    epoch_1 = 3,  // State blocks introduced
    epoch_2 = 4   // Work difficulty update
};
```

#### Example: Epoch Upgrade

```
Account I is on Epoch 1
                ↓
        Upgrade to Epoch 2
                ↓
    [Epoch State Block]
    - account: nano_3i...
    - previous: hash of last block
    - representative: (unchanged)
    - balance: (unchanged)
    - link: epoch_2_link
    - signature: (from epoch signer)
                ↓
Account I upgraded to Epoch 2
Can now use Epoch 2 features
```

## Block Sideband

All blocks have associated metadata stored separately called the "sideband":

```cpp
class block_sideband {
    nano::block_hash successor;      // Next block in chain (0 if head)
    nano::account account;           // Account that owns this block
    nano::amount balance;            // Balance after this block
    uint64_t height;                 // Block height in account chain
    uint64_t timestamp;              // When block was created
    nano::block_details details;     // Operation flags
    nano::epoch source_epoch;        // Epoch of source block (for receives)
};

class block_details {
    nano::epoch epoch;               // Epoch version of this block
    bool is_send;                    // Is this a send operation?
    bool is_receive;                 // Is this a receive operation?
    bool is_epoch;                   // Is this an epoch block?
};
```

The sideband is:
- Not part of the block hash
- Stored in the database
- Used for quick lookups without deserializing blocks
- Contains computed information about the block

## Block Processing Pipeline

When a block is received:

1. **Pre-validation**
   - Check block size
   - Parse block structure
   - Extract block type

2. **Signature Validation**
   - Verify Ed25519 signature
   - Signature must be from account owner (or epoch signer for epoch blocks)

3. **Work Validation**
   - Verify proof-of-work meets difficulty threshold
   - Difficulty varies by epoch and operation type

4. **Ledger Processing** (Visitor Pattern)
   - Call appropriate handler: `send_block()`, `receive_block()`, etc.
   - Validate block-specific rules
   - Check previous block exists (if not opening)
   - Check account state consistency

5. **State Updates**
   - Update account info (head, balance, representative, height)
   - For sends: create pending entry
   - For receives: delete pending entry, verify source
   - Update representative weights for consensus

6. **Storage**
   - Store block in database with sideband
   - Update account-chain links
   - Update indices

7. **Consensus**
   - Trigger election for confirmation
   - Representatives vote on the block
   - Block is cemented once confirmed

## Block Validation Rules

### Common to All Block Types

- Signature must be valid Ed25519 signature
- Work must meet network difficulty threshold
- Block must not already exist in ledger
- Account must not be the burn account (all zeros)

### Send Block

- Previous block must exist and be account head
- New balance must be less than previous balance
- Send amount must be positive

### Receive Block

- Previous block must exist (if not opening)
- Source send block must exist
- Pending entry must exist for this account/source
- Received amount must match send amount

### Open Block

- Account must not already exist
- Source send block must exist
- Pending entry must exist

### Change Block

- Previous block must exist and be account head
- Balance must equal previous balance

### State Block

- If account exists: previous must match account head
- If account doesn't exist: previous must be 0
- Balance must be valid relative to operation
- For receives: source block and pending must exist
- For sends: destination must be valid account

## Block Size

Block sizes are fixed per type:

```
send_block:    152 bytes
receive_block: 136 bytes
open_block:    168 bytes
change_block:  136 bytes
state_block:   216 bytes
```

State blocks are larger because they contain more information (account, representative, balance, link), but they're self-contained and more efficient overall.

## Visitor Pattern

Block processing uses the visitor pattern for polymorphic handling:

```cpp
class block_visitor {
    virtual void send_block(nano::send_block const &) = 0;
    virtual void receive_block(nano::receive_block const &) = 0;
    virtual void open_block(nano::open_block const &) = 0;
    virtual void change_block(nano::change_block const &) = 0;
    virtual void state_block(nano::state_block const &) = 0;
};
```

Implementations:
- **ledger_processor**: Processes blocks into ledger
- **ledger_rollback**: Rolls back blocks during forks
- **block_serializer**: Serializes blocks to JSON/binary
- **work_validator**: Validates proof-of-work

## Best Practices

### For Integrators

- **Always use state blocks** for new implementations
- Legacy blocks are supported but deprecated
- State blocks provide better pruning and efficiency
- Simpler to implement (one block type instead of four)

### For Node Operators

- Legacy blocks are still validated and processed
- Existing legacy blocks remain in the ledger
- No need to convert legacy blocks to state blocks
- The network naturally transitions to state blocks over time

### For Wallet Developers

- Generate state blocks for all operations
- Use appropriate link field based on operation:
  - Send: destination account
  - Receive: source block hash
  - Change: zero
- Include representative in every block
- Validate balance arithmetic before signing

## Block Hash Calculation

Blocks are identified by their Blake2b hash of the "hashables":

- **Send block**: `Blake2b(previous || destination || balance)`
- **Receive block**: `Blake2b(previous || source)`
- **Open block**: `Blake2b(source || representative || account)`
- **Change block**: `Blake2b(previous || representative)`
- **State block**: `Blake2b(account || previous || representative || balance || link)`

The signature and work are NOT included in the hash. This allows:
- Work to be computed after signing
- Work to be recomputed without invalidating the block
- Signature verification against a consistent hash

## Migration from Legacy to State Blocks

The network transitioned from legacy blocks to state blocks through epoch upgrades:

1. **Epoch 0**: Only legacy blocks (send, receive, open, change)
2. **Epoch 1**: State blocks introduced, both types supported
3. **Current**: State blocks are standard, legacy still processed

This gradual transition allowed:
- Backward compatibility
- No hard fork required
- Nodes and wallets to upgrade independently
- Existing ledger data to remain valid

## References

- **Block definitions**: `nano/lib/blocks.hpp`
- **Block processing**: `nano/secure/ledger_processor.cpp`
- **Block validation**: `nano/node/block_processor.cpp`
- **Sideband data**: `nano/lib/block_sideband.hpp`
- **Epoch handling**: `nano/lib/epoch.hpp`
