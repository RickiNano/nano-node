use num_traits::FromPrimitive;
use rsnano_network::AsyncBufferReader;
use rsnano_types::{Block, BlockType, BlockTypeId, serialized_block_size};

pub async fn read_block(input: &impl AsyncBufferReader) -> anyhow::Result<Option<Block>> {
    let mut buf = [0; 1];
    input.read(&mut buf, 1).await?;
    received_type(buf[0], input).await
}

async fn received_type(
    block_type_byte: u8,
    input: &impl AsyncBufferReader,
) -> anyhow::Result<Option<Block>> {
    let type_id =
        BlockTypeId::from_u8(block_type_byte).ok_or_else(|| anyhow!("Invalid block type"))?;

    match type_id {
        BlockTypeId::Invalid => Err(anyhow!("Invalid block type")),
        BlockTypeId::NotABlock => Ok(None),
        _ => {
            let block_type =
                BlockType::try_from(type_id).map_err(|_| anyhow!("Invalid block type"))?;

            let block_size = serialized_block_size(block_type);
            let mut buffer = [0; 256];
            input.read(&mut buffer, block_size).await?;
            let mut block_data = &buffer[..block_size];
            let block = Block::deserialize_block_type(block_type, &mut block_data)?;
            Ok(Some(block))
        }
    }
}
