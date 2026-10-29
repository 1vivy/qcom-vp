use anyhow::{bail, ensure, Context, Result};

/// Parse the integer expressions that Qualcomm storage XML uses.
pub(super) fn eval(text: &str, sectors: u64) -> Result<u64> {
    ensure!(text.len() <= 1024, "Storage expression exceeds 1024 bytes");
    let mut parser = Parser {
        input: text.as_bytes(),
        position: 0,
        sectors,
        depth: 0,
    };
    let value = parser.sum()?;
    parser.space();
    ensure!(
        parser.position == parser.input.len(),
        "Invalid storage expression: {text}"
    );
    Ok(value)
}

struct Parser<'a> {
    input: &'a [u8],
    position: usize,
    sectors: u64,
    depth: u8,
}

impl Parser<'_> {
    fn space(&mut self) {
        while self
            .input
            .get(self.position)
            .is_some_and(u8::is_ascii_whitespace)
        {
            self.position += 1;
        }
    }

    fn sum(&mut self) -> Result<u64> {
        let mut value = self.product()?;
        loop {
            self.space();
            match self.input.get(self.position) {
                Some(b'+') => {
                    self.position += 1;
                    value = value
                        .checked_add(self.product()?)
                        .context("Storage expression overflows")?;
                }
                Some(b'-') => {
                    self.position += 1;
                    value = value
                        .checked_sub(self.product()?)
                        .context("Storage expression underflows")?;
                }
                _ => return Ok(value),
            }
        }
    }

    fn product(&mut self) -> Result<u64> {
        let mut value = self.atom()?;
        loop {
            self.space();
            if self.input.get(self.position) != Some(&b'*') {
                return Ok(value);
            }
            self.position += 1;
            value = value
                .checked_mul(self.atom()?)
                .context("Storage expression overflows")?;
        }
    }

    fn atom(&mut self) -> Result<u64> {
        self.space();
        if self.input[self.position..].starts_with(b"NUM_DISK_SECTORS") {
            self.position += b"NUM_DISK_SECTORS".len();
            return Ok(self.sectors);
        }
        if self.input.get(self.position) == Some(&b'(') {
            ensure!(
                self.depth < 32,
                "Storage expression nesting exceeds 32 levels"
            );
            self.depth += 1;
            self.position += 1;
            let value = self.sum()?;
            self.space();
            ensure!(
                self.input.get(self.position) == Some(&b')'),
                "Unclosed storage expression"
            );
            self.position += 1;
            self.depth -= 1;
            return Ok(value);
        }
        let start = self.position;
        let hex = self.input[self.position..].starts_with(b"0x");
        if hex {
            self.position += 2;
        }
        let digits = self.position;
        while self.input.get(self.position).is_some_and(|b| {
            if hex {
                b.is_ascii_hexdigit()
            } else {
                b.is_ascii_digit()
            }
        }) {
            self.position += 1;
        }
        if self.position == digits {
            bail!("Expected an integer in storage expression");
        }
        let value = u64::from_str_radix(
            std::str::from_utf8(&self.input[digits..self.position])?,
            if hex { 16 } else { 10 },
        )?;
        if self.input.get(self.position) == Some(&b'.') {
            ensure!(!hex, "A hexadecimal integer cannot have a trailing dot");
            self.position += 1;
        }
        ensure!(self.position > start, "Invalid storage integer");
        Ok(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn package_expressions() {
        assert_eq!(eval("NUM_DISK_SECTORS-5.", 2048).unwrap(), 2043);
        assert_eq!(
            eval("(4096*NUM_DISK_SECTORS)-20480.", 2048).unwrap(),
            2043 * 4096
        );
        assert_eq!(eval("0x10-10.", 0).unwrap(), 6);
        assert_eq!(eval("2+3*4", 0).unwrap(), 14);
    }

    #[test]
    fn reject_invalid_expressions() {
        for text in [
            "",
            "NUM_DISK_SECTORS-1",
            "1.2",
            "1/2",
            "0x",
            "18446744073709551616",
            "(1",
            "1<<2",
            "2**3",
            "1,2",
            "CRC32(1,2)",
        ] {
            assert!(eval(text, 0).is_err(), "{text}");
        }
        assert!(eval("18446744073709551615+1", 0).is_err());
    }
}
