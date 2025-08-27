pub trait Stream {
    fn write_bytes(&mut self, bytes: &[u8]) -> anyhow::Result<()>;
    fn read_u8(&mut self) -> anyhow::Result<u8>;
    fn read_bytes(&mut self, buffer: &mut [u8], len: usize) -> anyhow::Result<()>;

    ///  Looking ahead into the stream.
    ///  returns:  The number of characters available.
    ///  If a read position is available, returns the number of characters
    ///  available for reading before the buffer must be refilled.
    ///  Otherwise returns the derived showmanyc().
    fn data_available(&mut self) -> anyhow::Result<usize>;
}

pub trait StreamExt: Stream {
    fn read_u64_be(&mut self) -> anyhow::Result<u64> {
        let mut buffer = [0u8; 8];
        self.read_bytes(&mut buffer, 8)?;
        Ok(u64::from_be_bytes(buffer))
    }

    fn read_u64_le(&mut self) -> anyhow::Result<u64> {
        let mut buffer = [0u8; 8];
        self.read_bytes(&mut buffer, 8)?;
        Ok(u64::from_le_bytes(buffer))
    }

    fn read_u128_le(&mut self) -> anyhow::Result<u128> {
        let mut buffer = [0u8; 16];
        self.read_bytes(&mut buffer, 16)?;
        Ok(u128::from_le_bytes(buffer))
    }

    fn read_u128_be(&mut self) -> anyhow::Result<u128> {
        let mut buffer = [0u8; 16];
        self.read_bytes(&mut buffer, 16)?;
        Ok(u128::from_be_bytes(buffer))
    }

    fn read_u64_ne(&mut self) -> anyhow::Result<u64> {
        let mut buffer = [0u8; 8];
        self.read_bytes(&mut buffer, 8)?;
        Ok(u64::from_ne_bytes(buffer))
    }

    fn write_u32_be(&mut self, value: u32) -> anyhow::Result<()> {
        self.write_bytes(&value.to_be_bytes())
    }

    fn write_u64_be(&mut self, value: u64) -> anyhow::Result<()> {
        self.write_bytes(&value.to_be_bytes())
    }

    fn write_u64_ne(&mut self, value: u64) -> anyhow::Result<()> {
        self.write_bytes(&value.to_ne_bytes())
    }
}

impl<T: Stream + ?Sized> StreamExt for T {}

pub struct BufferReader<'a> {
    bytes: &'a [u8],
    read_index: usize,
}

impl<'a> BufferReader<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self {
            bytes,
            read_index: 0,
        }
    }

    pub fn remaining(&self) -> &[u8] {
        &self.bytes[self.read_index..]
    }
}

impl<'a> Stream for BufferReader<'a> {
    fn write_bytes(&mut self, _bytes: &[u8]) -> anyhow::Result<()> {
        bail!("not supported");
    }

    fn read_u8(&mut self) -> anyhow::Result<u8> {
        if self.read_index >= self.bytes.len() {
            bail!("no more bytes to read")
        }

        let result = self.bytes[self.read_index];
        self.read_index += 1;
        Ok(result)
    }

    fn read_bytes(&mut self, buffer: &mut [u8], len: usize) -> anyhow::Result<()> {
        if self.read_index + len > self.bytes.len() {
            bail!("not enough bytes to read")
        }

        buffer.copy_from_slice(&self.bytes[self.read_index..self.read_index + len]);
        self.read_index += len;
        Ok(())
    }

    fn data_available(&mut self) -> anyhow::Result<usize> {
        Ok(self.bytes.len() - self.read_index)
    }
}

pub trait Deserialize {
    type Target;
    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<Self::Target>;
}

impl Deserialize for u64 {
    type Target = Self;
    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<u64> {
        stream.read_u64_be()
    }
}

impl Deserialize for [u8; 64] {
    type Target = Self;

    fn deserialize(stream: &mut dyn Stream) -> anyhow::Result<Self::Target> {
        let mut buffer = [0; 64];
        stream.read_bytes(&mut buffer, 64)?;
        Ok(buffer)
    }
}
