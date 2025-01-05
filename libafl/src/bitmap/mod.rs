//! Bitmap module for managing bit information.
use alloc::vec::Vec;

/// Bitmap structure used to store bit information.
#[derive(Debug)]
pub struct Bitmap {
    /// Total length of the bitmap in bits.
    pub len: usize,
    /// Byte array storing the bit information.
    pub buf: Vec<u8>,
}

impl Bitmap {
    /// Creates a new instance of the bitmap.
    ///
    /// # Arguments
    ///
    /// * `len` - Total length of the bitmap in bits.
    pub fn new(len: usize) -> Bitmap {
        let rounded_len = (len + 7) / 8 * 8;
        Bitmap {
            len: rounded_len,
            buf: vec![0; ((rounded_len + 7) / 8).try_into().unwrap()],
        }
    }

    /// Retrieves the value of a specific bit index.
    ///
    /// # Arguments
    ///
    /// * `idx` - Index of the bit to query.
    ///
    /// # Returns
    ///
    /// Returns `true` if the bit is set, otherwise returns `false`.
    pub fn get(&self, idx: usize) -> bool {
        if idx >= self.len {
            panic!("index out of range");
        }

        let byte = self.buf[idx / 8];
        (byte & (1 << (idx % 8))) != 0
    }

    /// Sets a specific bit index.
    ///
    /// # Arguments
    ///
    /// * `idx` - Index of the bit to set.
    pub fn set(&mut self, idx: usize) {
        if idx >= self.len {
            panic!("index out of range");
        }

        self.buf[idx / 8] |= 1 << (idx % 8);
    }

    /// Clears a specific bit index.
    ///
    /// # Arguments
    ///
    /// * `idx` - Index of the bit to clear.
    pub fn clear(&mut self, idx: usize) {
        if idx >= self.len {
            panic!("index out of range");
        }

        self.buf[idx / 8] &= !(1 << (idx % 8));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bitmap() {
        let mut bitmap = Bitmap::new(8);
        for i in 0..bitmap.len {
            assert_eq!(bitmap.get(i), false);
        }

        bitmap.set(0);
        assert_eq!(bitmap.get(0), true);

        bitmap.clear(0);
        assert_eq!(bitmap.get(0), false);
    }
}
